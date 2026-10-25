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

juce::File findSurgeEffects()
{
    return firstExisting({ "C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT Effects.vst3",
                           "C:/Program Files/Common Files/VST3/Surge XT Effects.vst3",
                           "/Library/Audio/Plug-Ins/VST3/Surge XT Effects.vst3" });
}

static std::unique_ptr<juce::AudioPluginInstance> createVst3(const juce::File& file, double sampleRate, int maxBlock,
                                                             juce::String& error)
{
    if (!file.exists()) { error = "not installed: " + file.getFileName(); return nullptr; }
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile(types, file.getFullPathName());
    if (types.isEmpty()) { error = "could not read " + file.getFullPathName(); return nullptr; }
    return vst3.createInstanceFromDescription(*types[0], sampleRate, maxBlock, error);
}

std::unique_ptr<juce::AudioPluginInstance> createSurge(double sampleRate, int maxBlock, juce::String& error)
{
    auto file = findSurgePlugin();
    if (!file.exists()) { error = "Surge XT is not installed (no Surge XT.vst3 found)"; return nullptr; }
    auto p = createVst3(file, sampleRate, maxBlock, error);
    if (p) p->enableAllBuses();
    return p;
}

juce::String setParameterText(juce::AudioPluginInstance& plugin, const juce::String& name, const juce::String& value)
{
    juce::AudioProcessorParameter* p = nullptr;
    for (auto* q : plugin.getParameters())
        if (q->getName(128) == name) p = q;
    if (!p)
    {
        juce::StringArray names;
        for (auto* q : plugin.getParameters()) names.add(q->getName(128));
        return "no parameter '" + name + "' (has: " + names.joinIntoString(", ") + ")";
    }

    // Numbers compare as numbers ("7 s" vs "7.00 s"); choices compare as text.
    const bool numeric = value.trimStart().containsAnyOf("0123456789") && (juce::CharacterFunctions::isDigit(value.trimStart()[0]) || value.trimStart()[0] == '-');
    auto num = [](const juce::String& s) { return s.retainCharacters("-0123456789.").getDoubleValue(); };
    auto matches = [&] {
        auto t = p->getCurrentValueAsText();
        return numeric ? std::abs(num(t) - num(value)) <= 0.02 * std::abs(num(value)) + 0.011 : t.equalsIgnoreCase(value);
    };

    if (!numeric)
    {
        // A choice: set the middle of the range that shows this label. Its edges are where
        // host and plugin round differently: "Distortion" at the edge ran as Rotary Speaker.
        int first = -1, last = -1;
        for (int i = 0; i <= 2000; ++i)
        {
            p->setValue(float(i) / 2000.0f);
            if (p->getCurrentValueAsText().equalsIgnoreCase(value)) { if (first < 0) first = i; last = i; }
            else if (first >= 0) break;
        }
        if (first >= 0) { p->setValue(float(first + last) / 4000.0f); return {}; }
    }
    else
    {
        p->setValue(p->getValueForText(value));
        if (matches()) return {};
    }

    // The plugin did not parse the text: search the normalised range for it instead.
    float best = -1;
    double bestErr = 1e300;
    juce::StringArray seen;
    for (int i = 0; i <= 2000; ++i)
    {
        p->setValue(float(i) / 2000.0f);
        auto t = p->getCurrentValueAsText();
        seen.addIfNotAlreadyThere(t);
        double err = numeric ? std::abs(num(t) - num(value)) : (t.equalsIgnoreCase(value) ? 0.0 : 1e300);
        if (err < bestErr) { bestErr = err; best = float(i) / 2000.0f; }
    }
    if (best >= 0) p->setValue(best);
    if (matches()) return {};
    return "'" + name + "' wanted " + value + ", got " + p->getCurrentValueAsText()
         + (numeric || seen.size() > 40 ? juce::String() : " (choices: " + seen.joinIntoString(", ") + ")");
}

