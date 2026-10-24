#include "Engine.h"

namespace liszt {

namespace {

juce::File firstExisting(std::initializer_list<const char*> paths)
{
    for (auto* p : paths)
        if (juce::File f(p); f.exists()) return f;
    return {};
}

// The patch name stored inside a Surge patch's XML: <meta name="..." .../>
juce::String metaName(const void* data, size_t size)
{
    auto text = juce::String::createStringFromData(data, int(size));
    auto at = text.indexOf("<meta name=\"");
    return at < 0 ? juce::String() : text.substring(at + 12).upToFirstOccurrenceOf("\"", false, false);
}

} // namespace

juce::File findSurgePlugin()
{
    return firstExisting({ "C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT.vst3",
                           "C:/Program Files/Common Files/VST3/Surge XT.vst3",
                           "/Library/Audio/Plug-Ins/VST3/Surge XT.vst3" });
}

juce::File findSurgePatch(const juce::String& categoryAndName)
{
    auto root = firstExisting({ "C:/Program Files/Common Files/VST3/Surge Synth Team/SurgeXTData/patches_factory",
                                "C:/ProgramData/Surge XT/patches_factory",
                                "/Library/Application Support/Surge XT/patches_factory" });
    return root.getChildFile(categoryAndName + ".fxp");
}

std::unique_ptr<juce::AudioPluginInstance> createSurge(double sampleRate, int maxBlock, juce::String& error)
{
    auto file = findSurgePlugin();
    if (!file.exists()) { error = "Surge XT is not installed (no Surge XT.vst3 found)"; return nullptr; }

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile(types, file.getFullPathName());
    if (types.isEmpty()) { error = "could not read " + file.getFullPathName(); return nullptr; }
    auto p = vst3.createInstanceFromDescription(*types[0], sampleRate, maxBlock, error);
    if (p) p->enableAllBuses();
    return p;
}

juce::String loadSurgePatch(juce::AudioPluginInstance& surge, const juce::File& fxp)
{
    juce::MemoryBlock file;
    if (!fxp.loadFileAsData(file) || file.getSize() < 64)
        return "missing patch " + fxp.getFullPathName();

    // A .fxp is a 60-byte VST2 chunk header followed by Surge's own state ('sub3' ...),
    // which is exactly what Surge's VST3 component state holds. JUCE's VST3 host wraps
    // that state in a small XML document, so build that document around the patch.
    constexpr size_t kFxpHeader = 60;
    juce::MemoryBlock patch(static_cast<const char*>(file.getData()) + kFxpHeader, file.getSize() - kFxpHeader);
    if (patch.getSize() < 4 || std::memcmp(patch.getData(), "sub3", 4) != 0)
        return "not a Surge patch: " + fxp.getFileName();

    juce::XmlElement xml("VST3PluginState");
    xml.createNewChildElement("IComponent")->addTextElement(patch.toBase64Encoding());
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    surge.setStateInformation(state.getData(), int(state.getSize()));

    // Read back: the patch name inside the plugin's state must now be this patch's.
    juce::MemoryBlock now;
    surge.getStateInformation(now);
    juce::String loaded;
    if (auto x = juce::AudioProcessor::getXmlFromBinary(now.getData(), int(now.getSize())))
        if (auto* c = x->getChildByName("IComponent"))
        {
            juce::MemoryBlock m;
            m.fromBase64Encoding(c->getAllSubText());
            loaded = metaName(m.getData(), m.getSize());
        }
    auto wanted = metaName(patch.getData(), patch.getSize());
    if (loaded != wanted)
        return "patch did not load: wanted '" + wanted + "', Surge has '" + loaded + "'";
    return {};
}

void PeakLimiter::prepare(double sr, float thresholdDb, float releaseMs)
{
    look_ = juce::jmax(1, int(0.0015 * sr));
    need_.assign(size_t(look_), 1.0f);
    delayL_.assign(size_t(look_), 0.0f);
    delayR_.assign(size_t(look_), 0.0f);
    threshold_ = juce::Decibels::decibelsToGain(thresholdDb);
    attack_ = 1.0f - std::exp(-5.0f / float(look_)); // within 0.7% of the target by the time the peak plays
    release_ = std::exp(-1.0f / float(releaseMs * 0.001 * sr));
    reset();
}

void PeakLimiter::reset()
{
    std::fill(need_.begin(), need_.end(), 1.0f);
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    gain_ = 1;
    at_ = 0;
}

void PeakLimiter::process(float* l, float* r, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float peak = juce::jmax(std::abs(l[i]), std::abs(r[i]));
        need_[size_t(at_)] = peak > threshold_ ? threshold_ / peak : 1.0f;
        float lowest = 1;
        for (float g : need_) lowest = juce::jmin(lowest, g);
        gain_ = lowest < gain_ ? gain_ + (lowest - gain_) * attack_ : lowest + (gain_ - lowest) * release_;

        const float outL = delayL_[size_t(at_)] * gain_, outR = delayR_[size_t(at_)] * gain_;
        delayL_[size_t(at_)] = l[i];
        delayR_[size_t(at_)] = r[i];
        at_ = (at_ + 1) % look_;

        l[i] = juce::jlimit(-threshold_, threshold_, outL);
        r[i] = juce::jlimit(-threshold_, threshold_, outR);
        clipped += (l[i] != outL) + (r[i] != outR);
        maxReductionDb = juce::jmin(maxReductionDb, juce::Decibels::gainToDecibels(gain_));
    }
}