std::unique_ptr<juce::AudioPluginInstance> createSurgeEffect(const FxSpec& spec, double sampleRate, int maxBlock,
                                                             juce::String& error)
{
    auto fx = createVst3(findSurgeEffects(), sampleRate, maxBlock, error);
    if (!fx) return nullptr;
    fx->prepareToPlay(sampleRate, maxBlock);
    if (auto e = setParameterText(*fx, "FX Type", spec.type); e.isNotEmpty()) { error = e; return nullptr; }

    // The parameters take their names and ranges from the type once the plugin has run with it.
    juce::AudioBuffer<float> buf(juce::jmax(2, fx->getTotalNumInputChannels(), fx->getTotalNumOutputChannels()), maxBlock);
    juce::MidiBuffer midi;
    for (int i = 0; i < 4; ++i) { buf.clear(); fx->processBlock(buf, midi); }

    for (auto& prm : spec.params)
        if (auto e = setParameterText(*fx, prm.name, prm.value); e.isNotEmpty())
        {
            error = juce::String(spec.type) + ": " + e;
            return nullptr;
        }
    return fx;
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

juce::String Palette::load(const juce::File& file)
{
    for (auto& p : parts) p.clear();
    if (!file.existsAsFile()) return "no palette at " + file.getFullPathName();

    juce::StringArray lines;
    file.readLines(lines);
    for (int n = 0; n < lines.size(); ++n)
    {
        auto line = lines[n].upToFirstOccurrenceOf("#", false, false).trim();
        if (line.isEmpty()) continue;
        auto tokens = juce::StringArray::fromTokens(line, " \t", "");
        tokens.removeEmptyStrings();
        int track = -1;
        for (int t = 0; t < kNumTracks; ++t)
            if (tokens[0] == kTrackNames[t]) track = t;
        if (track < 0 || tokens.size() < 4)
            return file.getFileName() + " line " + juce::String(n + 1) + ": expected 'part gain bright Category/Patch'";
        // The patch name is the rest of the line: it may contain spaces.
        auto patch = line.fromFirstOccurrenceOf(tokens[2], false, false).trim();
        if (!findSurgePatch(patch).existsAsFile())
            return file.getFileName() + " line " + juce::String(n + 1) + ": no Surge patch '" + patch + "'";
        parts[track].push_back({ patch, tokens[1].getFloatValue(), tokens[2].getFloatValue() });
    }
    for (auto& p : parts)
        std::stable_sort(p.begin(), p.end(), [](auto& a, auto& b) { return a.brightness < b.brightness; });
    for (int t = 0; t < kNumTracks; ++t)
        if (parts[t].empty()) return file.getFileName() + ": no patches for " + kTrackNames[t];
    return {};
}

juce::File findPalette(const juce::String& name)
{
    // Next to the executable, or in a sounds/ folder of any parent (the repo, in development).
    auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int up = 0; up < 8 && dir.exists(); ++up, dir = dir.getParentDirectory())
        for (auto sub : { "sounds", "Resources/sounds" }) // Resources/: inside the Mac .app bundle
            if (auto f = dir.getChildFile(sub).getChildFile(name + ".txt"); f.existsAsFile()) return f;
    return juce::File("C:/Users/kerem/OneDrive/Desktop/Claude/Projects/Lisztmachine/sounds").getChildFile(name + ".txt");
}

std::vector<SoundChange> planSounds(const Composer& composer, const Palette& palette, uint64_t seed)
{
    static constexpr int offsetBars[kNumTracks] = { 0, 4, 2, 0 }; // pad, bass, seq, lead
    Rng rng(seed * 0x9E3779B97F4A7C15ull + 7);
    std::vector<SoundChange> plan;
    const auto& sections = composer.sections();
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int n = int(palette.parts[t].size());
        int last = -1, beforeLast = -1;
        for (int s = 0; s < int(sections.size()); ++s)
        {
            // Pad and sequence change on even sections, bass and lead on odd ones.
            const bool evenPart = t == Pad || t == Seq;
            if (s > 0 && (s % 2 == 0) != evenPart) continue;

            // Energy picks a place on the dark-to-bright line, with a little seeded wander,
            // and never one of the last two sounds again.
            const double target = sections[size_t(s)].energy * (n - 1) + (rng.uniform() - 0.5) * n * 0.3;
            int best = -1;
            for (int e = 0; e < n; ++e)
                if ((n <= 2 || (e != last && e != beforeLast)) && (best < 0 || std::abs(e - target) < std::abs(best - target)))
                    best = e;
            const auto& sec = sections[size_t(s)];
            const int bar = s == 0 ? 0 : juce::jmin(sec.startBar + offsetBars[t], sec.startBar + sec.bars - 1);
            plan.push_back({ bar, t, best });
            beforeLast = last;
            last = best;
        }
    }
    std::sort(plan.begin(), plan.end(), [](auto& a, auto& b) { return a.bar != b.bar ? a.bar < b.bar : a.track < b.track; });
    return plan;
}