Engine::Engine() { events_.reserve(1 << 15); }

Engine::~Engine()
{
    for (auto& t : tracks_)
        if (t.plugin) t.plugin->releaseResources();
}

juce::String Engine::load(double sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    gains_.assign(size_t(maxBlock), 0);

    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = tracks_[i];
        juce::String err;
        t.plugin = createSurge(sampleRate, maxBlock, err);
        if (!t.plugin) return err;
        auto patch = patchOverride[i].isNotEmpty() ? patchOverride[i] : juce::String(kBerlinSounds[i].patch);
        if (auto e = loadSurgePatch(*t.plugin, findSurgePatch(patch)); e.isNotEmpty())
            return juce::String(kTrackNames[i]) + ": " + e;

        t.plugin->prepareToPlay(sampleRate, maxBlock);
        t.buffer.setSize(juce::jmax(2, t.plugin->getTotalNumInputChannels(), t.plugin->getTotalNumOutputChannels()), maxBlock);
        t.midi.ensureSize(8192);
        t.trim = juce::Decibels::decibelsToGain(kBerlinSounds[i].trimDb);
        t.cutoff = nullptr;
        for (auto* p : t.plugin->getParameters())
            if (p->getName(64) == "A Filter 1 Cutoff") { t.cutoff = p; t.cutoffBase = p->getValue(); }
    }

    limiter.prepare(sampleRate, -1.0f, 150.0f);
    return {};
}

void Engine::start(uint64_t seed)
{
    auto next = std::make_unique<Composer>(seed);
    const juce::SpinLock::ScopedLockType sl(lock_);
    composer_ = std::move(next);
    samplesPerTick_ = sampleRate_ * 60.0 / (composer_->bpm() * kPPQ);
    endSample_ = sampleOf(int64_t(composer_->totalBars()) * kBarTicks);
    events_.clear();
    head_ = 0;
    nextBar_ = 0;
    pos_ = -preRollSamples(); // 50 ms pre-roll: the all-notes-off sent on start must reach the plugins before bar 0. Sent in the same block, Surge applied it after the first notes and the opening pad was silent for 8 bars.
    clickEnv_ = 0;
    const double barSeconds = 4 * 60.0 / composer_->bpm();
    for (auto& t : tracks_)
    {
        t.gain.reset(sampleRate_, barSeconds); // level changes glide across a whole bar
        t.gain.setCurrentAndTargetValue(0);
        t.filterNow = t.filterTarget = 0;
    }
    limiter.reset();
    bar = 0;
    finished_ = false;
    panic_ = true;
    playing_ = true;
}

void Engine::stop()
{
    playing_ = false;
    panic_ = true;
}

void Engine::render(float* const* out, int numChannels, int numSamples)
{
    for (int c = 0; c < numChannels; ++c)
        if (out[c]) juce::FloatVectorOperations::clear(out[c], numSamples);

    const juce::SpinLock::ScopedTryLockType sl(lock_);
    if (!sl.isLocked() || !composer_) return;

    for (int done = 0; done < numSamples;)
    {
        int len = juce::jmin(numSamples - done, maxBlock_);
        renderChunk(out, numChannels, done, len);
        done += len;
    }
}

void Engine::renderChunk(float* const* out, int numChannels, int offset, int len)
{
    const bool playing = playing_.load();
    const int64_t end = pos_ + len;

    if (panic_.exchange(false))
        for (auto& t : tracks_)
        {
            t.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            t.midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
        }

    if (playing)
    {
        // Compose just ahead of the playhead, one bar at a time, into a sorted queue.
        // Ticks become samples from the absolute tick count, so nothing accumulates or drifts.
        while (nextBar_ < composer_->totalBars() && sampleOf(int64_t(nextBar_) * kBarTicks) < end + maxBlock_)
        {
            composer_->generateBar(nextBar_++, events_);
            std::sort(events_.begin() + long(head_), events_.end());
        }

        for (; head_ < events_.size(); ++head_)
        {
            const auto& e = events_[head_];
            const int64_t s = sampleOf(e.tick);
            if (s >= end) break;
            const int at = int(juce::jlimit<int64_t>(0, len - 1, s - pos_));
            auto& t = tracks_[e.track];
            switch (e.type)
            {
                case Event::NoteOn:  t.midi.addEvent(juce::MidiMessage::noteOn(1, e.pitch, e.value), at); break;
                case Event::NoteOff: t.midi.addEvent(juce::MidiMessage::noteOff(1, e.pitch), at); break;
                case Event::Level:   t.gain.setTargetValue(e.value); break;
                case Event::Filter:  t.filterTarget = e.value; break;
            }
        }
        if (head_ > 8192) // keep the queue's memory; just drop what has played
        {
            events_.erase(events_.begin(), events_.begin() + long(head_));
            head_ = 0;
        }
        bar = int(juce::jlimit<int64_t>(0, composer_->totalBars() - 1, int64_t(double(pos_) / samplesPerTick_) / kBarTicks));
    }

    float* pa[2] = { out[0] + offset, (numChannels > 1 ? out[1] : out[0]) + offset };
    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = tracks_[i];
        if (!t.plugin || !(enabledTracks & (1 << i))) continue;

        if (t.cutoff)
        {
            // Glide the filter towards its target over about a quarter second.
            t.filterNow += (t.filterTarget - t.filterNow) * juce::jmin(1.0f, float(len / (0.25 * sampleRate_)));
            float v = juce::jlimit(0.0f, 1.0f, t.cutoffBase + 0.2f * t.filterNow);
            if (std::abs(v - t.cutoff->getValue()) > 1e-4f) t.cutoff->setValue(v);
        }

        t.buffer.clear();
        juce::AudioBuffer<float> view(t.buffer.getArrayOfWritePointers(), t.buffer.getNumChannels(), len);
        t.plugin->processBlock(view, t.midi);
        t.midi.clear();

        for (int n = 0; n < len; ++n) gains_[size_t(n)] = t.gain.getNextValue() * t.trim;
        float peak = 0;
        double sq = 0;
        for (int c = 0; c < 2; ++c)
        {
            const float* src = view.getReadPointer(juce::jmin(c, view.getNumChannels() - 1));
            for (int n = 0; n < len; ++n)
            {
                float x = src[n] * gains_[size_t(n)];
                pa[c][n] += x;
                peak = juce::jmax(peak, std::abs(x));
                sq += double(x) * x;
            }
        }
        meters[i].peak = juce::jmax(meters[i].peak.load(), peak);
        meters[i].sumSquares = meters[i].sumSquares.load() + sq;
        meters[i].samples = meters[i].samples.load() + 2 * len;
    }

    float pre = 0;
    for (int c = 0; c < 2; ++c)
        pre = juce::jmax(pre, juce::FloatVectorOperations::findMaximum(pa[c], len), -juce::FloatVectorOperations::findMinimum(pa[c], len));
    peakPre = juce::jmax(peakPre.load(), pre);

    if (numChannels > 1) limiter.process(pa[0], pa[1], len);
    float post = 0;
    for (int c = 0; c < juce::jmin(2, numChannels); ++c)
        post = juce::jmax(post, juce::FloatVectorOperations::findMaximum(pa[c], len), -juce::FloatVectorOperations::findMinimum(pa[c], len));
    peakPost = juce::jmax(peakPost.load(), post);

    if (playing && numChannels >= 4) renderClick(out, numChannels, offset, len);

    if (playing) { pos_ = end; seconds = double(pos_) / sampleRate_; }
    if (playing && nextBar_ >= composer_->totalBars() && head_ >= events_.size() && pos_ > endSample_ + int64_t(6 * sampleRate_))
    {
        finished_ = true;
        playing_ = false;
    }
}

// The click for the drummer's in-ears: a short sine blip on every beat, higher on the
// downbeat, with an exponential decay so it never ticks at its own end.
void Engine::renderClick(float* const* out, int numChannels, int offset, int len)
{
    const int64_t beatTicks = kPPQ;
    const double decay = std::exp(-1.0 / (0.025 * sampleRate_));
    for (int n = 0; n < len; ++n)
    {
        const int64_t s = pos_ + n;
        const int64_t beat = int64_t(std::ceil(double(s) / samplesPerTick_ / beatTicks));
        if (sampleOf(beat * beatTicks) == s && s < endSample_)
        {
            clickFreq_ = beat % 4 == 0 ? 1760.0 : 1320.0;
            clickEnv_ = clickLevel;
            clickPhase_ = 0;
        }
        float x = float(clickEnv_ * std::sin(clickPhase_));
        clickPhase_ += juce::MathConstants<double>::twoPi * clickFreq_ / sampleRate_;
        clickEnv_ *= decay;
        out[2][offset + n] = x;
        out[3][offset + n] = x;
    }
    juce::ignoreUnused(numChannels);
}

} // namespace liszt