Engine::Engine() { events_.reserve(1 << 15); }

Engine::~Engine()
{
    for (auto& t : tracks_)
        for (auto& s : t.slots)
            if (s.plugin) s.plugin->releaseResources();
}

juce::String Engine::load(double sampleRate, int maxBlock, const Palette& palette)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    palette_ = palette;
    gains_.assign(size_t(maxBlock), 0);

    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = tracks_[i];
        int channels = 2;
        for (auto& s : t.slots)
        {
            juce::String err;
            s.plugin = createSurge(sampleRate, maxBlock, err);
            if (!s.plugin) return err;
            s.plugin->prepareToPlay(sampleRate, maxBlock);
            s.midi.ensureSize(8192);
            s.state = Idle;
            s.entry = -1;
            channels = juce::jmax(channels, s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels());
        }
        t.buffer.setSize(channels, maxBlock);
    }

    juce::String err;
    delay_ = createSurgeEffect(kDelayBus, sampleRate, maxBlock, err);
    if (!delay_) return "delay bus: " + err;
    reverb_ = createSurgeEffect(kReverbBus, sampleRate, maxBlock, err);
    if (!reverb_) return "reverb bus: " + err;
    auto busChannels = [](juce::AudioPluginInstance& p) { return juce::jmax(2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()); };
    delayBus_.setSize(busChannels(*delay_), maxBlock);
    reverbBus_.setSize(busChannels(*reverb_), maxBlock);

    limiter.prepare(sampleRate, -1.0f, 150.0f);
    return {};
}

// Message thread, on an instance the audio thread is not touching (Idle or Loading).
juce::String Engine::loadInto(Slot& slot, int track, int entry)
{
    const auto& e = palette_.parts[track][size_t(entry)];
    if (auto err = loadSurgePatch(*slot.plugin, findSurgePatch(e.patch)); err.isNotEmpty()) return err;
    slot.plugin->reset();
    slot.cutoff = nullptr;
    for (auto* p : slot.plugin->getParameters())
        if (p->getName(64) == "A Filter 1 Cutoff") { slot.cutoff = p; slot.cutoffBase = p->getValue(); }
    slot.gain = juce::Decibels::decibelsToGain(e.gainDb);
    slot.entry = entry;
    slot.quiet = 0;
    slot.midi.clear();
    return {};
}

juce::String Engine::delayDescription() const
{
    const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
    return "1/8." + dot + juce::String(delaySeconds_[0], 2) + " s | 1/4" + dot + juce::String(delaySeconds_[1], 2) + " s";
}

juce::String Engine::currentPatch(int track) const
{
    const auto& t = tracks_[track];
    const int e = t.slots[t.live.load()].entry;
    return e < 0 ? juce::String() : palette_.parts[track][size_t(e)].patch;
}

void Engine::start(uint64_t seed, int startBar)
{
    auto next = makeComposer(scene, seed);
    startBar = juce::jlimit(0, next->totalBars() - 1, startBar);

    // Starting later: compose the bars before it silently (the sequences mutate as they go,
    // so this is the only way to arrive at the same bar), keep the levels and filters they
    // left behind, and re-strike every note still held at the start (A-15: a held pad must
    // not wait for the next chord change).
    const int64_t startTick = int64_t(startBar) * kBarTicks;
    std::vector<Event> earlier, carried;
    // Including the start bar itself: the pad releases its old chord on the first tick of
    // the new bar, and without that release the re-struck old chord hung under the new one.
    for (int b = 0; b <= startBar; ++b) next->generateBar(b, earlier);
    std::sort(earlier.begin(), earlier.end());
    float level[kNumTracks] = {}, filter[kNumTracks] = {}, held[kNumTracks][128] = {};
    for (auto& e : earlier)
    {
        // A note-off on the start tick ends a note before it can be re-struck.
        if (e.tick < startTick || (e.tick == startTick && e.type == Event::NoteOff))
            switch (e.type)
            {
                case Event::Level:   level[e.track] = e.value; break;
                case Event::Filter:  filter[e.track] = e.value; break;
                case Event::NoteOn:  held[e.track][e.pitch] = e.value; break;
                case Event::NoteOff: held[e.track][e.pitch] = 0; break;
            }
        else
            carried.push_back(e);
    }
    for (int t = 0; t < kNumTracks; ++t)
        for (int p = 0; p < 128; ++p)
            if (held[t][p] > 0) carried.push_back({ startTick, Event::NoteOn, uint8_t(t), uint8_t(p), held[t][p] });
    std::sort(carried.begin(), carried.end());

    auto plan = planSounds(*next, palette_, seed);

    // The delay is a division of the bar (A-9): dotted eighth left, quarter right; the
    // tape wobble takes one bar per cycle.
    const double beat = 60.0 / next->bpm();
    delaySeconds_[0] = float(0.75 * beat);
    delaySeconds_[1] = float(beat);
    if (delay_)
    {
        setParameterText(*delay_, "Delay Time Left", juce::String(delaySeconds_[0] * 1000.0, 2) + " ms");
        setParameterText(*delay_, "Delay Time Right", juce::String(delaySeconds_[1] * 1000.0, 2) + " ms");
        setParameterText(*delay_, "Modulation Rate", juce::String(1.0 / (4 * beat), 3) + " Hz");
    }

    // Hold the lock for the whole reload: the audio thread plays silence meanwhile.
    const juce::SpinLock::ScopedLockType sl(lock_);
    composer_ = std::move(next);
    plan_ = std::move(plan);
    samplesPerTick_ = sampleRate_ * 60.0 / (composer_->bpm() * kPPQ);
    endSample_ = sampleOf(int64_t(composer_->totalBars()) * kBarTicks);
    events_.assign(carried.begin(), carried.end());
    head_ = 0;
    nextBar_ = startBar + 1;
    pos_ = sampleOf(startTick) - preRollSamples(); // 50 ms pre-roll: the all-notes-off sent on start must reach the plugins before bar 0. Sent in the same block, Surge applied it after the first notes and the opening pad was silent for 8 bars.
    clickEnv_ = 0;
    const double barSeconds = 4 * 60.0 / composer_->bpm();
    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = tracks_[i];
        t.gain.reset(sampleRate_, barSeconds); // level changes glide across a whole bar
        t.gain.setCurrentAndTargetValue(level[i]);
        t.filterNow = t.filterTarget = filter[i];
        std::copy(std::begin(held[i]), std::end(held[i]), std::begin(t.held));

        // This part's sound at the start bar is the last change at or before it.
        t.changes.clear();
        int entry = 0;
        for (auto& c : plan_)
            if (c.track == i && c.bar <= startBar) entry = c.entry;
            else if (c.track == i) t.changes.push_back(c);
        t.nextChange = 0;
        t.live = 0;
        t.slots[1].state = Idle;
        t.slots[1].entry = -1;
        if (auto e = loadInto(t.slots[0], i, entry); e.isNotEmpty()) DBG(e);
        t.slots[0].state = Live;
    }
    changesMade = 0;
    missedChanges = 0;
    limiter.reset();
    bar = startBar;
    finished_ = false;
    panic_ = true;
    playing_ = true;
}

void Engine::stop()
{
    playing_ = false;
    panic_ = true;
}

void Engine::service()
{
    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = tracks_[i];
        const size_t next = t.nextChange.load();
        if (next >= t.changes.size()) continue;
        auto& spare = t.slots[1 - t.live.load()];
        int expected = Idle;
        if (!spare.state.compare_exchange_strong(expected, Loading)) continue; // still ringing out, or already loaded
        if (auto e = loadInto(spare, i, t.changes[next].entry); e.isNotEmpty())
        {
            DBG(e);
            spare.state = Idle;
            continue;
        }
        spare.state = Ready;
    }
}

// Audio thread. Hands the part to its other instance at sample `at`: the notes still held
// move across (released on the old sound, struck on the new), and the old one rings out.
void Engine::changeSound(int track, int at)
{
    auto& t = tracks_[track];
    const size_t next = t.nextChange.load();
    auto& from = t.slots[t.live.load()];
    auto& to = t.slots[1 - t.live.load()];
    const int64_t tick = int64_t(t.changes[next].bar) * kBarTicks;
    if (to.state.load() == Ready && to.entry == t.changes[next].entry)
    {
        for (int p = 0; p < 128; ++p)
            if (t.held[p] > 0)
            {
                from.releasing[p] = true;
                // A note released on this very tick (the pad's old chord) is not carried over:
                // striking it on the new sound only to release it at once would blip.
                bool endsHere = false;
                for (size_t k = head_; k < events_.size() && events_[k].tick == tick && !endsHere; ++k)
                    endsHere = events_[k].type == Event::NoteOff && events_[k].track == track && events_[k].pitch == p;
                if (endsHere)
                {
                    from.releasing[p] = false;
                    from.midi.addEvent(juce::MidiMessage::noteOff(1, p), at);
                }
                else
                    to.midi.addEvent(juce::MidiMessage::noteOn(1, p, t.held[p]), at);
            }
        // The old sound holds its notes for one more bar while the new one fades in, so a
        // slow-attack pad crossfades instead of dipping (measured: a 7.6 dB dip without this).
        from.releaseAt = pos_ + at + int64_t(4 * kPPQ * samplesPerTick_);
        from.quiet = 0;
        from.tailed = 0;
        from.state = Tail;
        to.state = Live;
        t.live = 1 - t.live.load();
        ++changesMade;
    }
    else
    {
        ++missedChanges;
        if (to.state.load() == Ready) to.state = Idle; // loaded for a change that has passed
    }
    t.nextChange = next + 1;
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
            for (auto& s : t.slots)
            {
                s.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                s.midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
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

        // A part's sound changes at its bar: everything before it plays on the old sound.
        auto changesDue = [&](int track, int64_t uptoSample) {
            auto& t = tracks_[track];
            while (t.nextChange.load() < t.changes.size())
            {
                const int64_t s = sampleOf(int64_t(t.changes[t.nextChange.load()].bar) * kBarTicks);
                if (s > uptoSample) break;
                changeSound(track, int(juce::jlimit<int64_t>(0, len - 1, s - pos_)));
            }
        };

        for (; head_ < events_.size(); ++head_)
        {
            const auto& e = events_[head_];
            const int64_t s = sampleOf(e.tick);
            if (s >= end) break;
            const int at = int(juce::jlimit<int64_t>(0, len - 1, s - pos_));
            auto& t = tracks_[e.track];
            changesDue(e.track, s);
            auto& live = t.slots[t.live.load()];
            switch (e.type)
            {
                case Event::NoteOn:  live.midi.addEvent(juce::MidiMessage::noteOn(1, e.pitch, e.value), at); t.held[e.pitch] = e.value; break;
                case Event::NoteOff: live.midi.addEvent(juce::MidiMessage::noteOff(1, e.pitch), at); t.held[e.pitch] = 0; break;
                case Event::Level:   t.gain.setTargetValue(e.value); break;
                case Event::Filter:  t.filterTarget = e.value; break;
            }
        }
        for (int i = 0; i < kNumTracks; ++i) changesDue(i, end - 1);

        if (head_ > 8192) // keep the queue's memory; just drop what has played
        {
            events_.erase(events_.begin(), events_.begin() + long(head_));
            head_ = 0;
        }
        bar = int(juce::jlimit<int64_t>(0, composer_->totalBars() - 1, int64_t(double(pos_) / samplesPerTick_) / kBarTicks));
    }

    float* pa[2] = { out[0] + offset, (numChannels > 1 ? out[1] : out[0]) + offset };
    juce::AudioBuffer<float> delayIn(delayBus_.getArrayOfWritePointers(), delayBus_.getNumChannels(), len);
    juce::AudioBuffer<float> reverbIn(reverbBus_.getArrayOfWritePointers(), reverbBus_.getNumChannels(), len);
    delayIn.clear();
    reverbIn.clear();
    auto runFx = [&](juce::AudioPluginInstance& fx, juce::AudioBuffer<float>& buf) {
        for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, len); // a side-chain input hears nothing
        fx.processBlock(buf, noMidi_);
        noMidi_.clear();
    };

    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = tracks_[i];
        // Glide the filter towards its target over about a quarter second.
        t.filterNow += (t.filterTarget - t.filterNow) * juce::jmin(1.0f, float(len / (0.25 * sampleRate_)));
        for (int n = 0; n < len; ++n) gains_[size_t(n)] = t.gain.getNextValue();
        if (!(enabledTracks & (1 << i)))
        {
            for (auto& slot : t.slots)
            {
                slot.midi.clear();
                if (slot.state.load() == Tail) slot.state = Idle;
            }
            continue;
        }

        float peak = 0;
        double sq = 0;
        for (auto& slot : t.slots)
        {
            const int state = slot.state.load();
            if (state != Live && state != Tail) { slot.midi.clear(); continue; }
            if (state == Tail && slot.releaseAt >= 0 && slot.releaseAt < pos_ + len)
            {
                const int at = int(juce::jlimit<int64_t>(0, len - 1, slot.releaseAt - pos_));
                for (int p = 0; p < 128; ++p)
                    if (slot.releasing[p]) { slot.midi.addEvent(juce::MidiMessage::noteOff(1, p), at); slot.releasing[p] = false; }
                slot.releaseAt = -1;
                slot.quiet = 0;
            }
            if (slot.cutoff)
            {
                float v = juce::jlimit(0.0f, 1.0f, slot.cutoffBase + 0.2f * t.filterNow);
                if (std::abs(v - slot.cutoff->getValue()) > 1e-4f) slot.cutoff->setValue(v);
            }
            t.buffer.clear();
            juce::AudioBuffer<float> view(t.buffer.getArrayOfWritePointers(), t.buffer.getNumChannels(), len);
            slot.plugin->processBlock(view, slot.midi);
            slot.midi.clear();

            float slotPeak = 0;
            for (int c = 0; c < 2; ++c)
            {
                const float* src = view.getReadPointer(juce::jmin(c, view.getNumChannels() - 1));
                for (int n = 0; n < len; ++n)
                {
                    float x = src[n] * gains_[size_t(n)] * slot.gain;
                    pa[c][n] += x;
                    delayIn.getWritePointer(c)[n] += x * kSends[i][0];
                    reverbIn.getWritePointer(c)[n] += x * kSends[i][1];
                    slotPeak = juce::jmax(slotPeak, std::abs(src[n]));
                    sq += double(x) * x;
                }
            }
            peak = juce::jmax(peak, slotPeak * slot.gain);
            // A sound that has rung out for two seconds frees its instance for the next one;
            // one that never quite falls silent (a noise layer) is cut after 20 seconds.
            if (state == Tail)
            {
                slot.quiet = slotPeak < 1e-5f && slot.releaseAt < 0 ? slot.quiet + len : slot.quiet;
                slot.tailed += len;
                if (slot.quiet > int64_t(2 * sampleRate_) || slot.tailed > int64_t(20 * sampleRate_))
                {
                    slot.midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
                    slot.state = Idle;
                }
            }
        }
        meters[i].peak = juce::jmax(meters[i].peak.load(), peak);
        meters[i].sumSquares = meters[i].sumSquares.load() + sq;
        meters[i].samples = meters[i].samples.load() + 2 * len;
    }

    // The shared buses: the delay returns to the mix and also feeds the reverb.
    auto meter = [len](Meter& m, const juce::AudioBuffer<float>& b) {
        float peak = 0;
        double sq = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < len; ++n) { float x = b.getSample(c, n); peak = juce::jmax(peak, std::abs(x)); sq += double(x) * x; }
        m.peak = juce::jmax(m.peak.load(), peak);
        m.sumSquares = m.sumSquares.load() + sq;
        m.samples = m.samples.load() + 2 * len;
    };
    if (effects && delay_ && reverb_)
    {
        runFx(*delay_, delayIn);
        for (int c = 0; c < 2; ++c)
        {
            juce::FloatVectorOperations::add(pa[c], delayIn.getReadPointer(c), len);
            juce::FloatVectorOperations::addWithMultiply(reverbIn.getWritePointer(c), delayIn.getReadPointer(c), kDelayToReverb, len);
        }
        meter(delayMeter, delayIn);
        runFx(*reverb_, reverbIn);
        meter(reverbMeter, reverbIn);
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::add(pa[c], reverbIn.getReadPointer(c), len);
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
    if (playing && nextBar_ >= composer_->totalBars() && head_ >= events_.size() && pos_ > endSample_ + int64_t(12 * sampleRate_)) // the reverb and delay tails
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
