#include "Engine.h"
#include "Improv.h"
#include <sfizz.hpp>
#include "Break.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace liszt {

namespace {

juce::File firstExisting(std::initializer_list<juce::String> paths)
{
    for (auto& p : paths)
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

// "Category/Name" from the factory patches; "3rd/Author/Category/Name" from the third-party ones.
juce::File findSurgePatch(const juce::String& categoryAndName)
{
    const bool third = categoryAndName.startsWith("3rd/");
    const juce::String set = third ? "patches_3rdparty" : "patches_factory";
    auto root = firstExisting({ "C:/Program Files/Common Files/VST3/Surge Synth Team/SurgeXTData/" + set,
                                "C:/ProgramData/Surge XT/" + set,
                                "/Library/Application Support/Surge XT/" + set });
    const auto rel = set + "/" + (third ? categoryAndName.substring(4) : categoryAndName) + ".fxp";
    if (auto f = root.getChildFile(rel.fromFirstOccurrenceOf("/", false, false)); f.existsAsFile()) return f;
    // the ones this Surge release does not ship, carried with the songs (sounds/surge-patches; Kerem's Mac lacked 15)
    if (auto f = findPalette("berlin").getParentDirectory().getChildFile("surge-patches").getChildFile(rel); f.existsAsFile()) return f;
    return root.getChildFile(rel.fromFirstOccurrenceOf("/", false, false));
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

// Dexed (a DX7): in the standard VST3 folders, or in a plugins/ folder next to the executable or any
// parent of it (the repo in development; Resources/plugins inside the Mac .app bundle).
juce::File findDexed()
{
    auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int up = 0; up < 8 && dir.exists(); ++up, dir = dir.getParentDirectory())
        for (auto sub : { "plugins", "Resources/plugins" })
            if (auto f = dir.getChildFile(sub).getChildFile("Dexed.vst3"); f.exists()) return f;
    return firstExisting({ "C:/Users/kerem/OneDrive/Desktop/Claude/Projects/Lisztmachine/plugins/Dexed.vst3", // the repo, from the build dir
                           "C:/Program Files/Common Files/VST3/Dexed.vst3", "/Library/Audio/Plug-Ins/VST3/Dexed.vst3",
                           juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName() + "/Library/Audio/Plug-Ins/VST3/Dexed.vst3" });
}

bool isDexedSound(const juce::String& patch) { return patch.startsWith("dexed/"); }

std::unique_ptr<juce::AudioPluginInstance> createDexed(double sampleRate, int maxBlock, juce::String& error)
{
    auto file = findDexed();
    if (!file.exists()) { error = "Dexed is not installed (no Dexed.vst3 found)"; return nullptr; }
    return createVst3(file, sampleRate, maxBlock, error);
}

// "dexed/<program name>": one of the voices of Dexed's cartridge, by name.
juce::String loadDexedVoice(juce::AudioPluginInstance& dexed, const juce::String& patch)
{
    const auto name = patch.fromFirstOccurrenceOf("dexed/", false, false);
    for (int i = 0; i < dexed.getNumPrograms(); ++i)
        if (dexed.getProgramName(i).trim().equalsIgnoreCase(name)) { dexed.setCurrentProgram(i); return {}; }
    juce::StringArray names;
    for (int i = 0; i < dexed.getNumPrograms(); ++i) names.add(dexed.getProgramName(i).trim());
    return "no Dexed voice '" + name + "' (has: " + names.joinIntoString(", ") + ")";
}

bool isVstSound(const juce::String& patch) { return patch.startsWith("vst/"); }

juce::File findVst(const juce::String& patch)
{
    const auto name = patch.fromFirstOccurrenceOf("vst/", false, false).upToFirstOccurrenceOf("/", false, false) + ".vst3";
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    for (auto dir : { home.getChildFile("liszt-deps/synths/vst3"), home.getChildFile("Music/Lisztmachine/plugins"),
                      juce::File("C:/Program Files/Common Files/VST3"), juce::File("/Library/Audio/Plug-Ins/VST3"),
                      home.getChildFile("Library/Audio/Plug-Ins/VST3") })
    {
        if (auto f = dir.getChildFile(name); f.exists()) return f;
        for (const auto& sub : dir.findChildFiles(juce::File::findDirectories, false)) // a vendor's folder (u-he, Surge Synth Team)
            if (auto f = sub.getChildFile(name); f.exists()) return f;
    }
    return {};
}

std::unique_ptr<juce::AudioPluginInstance> createVstSound(const juce::String& patch, double sampleRate, int maxBlock, juce::String& error)
{
    const auto file = findVst(patch);
    if (!file.exists()) { error = "no plugin for '" + patch + "'"; return nullptr; }
    auto p = createVst3(file, sampleRate, maxBlock, error);
    if (!p) return nullptr;
    juce::StringArray parts;
    parts.addTokens(patch.fromFirstOccurrenceOf("vst/", false, false).fromFirstOccurrenceOf("/", false, false), "|", "");
    const auto prog = parts.isEmpty() ? juce::String() : parts[0].trim();
    if (prog.startsWith("#")) p->setCurrentProgram(prog.substring(1).getIntValue());
    else if (prog.isNotEmpty())
    {
        int found = -1;
        for (int i = 0; i < p->getNumPrograms() && found < 0; ++i)
            if (p->getProgramName(i).trim().equalsIgnoreCase(prog)) found = i;
        if (found < 0) { error = "no program '" + prog + "' in " + file.getFileName(); return nullptr; }
        p->setCurrentProgram(found);
    }
    for (int k = 1; k < parts.size(); ++k)
    {
        const auto name = parts[k].upToFirstOccurrenceOf("=", false, false).trim(), value = parts[k].fromFirstOccurrenceOf("=", false, false).trim();
        juce::AudioProcessorParameter* q = nullptr;
        for (auto* c : p->getParameters()) if (c->getName(128).trim().equalsIgnoreCase(name)) { q = c; break; }
        if (!q) { error = "no parameter '" + name + "' in " + file.getFileName(); return nullptr; }
        if (value.containsOnly("0123456789.")) q->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value.getFloatValue()));
        else if (auto e = setParameterText(*p, q->getName(128), value); e.isNotEmpty()) { error = e; return nullptr; }
    }
    p->enableAllBuses();
    // Two seconds of silence before the first note: JS80P drops the notes of its first 1.4 s
    p->prepareToPlay(sampleRate, maxBlock);
    juce::AudioBuffer<float> warm(juce::jmax(2, p->getTotalNumOutputChannels()), maxBlock);
    juce::MidiBuffer none;
    for (int i = 0; i < int(2.0 * sampleRate / maxBlock); ++i) { warm.clear(); p->processBlock(warm, none); }
    return p;
}

bool isSfzSound(const juce::String& patch) { return patch.startsWith("sfz/"); }

juce::File findSfz(const juce::String& patch)
{
    const auto rel = patch.fromFirstOccurrenceOf("sfz/", false, false);
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    for (auto root : { home.getChildFile("liszt-instruments/lib"), home.getChildFile("liszt-instruments"),
                       home.getChildFile("Music/Lisztmachine/instruments") })
        if (auto f = root.getChildFile(rel); f.existsAsFile()) return f;
    return home.getChildFile("liszt-instruments/lib").getChildFile(rel);
}

static std::atomic<bool> offlineRendering { false };

// The track pool: a few realtime threads that render tracks beside the audio thread. The audio thread hands out the
// block's tracks (the heaviest first), takes some itself, and waits for the rest; nothing allocates or locks on it.
namespace {
class TrackPool
{
public:
    static TrackPool& get() { static TrackPool p; return p; }
    void setWorkgroup(juce::AudioWorkgroup w)
    {
        const juce::SpinLock::ScopedLockType sl(wgLock_);
        wg_ = std::move(w);
        ++wgGen_;
    }
    // Audio thread: job(ctx, order[k]) for k in [0, n), on every thread; returns when all are done.
    void run(int n, const int* order, void (*job)(void*, int), void* ctx)
    {
        start();
        if (workers_.empty() || n < 2) { for (int k = 0; k < n; ++k) job(ctx, order[k]); return; }
        job_ = job; ctx_ = ctx; order_ = order; n_ = n;
        done_.store(0);
        next_.store(0);
        gen_.fetch_add(1);
        gen_.notify_all();
        drain();
        while (done_.load() < n) juce::Thread::yield(); // only the tracks another thread has started are left
    }
    ~TrackPool()
    {
        stop_ = true;
        gen_.fetch_add(1);
        gen_.notify_all();
        for (auto& w : workers_) w->stopThread(2000);
    }

private:
    struct Worker : juce::Thread
    {
        TrackPool& pool;
        Worker(TrackPool& p, int k) : juce::Thread("liszt tracks " + juce::String(k)), pool(p) {}
        void run() override
        {
            juce::WorkgroupToken token;
            int joined = -1;
            unsigned seen = pool.gen_.load();
            while (!pool.stop_)
            {
                pool.gen_.wait(seen);
                seen = pool.gen_.load();
                if (pool.stop_) break;
                if (const int g = pool.wgGen_.load(); g != joined)
                {
                    const juce::SpinLock::ScopedLockType sl(pool.wgLock_);
                    token.reset();
                    if (pool.wg_) pool.wg_.join(token);
                    joined = g;
                }
                pool.drain();
            }
        }
    };
    void start()
    {
        if (started_) return;
        started_ = true; // once, on the first block
        const int helpers = juce::jlimit(0, 3, juce::SystemStats::getNumPhysicalCpus() - 1);
        for (int k = 0; k < helpers; ++k)
        {
            auto w = std::make_unique<Worker>(*this, k);
            if (!w->startRealtimeThread(juce::Thread::RealtimeOptions {}.withPriority(10).withApproximateAudioProcessingTime(256, 48000.0)))
                w->startThread(juce::Thread::Priority::highest);
            workers_.push_back(std::move(w));
        }
    }
    void drain()
    {
        for (int k; (k = next_.fetch_add(1)) < n_.load();)
        {
            job_.load()(ctx_.load(), order_.load()[k]);
            done_.fetch_add(1);
        }
    }
    std::vector<std::unique_ptr<Worker>> workers_;
    bool started_ = false;
    std::atomic<bool> stop_ { false };
    std::atomic<unsigned> gen_ { 0 };
    std::atomic<int> next_ { 1 << 30 }, done_ { 0 }, n_ { 0 }, wgGen_ { 0 };
    std::atomic<void (*)(void*, int)> job_ { nullptr };
    std::atomic<void*> ctx_ { nullptr };
    std::atomic<const int*> order_ { nullptr };
    juce::SpinLock wgLock_;
    juce::AudioWorkgroup wg_;
};
} // namespace
void Engine::setAudioWorkgroup(juce::AudioWorkgroup workgroup) { TrackPool::get().setWorkgroup(std::move(workgroup)); }
void setOfflineRendering(bool offline) { offlineRendering = offline; }

namespace {
struct ExpressionParameter : juce::AudioPluginInstance::HostedParameter
{
    std::atomic<float> value { 0.7f };
    float getValue() const override { return value; }
    void setValue(float v) override { value = v; }
    float getDefaultValue() const override { return 0.7f; }
    juce::String getName(int n) const override { return juce::String("Expression").substring(0, n); }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& t) const override { return t.getFloatValue(); }
    juce::String getParameterID() const override { return "expression"; }
};

// sfizz behind the plugin interface, so the engine's slots, crossfades and sound changes treat it
// like any other instrument.
class SfzInstrument : public juce::AudioPluginInstance
{
public:
    SfzInstrument() : juce::AudioPluginInstance(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true))
    {
        auto p = std::make_unique<ExpressionParameter>();
        expression_ = p.get();
        addHostedParameter(std::move(p));
    }
    bool load(const juce::File& f)
    {
        synth_.setNumVoices(48);
        if (!synth_.loadSfzFile(f.getFullPathName().toStdString())) return false;
        name_ = f.getFileNameWithoutExtension();
        return synth_.getNumRegions() > 0;
    }
    const juce::String getName() const override { return "sfizz " + name_; }
    void prepareToPlay(double sr, int block) override
    {
        synth_.setSampleRate(float(sr));
        synth_.setSamplesPerBlock(block);
        if (offlineRendering) synth_.enableFreeWheeling();
    }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        const int cc = juce::jlimit(0, 127, int(expression_->getValue() * 127));
        if (cc != lastCc_) { synth_.cc(0, 1, cc); synth_.cc(0, 11, cc); lastCc_ = cc; }
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int at = juce::jlimit(0, buf.getNumSamples() - 1, meta.samplePosition);
            if (m.isNoteOn()) synth_.noteOn(at, m.getNoteNumber(), juce::jmax(1, int(m.getVelocity())));
            else if (m.isNoteOff()) synth_.noteOff(at, m.getNoteNumber(), 0);
            else if (m.isAllNotesOff() || m.isAllSoundOff()) synth_.allSoundOff();
            else if (m.isController()) synth_.cc(at, m.getControllerNumber(), m.getControllerValue());
        }
        float* out[2] = { buf.getWritePointer(0), buf.getWritePointer(juce::jmin(1, buf.getNumChannels() - 1)) };
        synth_.renderBlock(out, size_t(buf.getNumSamples()), 1);
        for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, buf.getNumSamples());
    }
    void reset() override { synth_.allSoundOff(); }
    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return name_; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "sfizz"; d.isInstrument = true; }

private:
    sfz::Sfizz synth_;
    ExpressionParameter* expression_ = nullptr;
    juce::String name_;
    int lastCc_ = -1;
};
// Breaks (Kerem, 2026-11-21, Bleach: "use these for breaks ... the design of that amen dnb box"): BreakMachine's player
// (Break.h) as an instrument. A `.breaks` file lists recorded breaks (one WAV path a line, beside it); each is sliced at
// its onsets and sorted into kick, snare and hat, and the player chops them into two-bar patterns, phrased A A' A FILL
// over eight bars. The composer drives it with one note a beat: pitch 36 + the beat's place in the eight-bar phrase
// (0-31), or 68 + it for a new pattern (a new part); velocity = how wild (chaos, density and rolls grow with it), and
// the lowest velocity = silent. Between the notes the position runs on at the tempo the notes keep.
class BreakInstrument : public juce::AudioPluginInstance
{
public:
    BreakInstrument() : juce::AudioPluginInstance(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true)) {}
    bool load(const juce::File& list)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        juce::StringArray lines;
        list.readLines(lines);
        std::vector<brk::Break> parts;
        for (auto line : lines)
        {
            line = line.upToFirstOccurrenceOf("#", false, false).trim();
            if (line.isEmpty()) continue;
            std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(list.getSiblingFile(line)));
            if (!r || r->lengthInSamples < 1000) continue;
            juce::AudioBuffer<float> b(2, int(r->lengthInSamples));
            r->read(&b, 0, int(r->lengthInSamples), 0, true, true);
            brk::Break k;
            k.sr = r->sampleRate;
            k.l.assign(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
            k.r.assign(b.getReadPointer(1), b.getReadPointer(1) + b.getNumSamples());
            brk::analyse(k, 0.5f);
            if (!k.slices.empty()) parts.push_back(std::move(k));
        }
        if (parts.empty()) return false;
        std::vector<const brk::Break*> ptrs;
        for (auto& k : parts) ptrs.push_back(&k);
        kit_ = brk::combine(ptrs);
        name_ = list.getFileNameWithoutExtension();
        return true;
    }
    const juce::String getName() const override { return "breaks " + name_; }
    void prepareToPlay(double sr, int) override { sr_ = sr; player_.prepare(sr); player_.setBreak(&kit_); player_.seed(1); player_.evolve = 1;
        player_.gainTarget = .4f; } // under the player's own limiter, which flattened the hits (the track gain makes it up)
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        const int n = buf.getNumSamples();
        float* L = buf.getWritePointer(0);
        float* R = buf.getWritePointer(juce::jmin(1, buf.getNumChannels() - 1));
        int done = 0;
        auto run = [&](int upTo) { // the player from `done` to `upTo`, at the beat position the notes keep
            if (upTo <= done) return;
            const bool playing = loud_ && anchor_ >= 0 && double(clock_ + done - anchor_) < 1.5 * quarter_;
            const double real = anchorPpq_ + double(clock_ + done - anchor_) / quarter_;
            // a dive: over its bar the speed falls from 1 to a quarter, the pattern and the hits slowing together
            player_.speed = diveBeat_ >= 0 ? std::max(0.25, 1.0 - 0.75 * (real - diveFrom_) / 4.0) : 1.0;
            const double ppq = diveBeat_ >= 0 ? playPpq_ : real;
            player_.render(L + done, R + done, upTo - done, ppq, player_.speed / quarter_, playing);
            playPpq_ = ppq + double(upTo - done) * player_.speed / quarter_;
            done = upTo;
        };
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isAllNotesOff() || m.isAllSoundOff()) { loud_ = false; continue; }
            if (!m.isNoteOn()) continue;
            const int at = juce::jlimit(0, n, meta.samplePosition);
            run(at);
            const int note = m.getNoteNumber();
            if (note < 36 || note > 103) continue;
            const bool dive = note >= 100; // a tape stop: this beat (0-3 of the bar) slows the break toward a stop
            const int pos = dive ? (lastPos_ + 1) % 32 : (note - 36) % 32;
            diveBeat_ = dive ? note - 100 : -1;
            const int64_t now = clock_ + at;
            if (anchor_ >= 0)
            {
                const int beats = (pos - lastPos_ + 32) % 32;
                if (beats >= 1 && beats <= 4) quarter_ = double(now - anchor_) / beats; // the tempo the notes keep
                if (pos < lastPos_) phrase_ += 1;
            }
            anchor_ = now, lastPos_ = pos, anchorPpq_ = phrase_ * 32.0 + pos;
            if (diveBeat_ == 0) diveFrom_ = playPpq_ = anchorPpq_;
            const float v = float(m.getVelocity()) / 127.0f;
            loud_ = m.getVelocity() > 1;
            player_.gen.chaos = v, player_.gen.density = .45f + .45f * v, player_.gen.rolls = .06f + .14f * v; // Kerem: not too many rolls (was .25 + .55 v)
            player_.gen.reverse = .15f, player_.gen.pitch = .1f;
            if (note >= 68) player_.requestNew();
        }
        run(n);
        clock_ += n;
        for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, n);
    }
    void reset() override { loud_ = false; }
    double getTailLengthSeconds() const override { return 0.5; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return name_; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "breaks"; d.isInstrument = true; }

private:
    brk::Break kit_;
    brk::Player player_;
    juce::String name_;
    double sr_ = 48000, quarter_ = 48000 * 60.0 / 170, anchorPpq_ = 0;
    int64_t clock_ = 0, anchor_ = -1;
    int lastPos_ = 0, diveBeat_ = -1;
    int64_t phrase_ = 0;
    double diveFrom_ = 0, playPpq_ = 0;
    bool loud_ = false;
};
} // namespace

std::unique_ptr<juce::AudioPluginInstance> createSfz(double sampleRate, int maxBlock, const juce::File& sfz, juce::String& error)
{
    if (sfz.hasFileExtension(".breaks"))
    {
        auto b = std::make_unique<BreakInstrument>();
        if (!sfz.existsAsFile() || !b->load(sfz)) { error = "no breaks in " + sfz.getFullPathName(); return nullptr; }
        b->prepareToPlay(sampleRate, maxBlock);
        return b;
    }
    auto p = std::make_unique<SfzInstrument>();
    if (!sfz.existsAsFile()) { error = "no sfz file " + sfz.getFullPathName(); return nullptr; }
    if (!p->load(sfz)) { error = "sfizz could not load " + sfz.getFullPathName(); return nullptr; }
    p->prepareToPlay(sampleRate, maxBlock);
    return p;
}

// Two read heads sweep a 60 ms window at twice the writing speed (an octave up), each faded in and
// out by a Hann window half a sweep apart, so their jumps back cancel; then a 5 kHz pole.
float Engine::OctaveUp::process(float x)
{
    const int size = int(buf.size());
    buf[size_t(at)] = x;
    float y = 0;
    for (float off : { 0.0f, 0.5f })
    {
        float p = phase + off;
        if (p >= 1) p -= 1;
        float pos = float(at) - (1 - p) * float(n);
        if (pos < 0) pos += float(size);
        const int i = int(pos) % size; // pos can round up to size (at 0, the head about to jump): that read past the end once blew up the reverb
        const float f = pos - float(i);
        const float s = buf[size_t(i)] + f * (buf[size_t((i + 1) % size)] - buf[size_t(i)]);
        const float w = std::sin(juce::MathConstants<float>::pi * p);
        y += s * w * w;
    }
    at = (at + 1) % size;
    phase += 1.0f / float(n);
    if (phase >= 1) phase -= 1;
    lp += (y - lp) * 0.48f;
    return lp;
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
    // in one unit: Surge on Kerem's Mac shows the reverb's pre-delay in seconds ("0.040 s") where the PC's showed
    // "40 ms"; compared as bare numbers, "40 ms" found "2.000 s" (2026-11-14)
    auto num = [](const juce::String& s) {
        const double x = s.retainCharacters("-0123456789.").getDoubleValue();
        const auto unit = s.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").toLowerCase();
        if (unit.startsWith("ms")) return x / 1000.0;
        if (unit.startsWith("khz")) return x * 1000.0;
        return x;
    };
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

void Biquad::lowpass(double sr, double hz, double q)
{
    const double w = juce::MathConstants<double>::twoPi * hz / sr, c = std::cos(w), alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
    b0 = float((1 - c) / 2 / a0);
    b1 = float((1 - c) / a0);
    b2 = b0;
    a1 = float(-2 * c / a0);
    a2 = float((1 - alpha) / a0);
    z1 = z2 = 0;
}

void Biquad::peak(double sr, double hz, double q, double dB)
{
    const double A = std::pow(10.0, dB / 40), w = juce::MathConstants<double>::twoPi * hz / sr, c = std::cos(w), alpha = std::sin(w) / (2 * q);
    const double a0 = 1 + alpha / A;
    b0 = float((1 + alpha * A) / a0);
    b1 = float(-2 * c / a0);
    b2 = float((1 - alpha * A) / a0);
    a1 = b1;
    a2 = float((1 - alpha / A) / a0);
    z1 = z2 = 0;
}

void Amp::prepare(double sr, const AmpSpec& s)
{
    spec_ = s;
    g1_ = 2 + 22 * s.gain * s.gain;
    g2_ = 1.5f + 5 * s.gain;
    for (int c = 0; c < 2; ++c)
    {
        tight_[c].highpass(sr, s.tightHz);
        push_[c].peak(sr, 850, 0.8, s.pushDb);
        inter_[c].highpass(2 * sr, 45);           // between the stages, at the doubled rate
        cab_[c][0].highpass(sr, 75);
        cab_[c][1].peak(sr, 110, 1.2, s.thumpDb); // the cabinet's thump
        cab_[c][2].peak(sr, s.scoopHz, 0.9, -5);  // the scoop
        cab_[c][3].peak(sr, s.presenceHz, 1.0, 4); // presence
        cab_[c][4].lowpass(sr, s.cabHz, 0.9);
        last_[c] = low_[c] = lp_[c] = 0;
    }
    lpA_ = 1 - std::exp(-juce::MathConstants<float>::twoPi * 6500.0f / float(2 * sr)); // the speaker's second pole, at the doubled rate
    lowA_ = 1 - std::exp(-juce::MathConstants<float>::twoPi * 140.0f / float(sr));
    envA_ = 1 - std::exp(-1.0f / float(0.3 * sr));
    envIn_ = envOut_ = 1e-6f;
}

float Amp::stage(float x, int c)
{
    x = std::tanh(g1_ * x);
    x = inter_[c].process(x);
    x = std::tanh(g2_ * x + 0.25f) - std::tanh(0.25f);
    lp_[c] += (x - lp_[c]) * lpA_;
    return lp_[c];
}

void Amp::process(float& l, float& r)
{
    float io[2] = { l, r }, wet[2];
    for (int c = 0; c < 2; ++c)
    {
        const float dry = io[c];
        low_[c] += (dry - low_[c]) * lowA_;
        const float x = push_[c].process(tight_[c].process(dry));
        // twice the rate: the midpoint and the sample, averaged back down
        float y = 0.5f * (stage(0.5f * (last_[c] + x), c) + stage(x, c));
        last_[c] = x;
        for (auto& f : cab_[c]) y = f.process(y);
        wet[c] = y;
    }
    // the amp follows the dry track's level (slowly, so it still compresses like an amp does)
    envIn_ += (0.5f * (l * l + r * r) - envIn_) * envA_;
    envOut_ += (0.5f * (wet[0] * wet[0] + wet[1] * wet[1]) - envOut_) * envA_;
    const float match = juce::jmin(8.0f, std::sqrt((envIn_ + 1e-9f) / (envOut_ + 1e-9f)));
    for (int c = 0; c < 2; ++c)
        io[c] = io[c] * (1 - spec_.mix) + (wet[c] * match + 0.8f * low_[c]) * spec_.mix;
    l = io[0];
    r = io[1];
}

void Biquad::highpass(double sr, double hz, double q)
{
    const double w = juce::MathConstants<double>::twoPi * hz / sr, c = std::cos(w), alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
    b0 = float((1 + c) / 2 / a0);
    b1 = float(-(1 + c) / a0);
    b2 = b0;
    a1 = float(-2 * c / a0);
    a2 = float((1 - alpha) / a0);
    z1 = z2 = 0;
}

void Biquad::shelf(double sr, double hz, double dB, bool high)
{
    const double A = std::pow(10.0, dB / 40), w = juce::MathConstants<double>::twoPi * hz / sr, c = std::cos(w);
    const double alpha = std::sin(w) / 2 * std::sqrt(2.0), k = 2 * std::sqrt(A) * alpha, s = high ? 1 : -1;
    const double a0 = (A + 1) - s * (A - 1) * c + k;
    b0 = float(A * ((A + 1) + s * (A - 1) * c + k) / a0);
    b1 = float(-2 * s * A * ((A - 1) + s * (A + 1) * c) / a0);
    b2 = float(A * ((A + 1) + s * (A - 1) * c - k) / a0);
    a1 = float(2 * s * ((A - 1) - s * (A + 1) * c) / a0);
    a2 = float(((A + 1) - s * (A - 1) * c - k) / a0);
    z1 = z2 = 0;
}

void Drive::prepare(double sr, const DriveSpec& spec)
{
    k_ = 1 + 14 * spec.amount * spec.amount;
    comp_ = 1 / std::sqrt(k_); // a mid-level note comes out about as loud as it went in
    lpA_ = 1 - std::exp(-juce::MathConstants<float>::twoPi * spec.toneHz / float(sr));
    for (int c = 0; c < 2; ++c) last_[c] = lp_[c] = dcIn_[c] = dcOut_[c] = 0;
}

float Drive::process(float x, int c)
{
    // twice the rate: the shaper sees the midpoint as well, the two are averaged back down
    const float y = 0.5f * (shape(0.5f * (last_[c] + x)) + shape(x)) * comp_;
    last_[c] = x;
    lp_[c] += (y - lp_[c]) * lpA_;
    const float out = lp_[c] - dcIn_[c] + 0.9995f * dcOut_[c];
    dcIn_[c] = lp_[c];
    dcOut_[c] = out;
    return out;
}

void Compressor::prepare(double sr, const CompSpec& s)
{
    spec_ = s;
    attack_ = 1.0f - std::exp(-1.0f / float(s.attackMs * 0.001 * sr));
    release_ = 1.0f - std::exp(-1.0f / float(s.releaseMs * 0.001 * sr));
    // the squashed copy brought back to about the level it had: half the reduction at the threshold, given back
    makeup_ = juce::Decibels::decibelsToGain(-s.thresholdDb * (1 - 1 / std::max(1.0f, s.ratio)) * 0.5f);
    env_ = 0;
}

void Compressor::process(float& l, float& r)
{
    const float peak = juce::jmax(std::abs(l), std::abs(r));
    env_ += (peak - env_) * (peak > env_ ? attack_ : release_);
    const float over = juce::Decibels::gainToDecibels(env_, -120.0f) - spec_.thresholdDb;
    const float cut = over > 0 ? over * (1 - 1 / spec_.ratio) : 0;
    maxReductionDb = juce::jmin(maxReductionDb, -cut);
    const float g = juce::Decibels::decibelsToGain(-cut) * makeup_;
    l += (l * g - l) * spec_.mix;
    r += (r * g - r) * spec_.mix;
}

void GlueBus::prepare(double sr)
{
    attack_ = 1.0f - std::exp(-1.0f / float(0.010 * sr));
    release_ = 1.0f - std::exp(-1.0f / float(0.150 * sr));
    reset();
}

void GlueBus::process(float* l, float* r, int n)
{
    constexpr float knee = 6, ceiling = 0.89f; // -1 dBFS
    const float makeup = juce::Decibels::decibelsToGain(makeupDb);
    for (int i = 0; i < n; ++i)
    {
        const float peak = juce::jmax(std::abs(l[i]), std::abs(r[i]));
        env_ += (peak - env_) * (peak > env_ ? attack_ : release_);
        const float over = juce::Decibels::gainToDecibels(env_, -120.0f) - thresholdDb;
        const float cut = over <= -knee / 2 ? 0
                        : over >= knee / 2   ? over * (1 - 1 / ratio)
                                             : (1 - 1 / ratio) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
        maxReductionDb = juce::jmin(maxReductionDb, -cut);
        const float g = juce::Decibels::decibelsToGain(-cut) * makeup;
        l[i] = ceiling * std::tanh(l[i] * g / ceiling);
        r[i] = ceiling * std::tanh(r[i] * g / ceiling);
    }
}

// What a mixing engineer does to each kind of track: how deep the kick ducks it, how wide it
// sits, where it is panned and where its low end is cut. Kick, bass and sub stay mono and full.
namespace {
struct TrackMix { float duck, width, pan, lowCutHz; };
TrackMix mixFor(const std::string& name)
{
    static const std::pair<const char*, TrackMix> table[] = {
        { "kick", { 0, 0, 0, 0 } },        { "sub", { .65f, 0, 0, 0 } },      { "bass", { .5f, 0, 0, 30 } },
        { "pad", { .55f, 1.5f, 0, 150 } }, { "drone", { .5f, 1.5f, 0, 120 } }, { "chord", { .45f, 1.4f, 0, 120 } },
        { "stab", { .45f, 1.4f, 0, 180 } }, { "arp", { .3f, 1.2f, 0, 150 } },  { "acid", { .25f, 1.0f, 0, 90 } },
        { "seq", { .2f, 1.2f, 0, 120 } },  { "lead", { .2f, 1.1f, 0, 150 } },  { "snare", { 0, 1.0f, 0, 120 } },
        { "clap", { 0, 1.2f, 0, 150 } },   { "pulse", { 0, 0, 0, 0 } },       { "metal", { .15f, 1.3f, -.15f, 200 } },
        { "tick", { .1f, 1.0f, .25f, 400 } },   { "hats", { 0, 1.0f, .25f, 300 } }, { "perc", { .1f, 1.3f, -.2f, 200 } },
        { "fx", { .5f, 1.6f, 0, 200 } },   { "harm", { .45f, 1.5f, 0, 180 } },
        { "ground", { .2f, 1.0f, 0, 35 } }, { "body", { .15f, 1.3f, .3f, 90 } }, { "mallet", { .15f, 1.4f, -.3f, 150 } },
        { "texture", { .05f, 1.8f, 0, 200 } }, { "impact", { .1f, 1.6f, 0, 40 } },
        { "riff", { .35f, 1.0f, 0, 60 } }, // the guitar: doubled left and right by its own two takes
    };
    for (auto& [n, m] : table) if (name == n) return m;
    return { 0, 1, 0, 0 }; // anything else: as it is
}
} // namespace

// The drum pad's own voice (input 2) and its gain, dB: the pad's rms about 6 dB under the mix (measured 2026-11-20)
// 2026-11-25: Donna's Glass Hammers with its three effects off (two delays, one feeding back 71 %, and a frequency shifter's
// echo: Kerem's "plays twice"), carried in sounds/surge-patches; the engine's room (padVerb_) instead
static constexpr const char* kPadVoice = "3rd/Lisztmachine/Pad/Glass Hammers Dry";
static constexpr float kPadVoiceDb = 1.0f; // with the octave under it, at 3.0: -3.7 (halftime), -4.4 (doom) dB under the mix
// 2026-11-25 (Kerem: "put its gain low or make its volume adjustable"): every song's pad 6 dB lower than it was
// (the 'drumpad' lines: 5 dB under the summit's mix), the page's Drum pad slider on top of that
static constexpr float kPadTrimDb = -12.0f; // -6 net: without its effects the patch measured 5.5 dB louder (rms -22.2 at -6 vs -22.7 before)
// The engine's own synths: no Surge instance, one sound for the whole song.
static bool isEngineSound(const juce::String& patch) { return patch.startsWith("internal/"); }

juce::String Palette::load(const juce::File& file, const std::string& sceneName)
{
    scene = sceneName;
    info = &sceneInfo(sceneName);
    for (auto& p : parts) p.clear();
    for (auto& g : trackGainDb) g = 0;
    masterDb = 0;
    returnDb[0] = returnDb[1] = returnDb[2] = 0;
    for (auto& f : feeds) f = 0;
    fbBeats = 0.75f;
    fbAmount = 0;
    for (auto& d : drives) d = {};
    for (auto& a : amps) a = {};
    for (auto& c : crushes) c = {};
    for (auto& c : comps) c = {};
    for (auto& d : duckDepth) d = -1;
    for (auto& d : lowCutHz) d = -1;
    for (auto& d : pan) d = -9;
    for (auto& d : delays) d = {};
    for (auto& r : releaseS) r = 0;
    for (auto& g : glideS) g = 0.25f;
    for (auto& w : wobbleHz) w[0] = w[1] = 0;
    for (auto& t : sends) t[0] = t[1] = -1;
    shimmer = 0;
    padPatch = {};
    padGainDb = 0;
    padGainSet = false;
    if (!file.existsAsFile()) return "no palette at " + file.getFullPathName();

    juce::StringArray lines;
    file.readLines(lines);
    for (int n = 0; n < lines.size(); ++n)
    {
        auto line = lines[n].upToFirstOccurrenceOf("#", false, false).trim();
        if (line.isEmpty()) continue;
        auto tokens = juce::StringArray::fromTokens(line, " \t", "");
        tokens.removeEmptyStrings();
        const auto where = file.getFileName() + " line " + juce::String(n + 1) + ": ";
        // "enter <part> <track> gain bright patch" (Kerem, 2026-11-25, Doom: "a new synth introduced when it's finished
        // and new parts come"): the track takes this sound at that part and keeps it to the end
        juce::String from;
        if (tokens[0] == "enter" && tokens.size() >= 6)
        {
            from = tokens[1];
            line = line.fromFirstOccurrenceOf(tokens[1], false, false).trim();
            tokens.remove(0);
            tokens.remove(0);
        }
        if (tokens[0] == "master" && tokens.size() >= 2) { masterDb = tokens[1].getFloatValue(); continue; }
        if (tokens[0] == "drumpad" && tokens.size() >= 2) // "drumpad <dB>": the pad's own voice at this song's level
        {
            // "drumpad off" (Kerem, 2026-11-26, Doom: "shut the drum pad in the last song, but the visuals still affected"):
            // silent, but still struck, so the page still gets its hits
            padGainDb = tokens[1] == "off" ? -200.0f : tokens[1].getFloatValue();
            padGainSet = true;
            padPatch = line.trim().fromFirstOccurrenceOf(" ", false, false).trim().fromFirstOccurrenceOf(" ", false, false).trim();
            continue;
        }
        if (tokens[0] == "shimmer" && tokens.size() >= 2) { shimmer = juce::jlimit(0.0f, 0.9f, tokens[1].getFloatValue()); continue; }
        if (tokens[0] == "send" && tokens.size() >= 4 && (tokens[2] == "delay" || tokens[2] == "reverb"))
        {
            const int t = info->trackOf(tokens[1].toStdString());
            if (t < 0 && tokens[1] != "all") return where + "no track '" + tokens[1] + "' in the " + juce::String(scene) + " scene";
            for (int k = 0; k < info->numTracks; ++k)
                if (k == t || t < 0) sends[k][tokens[2] == "reverb"] = juce::jmax(0.0f, tokens[3].getFloatValue());
            continue;
        }
        if (tokens[0] == "return" && tokens.size() >= 3 && (tokens[1] == "delay" || tokens[1] == "reverb" || tokens[1] == "feedback"))
        { returnDb[tokens[1] == "reverb" ? 1 : tokens[1] == "feedback" ? 2 : 0] = tokens[2].getFloatValue(); continue; }
        if (tokens[0] == "feedback" && tokens.size() >= 3) { fbBeats = tokens[1].getFloatValue(); fbAmount = tokens[2].getFloatValue(); continue; }
        if (tokens[0] == "gain" || tokens[0] == "delay" || tokens[0] == "drive" || tokens[0] == "crush" || tokens[0] == "comp" || tokens[0] == "duck" || tokens[0] == "lowcut" || tokens[0] == "pan" || tokens[0] == "glide" || tokens[0] == "wobble" || tokens[0] == "amp" || tokens[0] == "release" || tokens[0] == "feed")
        {
            const int t = info->trackOf(tokens[1].toStdString());
            if (t < 0) return where + "no track '" + tokens[1] + "' in the " + juce::String(scene) + " scene";
            if (tokens[0] == "gain") trackGainDb[t] = tokens[2].getFloatValue();
            else if (tokens[0] == "crush" && tokens.size() >= 5) crushes[t] = { tokens[2].getFloatValue(), tokens[3].getFloatValue(), juce::jlimit(0.0f, 1.0f, tokens[4].getFloatValue()) };
            else if (tokens[0] == "comp" && tokens.size() >= 7) comps[t] = { tokens[2].getFloatValue(), juce::jmax(1.0f, tokens[3].getFloatValue()), tokens[4].getFloatValue(), tokens[5].getFloatValue(), juce::jlimit(0.0f, 1.0f, tokens[6].getFloatValue()) };
            else if (tokens[0] == "duck") duckDepth[t] = juce::jlimit(0.0f, 1.0f, tokens[2].getFloatValue());
            else if (tokens[0] == "feed") feeds[t] = juce::jmax(0.0f, tokens[2].getFloatValue());
            else if (tokens[0] == "release") releaseS[t] = juce::jmax(0.0f, tokens[2].getFloatValue());
            else if (tokens[0] == "lowcut") lowCutHz[t] = juce::jmax(0.0f, tokens[2].getFloatValue());
            else if (tokens[0] == "wobble" && tokens.size() >= 4) { wobbleHz[t][0] = tokens[2].getFloatValue(); wobbleHz[t][1] = tokens[3].getFloatValue(); }
            else if (tokens[0] == "glide") glideS[t] = juce::jlimit(0.002f, 2.0f, tokens[2].getFloatValue());
            else if (tokens[0] == "pan") pan[t] = juce::jlimit(-1.0f, 1.0f, tokens[2].getFloatValue());
            else if (tokens[0] == "amp" && tokens.size() >= 5) amps[t] = { juce::jlimit(0.0f, 1.0f, tokens[2].getFloatValue()), tokens[3].getFloatValue(), juce::jlimit(0.0f, 1.0f, tokens[4].getFloatValue()),
                                                                     tokens.size() >= 6 ? tokens[5].getFloatValue() : 110.0f, tokens.size() >= 7 ? tokens[6].getFloatValue() : 7.0f,
                                                                     tokens.size() >= 8 ? tokens[7].getFloatValue() : 5200.0f, tokens.size() >= 9 ? tokens[8].getFloatValue() : 3.0f,
                                                                     tokens.size() >= 10 ? tokens[9].getFloatValue() : 450.0f };
            else if (tokens[0] == "drive") drives[t] = { juce::jlimit(0.0f, 1.0f, tokens[2].getFloatValue()), tokens.size() >= 4 ? tokens[3].getFloatValue() : 6000.0f };
            else if (tokens[0] == "delay" && tokens.size() >= 5) delays[t] = { tokens[2].getFloatValue(), juce::jlimit(0.0f, 0.95f, tokens[3].getFloatValue()), tokens[4].getFloatValue(), tokens[5] == "throws" };
            else if (tokens[0] == "delay") return where + "expected 'delay <track> beats feedback mix [throws]'";
            else return where + "too few values";
            continue;
        }
        std::vector<int> targets;
        if (tokens[0] == "voices") { for (int t = 0; t < info->numTracks; ++t) if (t == 0 ? info->group[1] == 0 : info->group[t] == 0) targets.push_back(t); }
        else if (int t = info->trackOf(tokens[0].toStdString()); t >= 0) targets.push_back(t);
        if (targets.empty() || tokens.size() < 4)
            return where + "expected 'part gain bright Category/Patch'";
        // The patch name is the rest of the line: it may contain spaces.
        auto patch = line.fromFirstOccurrenceOf(tokens[1], false, false).fromFirstOccurrenceOf(tokens[2], false, false).trim(); // after the gain: a bright of 0 is also in 0.0
        if (isSfzSound(patch) && !findSfz(patch).existsAsFile()) // not installed here: the track plays its other sounds
        {
            DBG(where + "no sampled instrument '" + patch + "', skipped");
            continue;
        }
        if (isVstSound(patch) && !findVst(patch).exists()) // not installed here, like a missing sampled instrument
        {
            DBG(where + "no plugin for '" + patch + "', skipped");
            continue;
        }
        if (!isEngineSound(patch) && !isDexedSound(patch) && !isSfzSound(patch) && !isVstSound(patch) && !findSurgePatch(patch).existsAsFile())
        {   // not in this Surge (Kerem's Mac, 2026-11-14: its Surge lacks 3rd/A.Liv/Basses/Sqweird): skipped like a missing
            // sampled instrument, the track plays its other sounds; tools/install-mac.sh lists them
            DBG(where + "no Surge patch '" + patch + "', skipped");
            continue;
        }
        for (int t : targets) parts[t].push_back({ patch, tokens[1].getFloatValue(), tokens[2].getFloatValue(), from });
    }
    for (auto& p : parts) // dark to bright; the entering sounds last, out of the plan's draw
        std::stable_sort(p.begin(), p.end(), [](auto& a, auto& b) { return a.from.isEmpty() != b.from.isEmpty() ? a.from.isEmpty() : a.brightness < b.brightness; });
    for (int t = 0; t < info->numTracks; ++t)
        if (parts[t].empty())
        {   // none of its sounds installed here: one of the engine's own, so the song still plays (and says nothing it cannot)
            const juce::String n = info->trackNames[t];
            const char* own = n == "bass" || n == "sub" ? "internal/808"
                            : n == "metal" || n == "perc" || n == "tick" || n == "ground" || n == "body" || n == "impact" || n == "mallet" ? "internal/metal"
                            : n == "pulse" || n == "boom" ? "internal/none" : "internal/blip";
            DBG(file.getFileName() + ": no installed sound for " + n + ", the engine's " + own);
            parts[t].push_back({ own, 0.0f, 1000.0f });
        }
    return {};
}

// What the visuals follow, for a song (the renderer's show.json without its levels; the app sends it live): its tempo,
// key, sections, voices and every note with its role.
juce::String showHead(const std::string& sceneName, uint64_t seed, double blockRate, double silence, double seedSeconds, double bridge)
{
    auto cp = makeComposer(sceneName, seed, silence, seedSeconds, bridge);
    auto& c = *cp;
    std::vector<Event> ev;
    for (int b = 0; b < c.totalBars(); ++b) c.generateBar(b, ev);
    std::stable_sort(ev.begin(), ev.end());
    const double secPerTick = 60.0 / (c.bpm() * kPPQ);
    auto num = [](double v) { return juce::String(v, 4).trimCharactersAtEnd("0").trimCharactersAtEnd("."); };

    juce::MemoryOutputStream j; // a juce::String grown by << took 6 minutes for 57k level rows
    j << "{\"seed\":" << juce::String(int64_t(seed)) << ",\"bpm\":" << num(c.bpm()) << ",\"key\":\"" << juce::String(c.describe()) << "\""
      << ",\"tracks\":[\"pad\",\"bass\",\"seq\",\"lead\"],\"levelNames\":[\"pad\",\"bass\",\"seq\",\"lead\",\"delay\",\"reverb\",\"master\"]"
      << ",\"levelRate\":" << num(blockRate) << ",\"sections\":[";
    for (size_t i = 0; i < c.sections().size(); ++i)
    {
        auto& sc = c.sections()[i];
        j << (i ? "," : "") << "{\"t\":" << num(sc.startBar * 4 * 60.0 / c.bpm()) << ",\"bars\":" << sc.bars
          << ",\"energy\":" << num(sc.energy) << ",\"name\":\"" << sc.name << "\"}";
    }
    const auto& info = sceneInfo(sceneName);
    j << "],\"voices\":[";
    for (int k = 0; k < info.numTracks; ++k) j << (k ? ",\"" : "\"") << info.trackNames[k] << "\"";
    j << "],\"notes\":["; // [time s, duration s, role, pitch, velocity, voice]: roles for every study, voices for a piece's own mapping
    int64_t onAt[kMaxTracks][128];
    float velAt[kMaxTracks][128] = {};
    std::fill(&onAt[0][0], &onAt[0][0] + kMaxTracks * 128, int64_t(-1));
    bool first = true;
    for (auto& e : ev)
    {
        if (e.type == Event::NoteOn) { onAt[e.track][e.pitch] = e.tick; velAt[e.track][e.pitch] = e.value; }
        else if (e.type == Event::NoteOff && onAt[e.track][e.pitch] >= 0)
        {
            const int64_t on = onAt[e.track][e.pitch];
            j << (first ? "" : ",") << "[" << num(on * secPerTick) << "," << num((e.tick - on) * secPerTick) << ","
              << info.role[e.track] << "," << int(e.pitch) << "," << num(velAt[e.track][e.pitch]) << "," << e.track << "]";
            first = false;
            onAt[e.track][e.pitch] = -1;
        }
    }
    j << "]";
    return j.toString();
}

// Whether a patch can sound on this machine: the palette loader's own checks
static bool patchInstalled(const juce::String& patch)
{
    if (isEngineSound(patch)) return true;
    if (isSfzSound(patch)) return findSfz(patch).existsAsFile();
    if (isVstSound(patch)) return findVst(patch).exists();
    if (isDexedSound(patch)) return findDexed().exists();
    return findSurgePatch(patch).existsAsFile();
}

juce::File writeImprovPalette(uint64_t seed, int k)
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lisztmachine-improv")
                    .getChildFile(juce::String(int64_t(seed)) + "-" + juce::String(k) + ".txt");
    file.getParentDirectory().createDirectory();
    file.replaceWithText(improvPalette(improvSong(seed, k), [](const std::string& s) { return findPalette(s).loadFileAsString().toStdString(); },
                                       [](const std::string& p) { return patchInstalled(juce::String(p)); }));
    return file;
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
    static constexpr int offsetBars[kMaxTracks] = { 0, 4, 2, 0, 4, 2, 0, 4, 2, 0 }; // Berlin: pad, bass, seq, lead
    Rng rng(seed * 0x9E3779B97F4A7C15ull + 7);
    std::vector<SoundChange> plan;
    const auto& sections = composer.sections();
    const auto& info = *palette.info;
    for (int t = 0; t < info.numTracks; ++t)
    {
        // A track in a group follows the group's first track: same bars, same entries.
        if (info.group[t] >= 0)
        {
            const int lead = info.group[t];
            for (size_t k = 0, m = plan.size(); k < m; ++k)
                if (plan[k].track == lead) plan.push_back({ plan[k].bar, t, std::min(plan[k].entry, int(palette.parts[t].size()) - 1) });
            continue;
        }
        if (palette.parts[t].size() == 1 && isEngineSound(palette.parts[t][0].patch)) { plan.push_back({ 0, t, 0 }); continue; }
        // a sound entering at a named part ("enter"): from there on the track keeps it, nothing drawn
        int n = int(palette.parts[t].size()), enterBar = 1 << 30;
        for (int e = 0; e < int(palette.parts[t].size()); ++e)
            if (const auto& f = palette.parts[t][size_t(e)].from; f.isNotEmpty())
            {
                n = juce::jmin(n, e);
                for (auto& sec : sections)
                    if (f == sec.name && sec.startBar < enterBar) { enterBar = sec.startBar; plan.push_back({ sec.startBar, t, e }); }
            }
        int last = -1, beforeLast = -1;
        // s counts the song's own parts: a part set aside (the concert's bridge before the song) changes nothing, the
        // song's sounds come out as without it and the bridge plays its first ones
        for (int k = 0, s = -1; k < int(sections.size()); ++k)
        {
            if (sections[size_t(k)].aside) continue;
            ++s;
            // Pad and sequence change on even sections, bass and lead on odd ones.
            const bool evenPart = t % 2 == 0;
            if (s > 0 && (s % 2 == 0) != evenPart) continue;

            // Energy picks a place on the dark-to-bright line, with a little seeded wander,
            // and never one of the last two sounds again.
            const double target = sections[size_t(k)].energy * (n - 1) + (rng.uniform() - 0.5) * n * 0.3;
            int best = -1;
            for (int e = 0; e < n; ++e)
                if ((n <= 2 || (e != last && e != beforeLast)) && (best < 0 || std::abs(e - target) < std::abs(best - target)))
                    best = e;
            const auto& sec = sections[size_t(k)];
            const int bar = s == 0 ? 0 : juce::jmin(sec.startBar + offsetBars[t], sec.startBar + sec.bars - 1);
            if (sec.startBar < enterBar) plan.push_back({ bar, t, best }); // drawn all the same: the other tracks' draws stay
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

    numTracks_ = palette.info->numTracks;
    scene = palette.scene;
    for (int i = 0; i < numTracks_; ++i)
    {
        auto& t = tracks_[i];
        int channels = 2;
        const auto& first = palette.parts[i].front().patch;
        t.internal = isEngineSound(first);
        t.trim = juce::Decibels::decibelsToGain(palette.trackGainDb[i]);
        t.glide = palette.glideS[i];
        t.wobLo = palette.wobbleHz[i][0], t.wobHi = palette.wobbleHz[i][1], t.wobCut = t.wobHi;
        t.wobS1[0] = t.wobS1[1] = t.wobS2[0] = t.wobS2[1] = 0;
        t.echo.prepare(sampleRate);
        auto m = mixFor(palette.info->trackNames[i]);
        if (palette.pan[i] >= -1) m.pan = palette.pan[i];
        t.duck = m.duck;
        t.width = m.width;
        t.panL = std::sqrt(2.0f) * std::cos((m.pan + 1) * juce::MathConstants<float>::pi / 4);
        t.panR = std::sqrt(2.0f) * std::sin((m.pan + 1) * juce::MathConstants<float>::pi / 4);
        const float cut = palette.lowCutHz[i] >= 0 ? palette.lowCutHz[i] : m.lowCutHz;
        t.lowCut = cut > 0;
        for (auto& f : t.hp) f.highpass(sampleRate, juce::jmax(10.0f, cut));
        t.mix.setSize(2, maxBlock);
        t.gains.assign(size_t(maxBlock), 0);
        t.drive.prepare(sampleRate, palette.drives[i]);
        t.amp.prepare(sampleRate, palette.amps[i]);
        t.crush.prepare(palette.crushes[i]);
        t.comp.prepare(sampleRate, palette.comps[i]);
        if (palette.duckDepth[i] >= 0) t.duck = palette.duckDepth[i];
        // Each track a different delay (7 to 19 ms) so the widened tracks don't fuse into one image.
        t.wideAmount = juce::jmax(0.0f, m.width - 1) * 1.4f;
        t.wide.assign(size_t((0.007 + 0.003 * (i % 5)) * sampleRate), 0.0f);
        t.wideAt = 0;
        t.hasEcho = palette.delays[i].beats > 0 && palette.delays[i].mix > 0;
        if (t.internal)
        {
            const auto kind = first.contains("/modal-") ? InternalSynth::Modal : first.endsWith("sub") ? InternalSynth::Sub : first.endsWith("blip") ? InternalSynth::Blip
                            : first.endsWith("kick") ? InternalSynth::Kick : first.endsWith("hat") ? InternalSynth::Hat
                            : first.endsWith("clap") ? InternalSynth::Clap : first.endsWith("snare") ? InternalSynth::Snare
                            : first.endsWith("metal") ? InternalSynth::Metal
                            : first.endsWith("zap") ? InternalSynth::Zap : first.endsWith("none") ? InternalSynth::None
                            : first.endsWith("guitar") ? InternalSynth::Guitar : first.endsWith("deep") ? InternalSynth::Deep : first.endsWith("808") ? InternalSynth::Bass808 : first.endsWith("boom") ? InternalSynth::Boom : InternalSynth::Click;
            t.synth.prepare(sampleRate, kind, first.fromFirstOccurrenceOf("/modal-", false, false));
            for (auto& s : t.slots) { s.plugin.reset(); s.midi.ensureSize(8192); s.state = Idle; s.entry = 0; s.gain = juce::Decibels::decibelsToGain(palette.parts[i].front().gainDb); }
            t.buffer.setSize(2, maxBlock);
            continue;
        }
        for (auto& s : t.slots)
        {
            juce::String err;
            guarded([&] { s.plugin = createSurge(sampleRate, maxBlock, err); if (s.plugin) s.plugin->prepareToPlay(sampleRate, maxBlock); });
            if (!s.plugin) return err;
            s.midi.ensureSize(8192);
            s.state = Idle;
            s.entry = -1;
            channels = juce::jmax(channels, s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels());
        }
        t.buffer.setSize(channels, maxBlock);
    }

    juce::String err;
    guarded([&] { delay_ = createSurgeEffect(kDelayBus, sampleRate, maxBlock, err); });
    if (!delay_) return "delay bus: " + err;
    guarded([&] { reverb_ = createSurgeEffect(kReverbBus, sampleRate, maxBlock, err); });
    if (!reverb_) return "reverb bus: " + err;
    auto busChannels = [](juce::AudioPluginInstance& p) { return juce::jmax(2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()); };
    delayBus_.setSize(busChannels(*delay_), maxBlock);
    fbBuf_.setSize(2, maxBlock);
    fbBus_.prepare(sampleRate);
    reverbBus_.setSize(busChannels(*reverb_), maxBlock);

    // The drum pad's sound: the palette's 'drumpad' line, else the pad's own voice, the same struck glass in every
    // song, so it stands apart from the song's synths (2026-11-20; measured in tune, pitch spread 6 cents)
    pad_ = {};
    pad_.patch = palette.padPatch.isNotEmpty() ? palette.padPatch : juce::String(kPadVoice);
    float padDb = palette.padGainSet ? palette.padGainDb : kPadVoiceDb;
    if (pad_.patch.isNotEmpty())
    {
        guarded([&] {
            juce::String e;
            auto p = createSurge(sampleRate, maxBlock, e);
            if (!p) { pad_.info = " · " + e; return; }
            p->prepareToPlay(sampleRate, maxBlock);
            if (e = loadSurgePatch(*p, findSurgePatch(pad_.patch)); e.isNotEmpty()) { pad_.info = " · " + e; return; }
            purify(*p, true); // in tune with the song: no detune
            // struck: no attack at all (Kerem, 2026-11-22: "shortest attack possible"), rings away over about two seconds,
            // no held sustain; the filter opens at once too
            for (auto sc : { "A", "B" })
            {
                for (auto [name, value] : { std::pair<const char*, const char*> { "Attack", "0.00 ms" }, { "Decay", "2.00 s" },
                                            { "Sustain", "0.00 %" }, { "Release", "2.00 s" } })
                    if (auto err = setParameterText(*p, juce::String(sc) + " Amp EG " + name, value); err.isNotEmpty())
                        pad_.info << " · " << err.substring(0, 100);
                if (auto err = setParameterText(*p, juce::String(sc) + " Filter EG Attack", "0.00 ms"); err.isNotEmpty())
                    pad_.info << " · " << err.substring(0, 100);
            }
            p->reset();
            pad_.buf.setSize(juce::jmax(2, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()), maxBlock);
            pad_.plugin = std::move(p);
        });
        pad_.gain = juce::Decibels::decibelsToGain(padDb + kPadTrimDb);
        pad_.midi.ensureSize(1024);
        juce::Reverb::Parameters rp; // a soft, wide room, about three seconds: the hit blooms and fades, never repeats
        rp.roomSize = 0.86f;
        rp.damping = 0.55f;
        rp.wetLevel = 0.42f;
        rp.dryLevel = 0.62f;
        rp.width = 1.0f;
        padVerb_.setParameters(rp);
        padVerb_.setSampleRate(sampleRate);
        padVerb_.reset();
    }

    limiter.prepare(sampleRate, -1.0f, 150.0f);
    glue.prepare(sampleRate);
    guard.prepare(sampleRate);
    masterFx.prepare(sampleRate, maxBlock);
    glue.makeupDb = palette.masterDb;
    for (auto& f : reverbLowCut_) f.highpass(sampleRate, 180);
    for (auto& o : octaveUp_) o.prepare(sampleRate);
    kickTrack_ = palette.info->trackOf("kick") >= 0 ? palette.info->trackOf("kick") : palette.info->trackOf("pulse");
    duckCurve_.assign(size_t(maxBlock), 0);
    kickHits_.reserve(64);
    hushCurve_.assign(size_t(maxBlock), 1);
    ringCurve_.assign(size_t(maxBlock), 1);
    hushAt_.reserve(16);
    hush_ = hushTarget_ = ring_ = 1;
    return {};
}

// Pure tuning: one voice per oscillator, no unison detune, oscillators on whole semitones, no
// analog drift, and no chorus-type effect (they all move the pitch). Measured on the factory
// choirs: up to 16 voices spread +-38 cents, one oscillator 0.32 semitones sharp.
void purify(juce::AudioPluginInstance& p, bool roundTuning)
{
    auto find = [&](const juce::String& name) -> juce::AudioProcessorParameter* {
        for (auto* q : p.getParameters()) if (q->getName(128) == name) return q;
        return nullptr;
    };
    for (auto sc : { "A", "B" })
    {
        const juce::String scn(sc);
        for (int o = 1; o <= 3; ++o)
        {
            const auto osc = scn + " Osc " + juce::String(o) + " ";
            setParameterText(p, osc + "Unison Voices", "1 voice");
            setParameterText(p, osc + "Unison Detune", "0.00 cents");
            if (auto* q = roundTuning ? find(osc + "Pitch") : nullptr)
            {
                const double v = q->getCurrentValueAsText().retainCharacters("-0123456789.").getDoubleValue();
                if (std::abs(v - std::round(v)) > 0.004) setParameterText(p, osc + "Pitch", juce::String(std::round(v), 2) + " semitones");
            }
        }
        setParameterText(p, scn + " Osc Drift", "0.00 %");
        if (auto* q = roundTuning ? find(scn + " Pitch") : nullptr)
        {
            const double v = q->getCurrentValueAsText().retainCharacters("-0123456789.").getDoubleValue();
            if (std::abs(v - std::round(v)) > 0.004) setParameterText(p, scn + " Pitch", juce::String(std::round(v), 2) + " semitones");
        }
    }
    static const char* moving[] = { "Chorus", "Ensemble", "Flanger", "Phaser", "Rotary Speaker", "Frequency Shifter", "Ring Modulator",
                                    "Vocoder", "Delay", "Reverb 2", "Nimbus", "Treemonster" };
    for (auto* q : p.getParameters())
        if (q->getName(128).endsWith("FX Type"))
            for (auto* m : moving)
                if (q->getCurrentValueAsText().equalsIgnoreCase(m)) { setParameterText(p, q->getName(128), "Off"); break; }
}

// Message thread, on an instance the audio thread is not touching (Idle or Loading).
juce::String Engine::loadInto(Slot& slot, int track, int entry)
{
    const auto& e = palette_.parts[track][size_t(entry)];
    if (tracks_[track].internal) { slot.entry = entry; return {}; }
    // A track may rotate between Surge, Dexed and sampled instruments: the slot's instance is swapped.
    const bool dexed = isDexedSound(e.patch), sampled = isSfzSound(e.patch), vst = isVstSound(e.patch);
    if (vst)
    {
        juce::String err;
        auto p = createVstSound(e.patch, sampleRate_, maxBlock_, err);
        if (!p) return err;
        p->prepareToPlay(sampleRate_, maxBlock_);
        slot.plugin = std::move(p);
        slot.dexed = false;
        slot.sfz = e.patch; // not Surge: the next Surge sound makes a fresh instance
    }
    else if (sampled)
    {
        if (slot.sfz != e.patch || !slot.plugin)
        {
            juce::String err;
            auto p = createSfz(sampleRate_, maxBlock_, findSfz(e.patch), err);
            if (!p) return err;
            slot.plugin = std::move(p);
            slot.sfz = e.patch;
            slot.dexed = false;
        }
    }
    // Surge always gets a fresh instance: a patch loaded into a used one left the host's cached
    // parameters stale (Late Night's cutoff read 0.78 for 0.04, and the filter automation then
    // opened it ten dB hot), so a sound's level depended on what the instance played before.
    else if (!dexed || slot.dexed != dexed || slot.sfz.isNotEmpty() || !slot.plugin)
    {
        juce::String err;
        auto p = dexed ? createDexed(sampleRate_, maxBlock_, err) : createSurge(sampleRate_, maxBlock_, err);
        if (!p) return err;
        p->prepareToPlay(sampleRate_, maxBlock_);
        slot.plugin = std::move(p);
        slot.dexed = dexed;
        slot.sfz = {};
    }
    if (sampled || vst) {}
    else if (dexed) { if (auto err = loadDexedVoice(*slot.plugin, e.patch); err.isNotEmpty()) return err; }
    else
    {
        if (auto err = loadSurgePatch(*slot.plugin, findSurgePatch(e.patch)); err.isNotEmpty()) return err;
        if (palette_.info->pure[track]) purify(*slot.plugin, palette_.info->pure[track] == 1);
        if (const float rel = palette_.releaseS[track]; rel > 0)
            for (auto sc : { "A", "B" })
                for (auto* q : slot.plugin->getParameters())
                    if (q->getName(128) == juce::String(sc) + " Amp EG Release")
                    {   // only ever longer: "873.8 ms", "2.58 s"
                        const auto t = q->getCurrentValueAsText();
                        const double now = t.retainCharacters("0123456789.").getDoubleValue() * (t.endsWith(" ms") ? 0.001 : 1.0);
                        if (now < rel)
                            if (auto err = setParameterText(*slot.plugin, q->getName(128), juce::String(rel, 2) + " s"); err.isNotEmpty()) return err;
                    }
    }
    slot.plugin->reset();
    slot.cutoff = nullptr;
    for (auto* p : slot.plugin->getParameters())
        // Dexed's filter closes far harder than Surge's (a voice fell 21 dB in a summit): it is left alone
        if (!dexed && !vst && p->getName(64) == (sampled ? "Expression" : "A Filter 1 Cutoff")) { slot.cutoff = p; slot.cutoffBase = p->getValue(); }
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

void Engine::start(uint64_t seed, int startBar, bool play)
{
    auto next = makeComposer(scene, seed, silence, seedSeconds, bridge);
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
    float level[kMaxTracks] = {}, filter[kMaxTracks] = {}, held[kMaxTracks][128] = {};
    for (auto& e : earlier)
    {
        // A note-off on the start tick ends a note before it can be re-struck.
        if (e.tick < startTick || (e.tick == startTick && e.type == Event::NoteOff))
            switch (e.type)
            {
                case Event::Level:   level[e.track] = e.value; break;
                case Event::Filter:  filter[e.track] = e.value; break;
                default: break;
                case Event::NoteOn:  held[e.track][e.pitch] = e.value; break;
                case Event::NoteOff: held[e.track][e.pitch] = 0; break;
            }
        else
            carried.push_back(e);
    }
    for (int t = 0; t < numTracks_; ++t)
        for (int p = 0; p < 128; ++p)
            if (held[t][p] > 0) carried.push_back({ startTick, Event::NoteOn, uint8_t(t), uint8_t(p), held[t][p] });
    std::sort(carried.begin(), carried.end());

    auto plan = planSounds(*next, palette_, seed);

    // The delay is a division of the bar (A-9): dotted eighth left, quarter right; the
    // tape wobble takes one bar per cycle.
    const double beat = 60.0 / next->bpm();
    delaySeconds_[0] = float(0.75 * beat);
    delaySeconds_[1] = float(beat);
    fbBus_.set(palette_.fbBeats * beat, palette_.fbAmount);
    if (delay_ && reverb_)
    {
        // Their pitch modulation (the delay's tape wobble, the reverb's chorus) reads as detune
        // under pure-tuned voices; Berlin keeps it.
        setParameterText(*delay_, "Modulation Depth", palette_.info->pureBuses ? "0 cents" : "8 cents");
        setParameterText(*reverb_, "Reverb Modulation", palette_.info->pureBuses ? "0 %" : "60 %");
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
    glideN_ = 0;
    holdUntil_ = 0;
    clickFrom = 0;
    {   // the drum pad before any chord is heard: the key's tonic and fifth, from "<title> · <tonic> <mode> · ..."
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto words = juce::StringArray::fromTokens(juce::String::fromUTF8(composer_->describe().c_str()), " ", "");
        int tonic = -1;
        for (auto& w : words) for (int pc = 0; pc < 12 && tonic < 0; ++pc) if (w == names[pc]) tonic = pc;
        padMask_ = tonic >= 0 ? (1 << tonic) | (1 << ((tonic + 7) % 12)) : 0;
    }
    const double barSeconds = 4 * 60.0 / composer_->bpm();
    for (int i = 0; i < numTracks_; ++i)
    {
        auto& t = tracks_[i];
        const auto& d = palette_.delays[i];
        if (t.hasEcho) t.echo.set(d.beats * 60.0 / composer_->bpm(), feedbackOverride >= 0 ? feedbackOverride : d.feedback, d.mix, d.throws);
        if (t.internal) t.synth.allOff();
        t.gain.reset(sampleRate_, barSeconds); // level changes glide across a whole bar
        t.gain.setCurrentAndTargetValue(level[i]);
        t.filterNow = t.filterTarget = filter[i];
        std::copy(std::begin(held[i]), std::end(held[i]), std::begin(t.held));

        // This part's sound at the start bar is the last change at or before it.
        t.changes.clear();
        int entry = 0;
        for (auto& c : plan_)
            if (c.track == i && c.bar <= startBar) entry = c.entry;
            else if (c.track == i && !t.internal) t.changes.push_back(c);
        t.nextChange = 0;
        t.live = 0;
        t.slots[1].state = Idle;
        t.slots[1].entry = -1;
        guarded([&] { if (auto e = loadInto(t.slots[0], i, entry); e.isNotEmpty()) DBG(e); });
        t.slots[0].state = Live;
    }
    changesMade = 0;
    missedChanges = 0;
    limiter.reset();
    glue.reset();
    // The duck lets go over about a quarter of a beat: back by the next one, even at 4/4.
    duckDecay_ = std::exp(-1.0f / float(0.22 * 60.0 / composer_->bpm() * sampleRate_));
    duckEnv_ = duckTarget_ = 0;
    duckRamp_ = 0;
    for (auto& t : tracks_) for (auto& f : t.hp) f.z1 = f.z2 = 0;
    bar = startBar;
    quiet_ = 0;
    finished_ = false;
    if (play) go();
}

void Engine::stop()
{
    playing_ = false;
    panic_ = true;
}

void Engine::service()
{
    for (int i = 0; i < numTracks_; ++i)
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
        {
            if (t.internal) { t.synth.allOff(); continue; }
            for (auto& s : t.slots)
            {
                s.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                s.midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            }
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
                case Event::NoteOn:
                    live.midi.addEvent(juce::MidiMessage::noteOn(1, e.pitch, e.value), at);
                    t.held[e.pitch] = e.value;
                    if (e.track == kickTrack_) kickHits_.push_back({ at, e.value });
                    break;
                case Event::NoteOff: live.midi.addEvent(juce::MidiMessage::noteOff(1, e.pitch), at); t.held[e.pitch] = 0; break;
                case Event::Level:   t.gain.setTargetValue(e.value); break;
                case Event::Filter:  t.filterTarget = e.value; break;
                case Event::Throw:   if (t.hasEcho) t.echo.throwFor(at, int(e.value * kPPQ * samplesPerTick_)); break;
                case Event::Hush:
                    hushAt_.push_back({ at, e.value });
                    if (e.value < 0.5f) { if (e.pitch & 1) hushKeepNext_ |= 1u << e.track; if (e.pitch & 2) hushTailsNext_ = true; if (e.pitch & 4) hushSoftNext_ = true; }
                    break;
            }
        }
        for (int i = 0; i < numTracks_; ++i) changesDue(i, end - 1);

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
    float* st[2 * kMaxTracks + 6] = {};
    if (stemOut)
        for (int k = 0; k < 2 * numTracks_ + 6; ++k) st[k] = stemOut[k] + offset;
    fbBuf_.clear(0, len);
    fbBuf_.clear(1, len);
    float* fbIn[2] = { fbBuf_.getWritePointer(0), fbBuf_.getWritePointer(1) };
    auto runFx = [&](juce::AudioPluginInstance& fx, juce::AudioBuffer<float>& buf) {
        for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, len); // a side-chain input hears nothing
        fx.processBlock(buf, noMidi_);
        noMidi_.clear();
    };

    // The kick's duck, as a gain curve for this chunk: struck by the kick's own notes (exact to
    // the sample, and there even when the kick track is soloed out), down in 2 ms, back over
    // about a quarter beat.
    {
        const int attack = juce::jmax(1, int(0.002 * sampleRate_));
        size_t k = 0;
        for (int n = 0; n < len; ++n)
        {
            for (; k < kickHits_.size() && kickHits_[k].first <= n; ++k)
            {
                duckTarget_ = juce::jmax(duckEnv_, kickHits_[k].second);
                duckRamp_ = attack;
            }
            if (duckRamp_ > 0) { duckEnv_ += (duckTarget_ - duckEnv_) / float(duckRamp_); --duckRamp_; }
            else duckEnv_ *= duckDecay_;
            duckCurve_[size_t(n)] = mixStage ? duckEnv_ : 0;
        }
        kickHits_.clear();
    }
    {   // A stop's silence: everything, the reverb and echo tails too, dies away like a room (Kerem, 2026-11-06:
        // "a release, so it feels natural", not a wall): an exponential release, -60 dB in half a second, then
        // true silence; back in at the downbeat in 15 ms.
        const float step = float(1.0 / (0.015 * sampleRate_)), release = float(std::exp(-1.0 / (0.072 * sampleRate_)));
        // where the tails ring (a fall, the hole) they still die away, only slower: -43 dB a second, so the room
        // rings out over the bar line instead of holding the music's level through the silence
        const float ringOut = float(std::exp(-1.0 / (0.2 * sampleRate_)));
        // and after them they come back slowly, so the old part's room does not spring back over the new
        // part's downbeat (Kerem, 2026-11-07: the start after the echoes was not sharp enough)
        const float reopen = float(1.0 / (0.35 * sampleRate_));
        size_t k = 0;
        for (int n = 0; n < len; ++n)
        {
            for (; k < hushAt_.size() && hushAt_[k].first <= n; ++k)
            {
                const float was = hushTarget_;
                hushTarget_ = hushAt_[k].second;
                if (hushTarget_ < 0.5f && was >= 0.5f) // a silence begins: what it keeps (open, keeping it changes nothing)
                {
                    hushKeep_ = hushKeepNext_;
                    hushTails_ = hushTailsNext_;
                    hushSoft_ = hushSoftNext_;
                    hushKeepNext_ = 0;
                    hushTailsNext_ = hushSoftNext_ = false;
                }
            }
            if (hushTarget_ < hush_) hush_ = hush_ * release < 1e-3f ? 0.0f : hush_ * release;
            else hush_ += juce::jmin(step, hushTarget_ - hush_);
            hushCurve_[size_t(n)] = hush_;
            if (hushTarget_ < ring_) ring_ = ring_ * ringOut < 1e-3f ? 0.0f : ring_ * ringOut;
            else ring_ += juce::jmin(reopen, hushTarget_ - ring_);
            ringCurve_[size_t(n)] = ring_;
        }
        hushAt_.clear();
    }

    // One track's mix stage: its low cut, width, pan and duck, then its gain, into its own `mix` (summed into the mix and
    // the sends below, in track order, once every track is done)
    auto mixTrack = [&](int i, const float* L, const float* R) {
        auto& t = tracks_[i];
        float peak = 0;
        double sq = 0;
        for (int n = 0; n < len; ++n)
        {
            float l = L[n], r = R[n], g = t.gains[size_t(n)] * t.trim;
            if (mixStage)
            {
                if (t.drive.on()) { l = t.drive.process(l, 0); r = t.drive.process(r, 1); }
                if (t.amp.on()) t.amp.process(l, r);
                if (t.crush.on()) t.crush.process(l, r);
                if (t.comp.on()) t.comp.process(l, r);
                if (t.wobLo > 0) // the wobble (Bleach's growl): a resonant low-pass whose cutoff rides the track's filter, after the amp,
                {                // low to high on an exponential scale, smoothed over 3 ms; a state-variable filter (TPT)
                    const float want = t.wobLo * std::pow(t.wobHi / t.wobLo, juce::jlimit(0.0f, 1.0f, 0.5f * (t.filterNow + 1)));
                    t.wobCut += (want - t.wobCut) * float(1.0 - std::exp(-1.0 / (0.003 * sampleRate_)));
                    const float g = std::tan(juce::MathConstants<float>::pi * juce::jmin(t.wobCut, float(0.45 * sampleRate_)) / float(sampleRate_));
                    const float k = 0.6f, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                    float* io[2] = { &l, &r };
                    for (int c = 0; c < 2; ++c)
                    {
                        const float v3 = *io[c] - t.wobS2[c], v1 = a1 * t.wobS1[c] + a2 * v3, v2 = t.wobS2[c] + a2 * t.wobS1[c] + a3 * v3;
                        t.wobS1[c] = 2 * v1 - t.wobS1[c], t.wobS2[c] = 2 * v2 - t.wobS2[c];
                        *io[c] = v2;
                    }
                }
                if (t.lowCut) { l = t.hp[0].process(l); r = t.hp[1].process(r); }
                const float mid = (l + r) * 0.5f;
                float side = (l - r) * 0.5f * juce::jmin(1.0f, t.width);
                if (t.wideAmount > 0)
                {
                    const float d = t.wide[size_t(t.wideAt)];
                    t.wide[size_t(t.wideAt)] = mid;
                    t.wideAt = (t.wideAt + 1) % int(t.wide.size());
                    t.wideLp += (d - t.wideLp) * 0.04f; // ~300 Hz: below it the widened part is removed
                    side += (d - t.wideLp) * t.wideAmount;
                }
                l = (mid + side) * t.panL;
                r = (mid - side) * t.panR;
                g *= 1 - t.duck * duckCurve_[size_t(n)];
            }
            const float x[2] = { l * g, r * g };
            if (stemOut) { st[2 * i][n] = x[0]; st[2 * i + 1][n] = x[1]; }
            for (int c = 0; c < 2; ++c)
            {
                t.mix.getWritePointer(c)[n] = x[c]; // L, R may be this buffer: sample n is read above, before it is written
                peak = juce::jmax(peak, std::abs(x[c]));
                sq += double(x[c]) * x[c];
            }
        }
        meters[i].peak = juce::jmax(meters[i].peak.load(), peak);
        meters[i].sumSquares = meters[i].sumSquares.load() + sq;
        meters[i].samples = meters[i].samples.load() + 2 * len;
        t.out = true;
    };

    auto renderTrack = [&](int i) {
        auto& t = tracks_[i];
        struct Timed { double& worst; float& cost; int64_t t0 = juce::Time::getHighResolutionTicks(); // each track's slowest block (--live) and its cost
                       ~Timed() { const double ms = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1000.0;
                                  worst = juce::jmax(worst, ms); cost += 0.05f * (float(ms) - cost); } } timed { trackMaxMs[i], t.cost };
        // Glide the filter towards its target over about a quarter second (a wobbling bass: its own `glide`).
        t.filterNow += (t.filterTarget - t.filterNow) * juce::jmin(1.0f, float(len / (t.glide * sampleRate_)));
        // A kept track sounds on through a silence, 7 dB down (14 in a soft one, the hole); where the tails ring,
        // a track's own echo is silenced at its input instead of its output, so what it already holds rings on.
        const bool kept = (hushKeep_ >> i) & 1, preEcho = hushTails_ && t.hasEcho && !kept;
        const float floor = hushSoft_ ? 0.2f : 0.45f;
        for (int n = 0; n < len; ++n) t.gains[size_t(n)] = t.gain.getNextValue() * (kept ? floor + (1 - floor) * hushCurve_[size_t(n)] : preEcho ? ringCurve_[size_t(n)] : hushCurve_[size_t(n)]);
        auto echo = [&](float* l, float* r) {
            if (preEcho) { juce::FloatVectorOperations::multiply(l, hushCurve_.data(), len); juce::FloatVectorOperations::multiply(r, hushCurve_.data(), len); }
            t.echo.process(l, r, len);
        };
        if (!(enabledTracks & (1 << i)))
        {
            for (auto& slot : t.slots)
            {
                slot.midi.clear();
                if (slot.state.load() == Tail) slot.state = Idle;
            }
            return;
        }

        if (t.internal)
        {   // the engine's own instrument: one voice pool, its MIDI in the first slot's buffer
            auto& slot = t.slots[0];
            t.buffer.clear();
            t.synth.render(slot.midi, t.buffer.getWritePointer(0), t.buffer.getWritePointer(1), len);
            slot.midi.clear();
            if (t.hasEcho) echo(t.buffer.getWritePointer(0), t.buffer.getWritePointer(1));
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply(t.buffer.getWritePointer(c), slot.gain, len);
            mixTrack(i, t.buffer.getReadPointer(0), t.buffer.getReadPointer(1));
            return;
        }
        t.mix.clear(0, 0, len);
        t.mix.clear(1, 0, len);
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
                juce::FloatVectorOperations::addWithMultiply(t.mix.getWritePointer(c), src, slot.gain, len);
                for (int n = 0; n < len; ++n) slotPeak = juce::jmax(slotPeak, std::abs(src[n]));
            }
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
        // The echo runs once on the track's sum: run per slot, a crossfade ran it twice a block (the old
        // sound's slot then carried the echo, never fell quiet, and the track sat 11 dB hot for 20 s).
        if (t.hasEcho) echo(t.mix.getWritePointer(0), t.mix.getWritePointer(1));
        mixTrack(i, t.mix.getReadPointer(0), t.mix.getReadPointer(1));
    };
    int order[kMaxTracks];
    for (int i = 0; i < numTracks_; ++i) { order[i] = i; tracks_[i].out = false; }
    std::sort(order, order + numTracks_, [this](int a, int b) { return tracks_[a].cost > tracks_[b].cost; }); // the longest first
    if (parallel)
        TrackPool::get().run(numTracks_, order, [](void* f, int i) { (*static_cast<decltype(renderTrack)*>(f))(i); }, &renderTrack);
    else
        for (int i = 0; i < numTracks_; ++i) renderTrack(i);
    for (int i = 0; i < numTracks_; ++i) // into the mix and the sends, in track order (the same sums one thread made)
    {
        const auto& t = tracks_[i];
        if (!t.out) continue;
        const float sends[2] = { palette_.sends[i][0] >= 0 ? palette_.sends[i][0] : palette_.info->sends[i][0],
                                 palette_.sends[i][1] >= 0 ? palette_.sends[i][1] : palette_.info->sends[i][1] };
        for (int c = 0; c < 2; ++c)
        {
            const float* x = t.mix.getReadPointer(c);
            juce::FloatVectorOperations::add(pa[c], x, len);
            juce::FloatVectorOperations::addWithMultiply(delayIn.getWritePointer(c), x, sends[0], len);
            juce::FloatVectorOperations::addWithMultiply(reverbIn.getWritePointer(c), x, sends[1], len);
            juce::FloatVectorOperations::addWithMultiply(fbIn[c], x, palette_.feeds[i], len);
        }
    }

    // The drum pad (input 2): a note of the chord sounding now, played live, so the song's silences do not touch it
    if (pad_.plugin)
    {
        int mask = 0; // the chord heard now: the harmony's held notes, else the melody's, the rhythm's, the bass's
        for (int role : { 0, 3, 2, 1 })
        {
            for (int i = 0; i < numTracks_; ++i)
                if (palette_.info->role[i] == role && !tracks_[i].internal)
                    for (int p = 0; p < 128; ++p) if (tracks_[i].held[p] > 0) mask |= 1 << (p % 12);
            if (mask) break;
        }
        if (mask) padMask_ = mask;
        if (const float v = padHit_.exchange(0); v > 0 && padMask_)
        {
            if (pad_.note >= 0) pad_.midi.addEvent(juce::MidiMessage::noteOff(1, pad_.note), 0);
            int pcs[12], n = 0;
            for (int pc = 0; pc < 12; ++pc) if (padMask_ & (1 << pc)) pcs[n++] = pc;
            const int k = pad_.count++ % (2 * n); // up the chord over two octaves from middle C, then again
            pad_.note = 60 + pcs[k % n] + 12 * (k / n);
            pad_.midi.addEvent(juce::MidiMessage::noteOn(1, pad_.note, v), 0);
            pad_.left = int64_t(kPPQ * samplesPerTick_); // let go after a beat: the ring goes on in the release
            padStruckNote = pad_.note;
            ++padStruck;
        }
        else if (pad_.note >= 0 && (pad_.left -= len) <= 0)
        {
            pad_.midi.addEvent(juce::MidiMessage::noteOff(1, pad_.note), 0);
            pad_.note = -1;
        }
        pad_.buf.clear();
        juce::AudioBuffer<float> view(pad_.buf.getArrayOfWritePointers(), pad_.buf.getNumChannels(), len);
        pad_.plugin->processBlock(view, pad_.midi);
        pad_.midi.clear();
        if (view.getNumChannels() >= 2) padVerb_.processStereo(view.getWritePointer(0), view.getWritePointer(1), len);
        else padVerb_.processMono(view.getWritePointer(0), len);
        const float g = pad_.gain * padLevel.load();
        double sq = 0;
        for (int c = 0; c < 2; ++c)
        {
            const float* x = view.getReadPointer(juce::jmin(c, view.getNumChannels() - 1));
            for (int n = 0; n < len; ++n) sq += double(x[n] * g) * double(x[n] * g);
        } // mixed in after the master effects, below
        padSq = padSq.load() + sq;
        padN = padN.load() + 2 * len;
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
        const float returnGain[2] = { juce::Decibels::decibelsToGain(palette_.returnDb[0]), juce::Decibels::decibelsToGain(palette_.returnDb[1]) };
        runFx(*delay_, delayIn);
        // The echoes and the reverb pump with the kick too, so the tail breathes instead of washing.
        auto duckReturn = [&](juce::AudioBuffer<float>& b, float depth) {
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < len; ++n) b.getWritePointer(c)[n] *= 1 - depth * duckCurve_[size_t(n)];
        };
        duckReturn(delayIn, 0.35f);
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply(delayIn.getWritePointer(c), (hushTails_ ? ringCurve_ : hushCurve_).data(), len);
        for (int c = 0; c < 2; ++c)
        {
            juce::FloatVectorOperations::addWithMultiply(pa[c], delayIn.getReadPointer(c), returnGain[0], len);
            juce::FloatVectorOperations::addWithMultiply(reverbIn.getWritePointer(c), delayIn.getReadPointer(c), kDelayToReverb, len);
        }
        meter(delayMeter, delayIn);
        if (stemOut)
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::copyWithMultiply(st[2 * numTracks_ + c], delayIn.getReadPointer(c), returnGain[0], len);
        if (palette_.shimmer > 0)
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < len; ++n) reverbIn.getWritePointer(c)[n] += palette_.shimmer * octaveUp_[c].process(reverbIn.getReadPointer(c)[n]);
        runFx(*reverb_, reverbIn);
        duckReturn(reverbIn, 0.5f);
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply(reverbIn.getWritePointer(c), (hushTails_ ? ringCurve_ : hushCurve_).data(), len);
        if (mixStage)
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < len; ++n) reverbIn.getWritePointer(c)[n] = reverbLowCut_[c].process(reverbIn.getReadPointer(c)[n]);
        meter(reverbMeter, reverbIn);
        if (stemOut)
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::copyWithMultiply(st[2 * numTracks_ + 2 + c], reverbIn.getReadPointer(c), returnGain[1], len);
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::addWithMultiply(pa[c], reverbIn.getReadPointer(c), returnGain[1], len);
    }

    if (effects) // the feedback bus (silent unless something feeds it)
    {
        // the slider glides: the whole range in about half a second
        if (const float a = feedbackBus.load(); a >= 0 && !(playing && pos_ > endSample_)) fbBus_.glideTo(a, float(len / (0.5 * sampleRate_)));
        fbBus_.process(fbIn[0], fbIn[1], len);
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply(fbIn[c], (hushTails_ ? ringCurve_ : hushCurve_).data(), len);
        const float g = juce::Decibels::decibelsToGain(palette_.returnDb[2]);
        for (int c = 0; c < 2; ++c)
        {
            if (stemOut) juce::FloatVectorOperations::copyWithMultiply(st[2 * numTracks_ + 4 + c], fbIn[c], g, len);
            juce::FloatVectorOperations::addWithMultiply(pa[c], fbIn[c], g, len);
        }
    }

    // the drum pad straight to the master, dry: no song echo, reverb or feedback, no master drive or delay (Kerem,
    // 2026-11-22: "I don't want the drum hits to be affected by the general feedback"), but through the master EQ
    // (2026-11-23: "add it")
    const auto padCh = [&](int c) { return pad_.buf.getReadPointer(juce::jmin(c, pad_.buf.getNumChannels() - 1)); };
    if (numChannels > 1)
        masterFx.process(pa[0], pa[1], len, composer_ ? composer_->bpm() : 120.0, pad_.plugin ? padCh(0) : nullptr,
                         pad_.plugin ? padCh(1) : nullptr, pad_.gain * padLevel.load());
    else if (pad_.plugin)
        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::addWithMultiply(pa[c], pad_.buf.getReadPointer(juce::jmin(c, pad_.buf.getNumChannels() - 1)),
                                                         pad_.gain * padLevel.load(), len);
    if (mixStage && numChannels > 1) glue.process(pa[0], pa[1], len);
    if (numChannels > 1) guard.process(pa[0], pa[1], len);
    float pre = 0;
    for (int c = 0; c < 2; ++c)
        pre = juce::jmax(pre, juce::FloatVectorOperations::findMaximum(pa[c], len), -juce::FloatVectorOperations::findMinimum(pa[c], len));
    peakPre = juce::jmax(peakPre.load(), pre);

    if (numChannels > 1) limiter.process(pa[0], pa[1], len);
    if (const float r = fadeStep_.load(); r > 0) // fadeOut(): an exponential fall, then silence
        for (int n = 0; n < len; ++n)
        {
            fade_ = fade_ * r < 0.001f ? 0.0f : fade_ * r;
            for (int c = 0; c < juce::jmin(2, numChannels); ++c) pa[c][n] *= fade_;
        }
    float post = 0;
    for (int c = 0; c < juce::jmin(2, numChannels); ++c)
        post = juce::jmax(post, juce::FloatVectorOperations::findMaximum(pa[c], len), -juce::FloatVectorOperations::findMinimum(pa[c], len));
    peakPost = juce::jmax(peakPost.load(), post);

    if (playing && (numChannels >= 3 || clickToMain.load())) renderClick(out, numChannels, offset, len);

    if (playing) { pos_ = end; seconds = double(pos_) / sampleRate_; }
    // 2026-11-04 (Kerem: "you cut the end of Working Class Curse abrupt; wait for total silence"): the
    // feedback bus at 0.99 still rang at -29 dB when a fixed 12 s tail ran out. After the last bar every
    // echo eases its feedback to 0.6 over two seconds, and the song ends after half a second under -70 dB.
    if (playing && pos_ > endSample_)
    {
        const float step = 0.4f * float(len / (2.0 * sampleRate_));
        fbBus_.letGo(0.6f, step);
        for (auto& t : tracks_) if (t.hasEcho) t.echo.letGo(0.6f, step);
        // and every sound lets go of its notes at the last bar: Doom's drone guitar held one past its note-off forever
        if (end - len <= endSample_)
            for (auto& t : tracks_) for (auto& slot : t.slots) slot.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        quiet_ = post < 3.2e-4f ? quiet_ + len : 0;
    }
    if (playing && nextBar_ >= composer_->totalBars() && head_ >= events_.size() && pos_ >= holdUntil_.load()
        && (quiet_ > int64_t(sampleRate_ / 2) || pos_ > endSample_ + int64_t(60 * sampleRate_))) // the reverb and delay tails
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
    const bool main = clickToMain.load();
    const int gn = glideN_.load(std::memory_order_acquire);
    for (int n = 0; n < len; ++n)
    {
        const int64_t s = pos_ + n;
        int64_t beat = -1;
        if (gn > 0 && s >= glideAt_[0]) // the glide into the next song
        {
            while (glideI_ < gn && glideAt_[glideI_] < s) ++glideI_;
            if (glideI_ < gn && glideAt_[glideI_] == s) beat = (gn - glideI_) % 4 == 0 ? 0 : 1; // its downbeats: whole bars before the landing
        }
        // the nearest beat, then whether this is its sample: ceil() overshot to the next beat whenever the beat's sample was
        // rounded up, and at most tempos about half the clicks never sounded (Kerem, 2026-11-14: "some of the metronome
        // hits disappear"; at 106 bpm 981 of 2000 missed, measured)
        else if (const int64_t b = int64_t(std::llround(double(s) / samplesPerTick_ / beatTicks));
                 sampleOf(b * beatTicks) == s && s < endSample_)
            beat = b;
        if (beat >= 0 && s >= clickFrom.load())
        {
            clickFreq_ = beat % 4 == 0 ? 1760.0 : 1320.0;
            clickEnv_ = clickLevel;
            clickPhase_ = 0;
        }
        float x = float(clickEnv_ * std::sin(clickPhase_));
        clickPhase_ += juce::MathConstants<double>::twoPi * clickFreq_ / sampleRate_;
        clickEnv_ *= decay;
        if (numChannels >= 3) out[2][offset + n] = x;
        if (numChannels >= 4) out[3][offset + n] = x;
        if (main) for (int c = 0; c < juce::jmin(2, numChannels); ++c) out[c][offset + n] += 0.6f * x;
    }
}

// The glide (the visuals page's, 2026-11-04): tempo over the ramp b0 + (b1 - b0) u + c u (1 - u), c chosen so the bars come
// out whole; the start bar is the one that keeps c, the bend, smallest.
void Engine::glideClick(double b1, double land, double minSec, double maxSec)
{
    holdUntil_ = std::llround(land * sampleRate_);
    if (!composer_ || b1 <= 0) return;
    const double b0 = composer_->bpm(), spb = 60 / b0, mean = (b0 + b1) / 2;
    int j = 0, N = 0;
    double D = 0, c = 0;
    for (int jj = int(std::floor((land - minSec) / spb / 4)) * 4; jj > 0 && land - jj * spb <= maxSec; jj -= 4)
    {
        const double d = land - jj * spb;
        // beats, fewer or more (2026-11-25: whole bars rounded to the nearest, the glide into Liquid first slowed 144 ->
        // 141 then rose to 166): the count with the least bend, so the tempo moves one way; the accents are counted back
        // from the landing (renderClick), so the high click still falls on the new song's downbeats
        for (const int n : { std::max(4, int(std::floor(mean * d / 60))), std::max(4, int(std::ceil(mean * d / 60))) })
        {
            const double cc = 6 * (n * 60 / d - mean);
            if (N == 0 || std::abs(cc) < std::abs(c)) { j = jj; N = n; D = d; c = cc; }
        }
    }
    if (N == 0 || N > kMaxGlide || seconds.load() > j * spb - 0.3) return; // too late to glide: the click keeps its tempo
    const auto phi = [&](double u) { return D / 60 * (b0 * u + (b1 - b0) * u * u / 2 + c * (u * u / 2 - u * u * u / 3)); }; // beats
    glideAt_[0] = sampleOf(int64_t(j) * kPPQ);
    for (int i = 1; i < N; ++i)
    {
        double lo = 0, hi = 1;
        for (int it = 0; it < 50; ++it) { const double m = (lo + hi) / 2; (phi(m) < i ? lo : hi) = m; }
        glideAt_[i] = std::llround((j * spb + lo * D) * sampleRate_);
    }
    glideJ_ = j;
    glideI_ = 0;
    glideN_.store(N, std::memory_order_release);
}

// ---------------------------------------------------------------- the engine's own instrument
// The modal materials: mode frequency ratios (measured shapes of real objects: a marimba bar, a
// circular membrane, a wine glass...), the fundamental's decay, how much faster the upper modes die,
// the strike's length (short = hard mallet, bright), how much raw noise the hit carries, and how it
// is excited.
static const InternalSynth::Material kMaterials[] = {
    { "wood",    { 1, 3.93f, 9.54f, 16.2f },                                   4, 0.22f, 0.6f,  1.2f, 0.05f, InternalSynth::Strike },
    { "block",   { 1, 2.68f, 5.40f },                                          3, 0.07f, 0.3f,  0.6f, 0.10f, InternalSynth::Strike },
    { "log",     { 1, 2.32f, 4.25f, 6.63f },                                   4, 0.55f, 0.5f,  3.0f, 0.04f, InternalSynth::Strike },
    { "skin",    { 1, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.50f },       8, 0.16f, 1.2f,  2.0f, 0.30f, InternalSynth::Strike },
    { "glass",   { 1, 2.76f, 5.40f, 8.93f },                                   4, 1.40f, 0.4f,  0.4f, 0.02f, InternalSynth::Strike },
    { "bowl",    { 1, 2.71f, 5.15f, 8.43f },                                   4, 3.50f, 0.25f, 2.0f, 0.01f, InternalSynth::Strike },
    { "kalimba", { 1, 5.92f, 13.0f },                                          3, 0.90f, 0.7f,  1.0f, 0.03f, InternalSynth::Strike },
    { "plate",   { 1, 1.47f, 2.09f, 2.56f, 3.14f, 3.90f, 4.70f, 5.60f },       8, 2.20f, 0.15f, 6.0f, 0.15f, InternalSynth::Strike },
    { "scrape",  { 1, 1.83f, 2.71f, 3.47f, 4.90f, 6.30f },                     6, 0.08f, 0.1f,  0.0f, 0.20f, InternalSynth::Scrape },
    { "rattle",  { 1, 2.30f, 3.90f, 5.60f },                                   4, 0.03f, 0.2f,  0.5f, 0.40f, InternalSynth::Rattle },
    { "chain",   { 1, 1.90f, 2.70f, 3.60f, 4.40f, 5.30f },                     6, 0.25f, 0.3f,  0.8f, 0.25f, InternalSynth::Rattle },
    { "crackle", { 1, 3.10f, 7.20f },                                          3, 0.004f, 0.0f, 0.0f, 0.60f, InternalSynth::Crackle },
};

void InternalSynth::prepare(double sampleRate, Kind kind, const juce::String& material)
{
    sr_ = sampleRate;
    kind_ = kind;
    material_ = &kMaterials[0];
    for (auto& m : kMaterials) if (material == m.name) material_ = &m;
    for (auto& v : voices_) v.note = -1;
    strings_.assign(kind == Guitar ? 24 : 0, {});
    for (auto& st : strings_) st.buf.assign(size_t(sampleRate / 25) + 4, 0.0f); // down to 25 Hz
}

void InternalSynth::pluck(Str& st, float velocity, bool palm, float pick)
{
    // the pick: a burst of noise as long as the string, darker when soft or muted, with a notch
    // comb where the pick strikes (a fraction of the way from the bridge)
    const float dyn = palm ? juce::jlimit(0.1f, 1.0f, velocity / 0.45f) : juce::jlimit(0.1f, 1.0f, (velocity - 0.5f) / 0.5f);
    const float bright = palm ? 0.25f + 0.25f * dyn : 0.45f + 0.5f * dyn, amp = 0.25f + 0.75f * dyn;
    const int notch = juce::jmax(1, int(pick * float(st.len)));
    std::vector<float> n(size_t(st.len));
    float lp = 0;
    for (auto& x : n) { seed_ = seed_ * 1664525u + 1013904223u; lp += (float(int32_t(seed_)) * (1.0f / 2147483648.0f) - lp) * bright; x = lp; }
    for (int i = 0; i < st.len; ++i)
        st.buf[size_t((st.w + i) % st.len)] += amp * (n[size_t(i)] - n[size_t((i + st.len - notch) % st.len)]);
    // the decay: an open string rings for seconds (the low ones longest), a muted one thuds
    const float t60 = palm ? 0.10f + 0.12f * dyn : juce::jlimit(1.5f, 8.0f, 9.0f - st.hz / 40.0f);
    st.s = palm ? 0.45f : 0.22f;
    const double w = juce::MathConstants<double>::twoPi * st.hz / sr_;
    const double hmag = std::sqrt(juce::square(1.0 - st.s) + juce::square(double(st.s)) + 2 * st.s * (1 - st.s) * std::cos(w));
    st.g = float(juce::jmin(0.99999, std::pow(10.0, -3.0 / (t60 * st.hz)) / hmag));
    st.gOff = float(std::pow(10.0, -3.0 / (0.07 * st.hz)) / hmag); // a fretting hand lets go: 70 ms
    st.held = true;
    st.age = 0;
    st.env = 1;
}

void InternalSynth::startGuitar(int note, float velocity)
{
    const bool palm = velocity < 0.5f;
    double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    for (int side = 0; side < 2; ++side)
    {
        // pure fifths: over a root struck with it (or just before), this string takes 3:2 (or 3:1) of it
        for (auto& o : strings_)
            if (o.note >= 0 && o.held && o.side == side && o.age < int64_t(0.03 * sr_) && (note - o.note == 7 || note - o.note == 19))
                hz = o.hz * (note - o.note == 7 ? 1.5 : 3.0);
        Str* st = nullptr;
        for (auto& o : strings_) if (o.note == note && o.side == side) { st = &o; break; }  // the same string, picked again
        // the second take re-picking fast (a tremolo, under 110 ms since its last pick): 1 to 7 ms behind the first,
        // a different amount each stroke, as a second player's hand is (2026-11-23, Kerem: "not like a machine")
        if (st && side == 1 && st->age < int64_t(0.11 * sr_))
        {
            seed_ = seed_ * 1664525u + 1013904223u;
            st->repick = 1 + int((0.001 + 0.006 * double(seed_ >> 8) / 16777216.0) * sr_);
            st->repickVel = velocity;
            st->repickPalm = palm;
            continue;
        }
        if (!st) for (auto& o : strings_) if (o.note < 0) { st = &o; break; }
        if (!st) { st = &strings_[0]; for (auto& o : strings_) if (o.age > st->age) st = &o; } // steal the oldest
        if (st->note != note || st->side != side)
        {
            std::fill(st->buf.begin(), st->buf.end(), 0.0f);
            st->apX = st->apY = st->lpX = st->dcX = st->dcY = 0;
            st->w = 0;
            // the delay: the loop's length, plus the lowpass's (s) and a fractional allpass for the rest
            const double N = sr_ / hz;
            st->hz = float(hz);
            st->s = palm ? 0.45f : 0.22f;
            st->len = juce::jlimit(2, int(st->buf.size()) - 1, int(N - st->s - 0.1));
            const double d = N - st->s - st->len;
            st->a = float((1 - d) / (1 + d));
            st->note = note;
            st->side = side;
            // the second take, a little late: 6 to 18 ms, never the same twice
            seed_ = seed_ * 1664525u + 1013904223u;
            st->wait = side == 0 ? 0 : int((0.006 + 0.012 * double(seed_ >> 8) / 16777216.0) * sr_);
        }
        pluck(*st, velocity, palm, side == 0 ? 0.13f : 0.21f);
    }
}

void InternalSynth::runGuitar(float* l, float* r, int from, int to)
{
    for (auto& st : strings_)
    {
        if (st.note < 0) continue;
        float g = st.held ? st.g : st.gOff;
        const float pl = st.side == 0 ? 0.88f : 0.12f, pr = 1 - pl;
        float peak = 0;
        for (int n = from; n < to; ++n)
        {
            if (st.wait > 0) { --st.wait; continue; }
            if (st.repick > 0 && --st.repick == 0)
            {
                const bool held = st.held; // a note-off that came first still stands
                pluck(st, st.repickVel, st.repickPalm, 0.21f);
                st.held = held;
                g = held ? st.g : st.gOff;
            }
            const float out = st.buf[size_t(st.w)];
            const float lp = (1 - st.s) * out + st.s * st.lpX;
            st.lpX = out;
            const float ap = st.a * lp + st.apX - st.a * st.apY;
            st.apX = lp;
            st.apY = ap;
            st.buf[size_t(st.w)] = g * ap;
            if (++st.w >= st.len) st.w = 0;
            st.dcY = out - st.dcX + 0.995f * st.dcY;      // no DC into the amp
            st.dcX = out;
            l[n] += st.dcY * pl;
            r[n] += st.dcY * pr;
            peak = juce::jmax(peak, std::abs(out));
            ++st.age;
        }
        st.env = peak;
        if (st.age > int64_t(0.05 * sr_) && peak < 1e-5f) st.note = -1;
    }
}

void InternalSynth::startModal(Voice& v, int note, float velocity)
{
    const auto& m = *material_;
    const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    v.modes = 0;
    v.maxTau = 0;
    for (int i = 0; i < m.modes; ++i)
    {
        const double f = hz * m.ratios[size_t(i)];
        if (f > 0.45 * sr_) break;
        const double tau = m.tau * (0.6 + 0.8 * velocity) / (1 + m.damp * (m.ratios[size_t(i)] - 1));
        const double rr = std::exp(-1.0 / (tau * sr_)), w = juce::MathConstants<double>::twoPi * f / sr_;
        const size_t k = size_t(v.modes++);
        v.c1[k] = float(2 * rr * std::cos(w));
        v.c2[k] = float(-rr * rr);
        // a harder hit reaches the upper modes: the higher, the more velocity it takes
        v.gin[k] = float(std::sin(w) / (1 + 0.7 * i) * (i == 0 ? 1.0 : 0.35 + 0.65 * velocity));
        v.y1[k] = v.y2[k] = 0;
        v.maxTau = juce::jmax(v.maxTau, float(tau));
    }
    v.gain = 0.6 * std::pow(double(velocity), 1.2);
    v.held = m.excite == Scrape || m.excite == Crackle;
    v.exc = m.excite == Scrape ? 0.0f : 1.0f;
    v.excTarget = 1;
    v.impGap = float(0.09 * sr_);
    v.nextImp = 0;
    v.decay = std::exp(-1.0 / (juce::jmax(0.0002, m.exciteMs * 0.001 * (1.3 - 0.8 * velocity)) * sr_));
    v.release = std::exp(-1.0 / (0.08 * sr_));
    const float pan = (flip_++ % 3 - 1) * 0.15f; // a little to each side in turn, as hands move
    v.panL = std::sqrt(0.5f * (1 - pan));
    v.panR = std::sqrt(0.5f * (1 + pan));
}

void InternalSynth::runModal(Voice& v, float* l, float* r, int from, int to)
{
    const auto& m = *material_;
    for (int n = from; n < to; ++n)
    {
        seed_ = seed_ * 1664525u + 1013904223u;
        const float w = float(int32_t(seed_)) * (1.0f / 2147483648.0f);
        float e = 0;
        switch (m.excite)
        {
            case Strike: e = w * v.exc; v.exc *= float(v.decay); break;
            case Scrape: // a noise that swells and wavers while held: a bow, a brush, a scrape
                if ((v.age & 2047) == 0) v.excTarget = 0.25f + 0.75f * std::abs(w);
                if (v.held) v.exc += (v.excTarget - v.exc) * 0.0005f; else v.exc *= float(v.release);
                e = w * v.exc * 0.12f;
                break;
            case Rattle: // bouncing: impulses ever closer together and softer
                if (float(v.age) >= v.nextImp && v.impGap > 0.004f * float(sr_))
                {
                    e = v.exc * (0.6f + 0.4f * std::abs(w));
                    v.exc *= 0.82f;
                    v.nextImp += v.impGap;
                    v.impGap *= 0.8f;
                }
                break;
            case Crackle: // dust on a record: sparse random clicks while held
                if (v.held && std::abs(w) > 0.9994f) e = w;
                break;
        }
        float y = e * m.slap * 0.3f;
        for (int k = 0; k < v.modes; ++k)
        {
            const float yk = v.gin[size_t(k)] * e + v.c1[size_t(k)] * v.y1[size_t(k)] + v.c2[size_t(k)] * v.y2[size_t(k)];
            v.y2[size_t(k)] = v.y1[size_t(k)];
            v.y1[size_t(k)] = yk;
            y += yk;
        }
        y *= float(v.gain);
        l[n] += y * v.panL;
        r[n] += y * v.panR;
        ++v.age;
    }
    const bool rung = double(v.age) > (6.0 * v.maxTau + (m.excite == Rattle ? 0.6 : 0.0)) * sr_;
    if (!v.held && rung && (m.excite != Scrape || v.exc < 1e-4f)) v.note = -1;
}

void InternalSynth::start(int note, float velocity)
{
    if (kind_ == None) return; // the silent pulse: its notes only duck the mix
    if (kind_ == Guitar) { startGuitar(note, velocity); return; }
    if (kind_ == Modal)
    {
        Voice* v = &voices_[0];
        for (auto& x : voices_) if (x.note < 0) { v = &x; break; }
        v->note = note;
        v->age = 0;
        startModal(*v, note, velocity);
        return;
    }
    if (kind_ == Bass808)
        for (auto& x : voices_)
            if (x.note >= 0 && x.held) // the slide
            {
                x.note = note;
                x.target = juce::MathConstants<double>::twoPi * 440.0 * std::pow(2.0, (note - 69) / 12.0) / sr_;
                // 2026-11-11 (Kerem: Ecstatic part 5 "something in bass is out of tune"): the slide was an exponential
                // approach (tau 50 ms) that needed ~275 ms to come within 5 cents of an octave, a quarter of every note.
                // Now an even glide in pitch that lands exactly on the note after 50 ms.
                x.glideLeft = juce::jmax(1, int(0.05 * sr_));
                x.glideMul = std::pow(x.target / x.inc, 1.0 / x.glideLeft);
                return;
            }
    Voice* v = &voices_[0];
    for (auto& x : voices_) if (x.note < 0) { v = &x; break; }       // a free voice, or steal the first
    v->note = note;
    v->phase = 0;
    v->held = kind_ == Sub || kind_ == Bass808;
    v->noise = (kind_ == Click && note >= 96) || kind_ == Hat || kind_ == Clap || kind_ == Snare;
    v->age = 0;
    v->lastNoise = v->lastNoise2 = 0;
    const double hz = 440.0 * std::pow(2.0, ((kind_ == Click ? note + 36 : note) - 69) / 12.0);
    v->inc = juce::MathConstants<double>::twoPi * std::min(hz, 15000.0) / sr_;
    v->gain = 0.5 * std::pow(double(velocity), 1.3);
    double tau = 0.05;
    if (kind_ == Click) tau = v->noise ? 0.0015 : 0.002 + 0.004 * juce::jlimit(0.0, 1.0, (96.0 - note) / 40.0);
    if (kind_ == Blip) tau = 0.03 + 0.06 * velocity;
    if (kind_ == Kick) tau = 0.16 + 0.2 * velocity;                         // the kick's body, ending on the note
    if (kind_ == Snare) tau = note >= 60 ? 0.025 : 0.09 + 0.08 * velocity;  // the rim, or the snare's rattle
    if (kind_ == Hat) tau = note >= 46 ? 0.07 + 0.1 * velocity : 0.012 + 0.02 * velocity; // 46 open, else closed
    if (kind_ == Metal) tau = 0.05 + 0.25 * velocity;   // struck metal, tuned to the note
    if (kind_ == Zap) tau = 0.04 + 0.08 * velocity;     // a laser: a sine diving onto the note
    if (kind_ == Deep) tau = 0.45 + 0.9 * velocity;     // a struck plate the size of a door
    if (kind_ == Boom) tau = 0.7 + 0.8 * velocity;      // the drum rings long; harder, longer
    if (kind_ == Bass808) tau = 2.5;                    // while held the 808 fades slowly, as the real one does
    v->decay = std::exp(-1.0 / (tau * sr_));
    v->phase2 = v->phase3 = 0;
    v->target = v->inc;
    v->glideLeft = 0;
    const bool struck = kind_ != Sub && kind_ != Blip && kind_ != Bass808;
    v->attack = struck ? 1.0 : 1.0 / ((kind_ == Sub ? 0.04 : kind_ == Bass808 ? 0.002 : 0.001) * sr_);
    v->release = std::exp(-1.0 / ((kind_ == Bass808 ? 0.09 : 0.4) * sr_));
    v->env = struck ? v->gain : 0.0;
    // stereo: clicks and hats alternate a little left and right, blips spread by pitch, the rest sits centre
    float pan = kind_ == Click || kind_ == Hat ? ((flip_++ & 1) ? 0.3f : -0.3f) : kind_ == Blip ? juce::jlimit(-0.5f, 0.5f, float(note - 88) / 24.0f) : 0.0f;
    v->panL = std::sqrt(0.5f * (1 - pan));
    v->panR = std::sqrt(0.5f * (1 + pan));
}

void InternalSynth::stopNote(int note)
{
    for (auto& v : voices_) if (v.note == note && v.held) v.held = false;
    for (auto& st : strings_) if (st.note == note) st.held = false;
}

void InternalSynth::run(float* l, float* r, int from, int to)
{
    if (kind_ == Guitar) { runGuitar(l, r, from, to); return; }
    for (auto& v : voices_)
    {
        if (v.note < 0) continue;
        if (kind_ == Modal) { runModal(v, l, r, from, to); continue; }
        for (int n = from; n < to; ++n)
        {
            if (kind_ == Bass808)
            {
                // the attack in 2 ms, then a slow fade while held; the pitch punches in from a fifth up
                // in 12 ms, and glides to a new note in 50 ms
                if (v.held) { if (v.attack > 0) { v.env += v.gain * v.attack; if (v.env >= v.gain) { v.env = v.gain; v.attack = 0; } } else v.env *= v.decay; }
                else v.env *= v.release;
                if (v.glideLeft > 0) v.inc = --v.glideLeft == 0 ? v.target : v.inc * v.glideMul;
                v.phase += v.inc * (1 + 0.5 * std::exp(-double(v.age) / (0.012 * sr_)));
                if (v.phase > juce::MathConstants<double>::twoPi) v.phase -= juce::MathConstants<double>::twoPi;
                ++v.age;
                const float y = float(std::tanh(2.2 * std::sin(v.phase)) * 0.62 * v.env);
                l[n] += y * v.panL;
                r[n] += y * v.panR;
                continue;
            }
            if (kind_ == Click || kind_ == Kick || kind_ == Hat || kind_ == Snare || kind_ >= Metal) v.env *= v.decay;
            else if (kind_ == Clap)
            {
                // three hand claps 11 ms apart, then the room
                const double t = double(v.age) / sr_;
                v.env = v.gain * (t < 0.033 ? std::exp(-std::fmod(t, 0.011) / 0.003) : 0.7 * std::exp(-(t - 0.033) / 0.09));
            }
            else if (kind_ == Blip) { if (v.attack > 0 && v.env < v.gain) { v.env += v.gain * v.attack; if (v.env >= v.gain) { v.env = v.gain; v.attack = 0; } } else v.env *= v.decay; }
            else { if (v.held) v.env += (v.gain - v.env) * v.attack * 3.0; else v.env *= v.release; }
            float x;
            if (v.noise)
            {
                seed_ = seed_ * 1664525u + 1013904223u;
                const float w = float(int32_t(seed_)) * (1.0f / 2147483648.0f);
                if (kind_ == Hat) x = (w - 2 * v.lastNoise + v.lastNoise2) * 0.35f; // twice differenced: only the top
                else if (kind_ == Snare)
                {
                    // the body: a tone falling an octave in 10 ms, gone in 40 (the rim: higher, at once)
                    const double age = double(v.age) / sr_;
                    v.phase += v.inc * std::exp(-age / 0.01);
                    const float body = float(std::sin(v.phase) * std::exp(-age / (v.note >= 60 ? 0.008 : 0.04)) * v.gain / std::max(v.env, 1e-9));
                    x = (w - v.lastNoise) * 0.45f + body * 0.8f;
                }
                else x = (w - v.lastNoise) * 0.7f; // differenced noise: the low end removed, a tick
                v.lastNoise2 = v.lastNoise;
                v.lastNoise = w;
            }
            else
                x = float(std::sin(v.phase) + (kind_ == Sub ? 0.12 * std::sin(2 * v.phase) : 0.0));
            if (kind_ >= Metal)
            {
                const double age = double(v.age) / sr_;
                double y;
                if (kind_ == Metal)
                {
                    // FM: two inharmonic modulators (1.41 and 3.73 times the note), their brightness gone first
                    v.phase += v.inc;
                    v.phase2 += v.inc * 1.4142;
                    v.phase3 += v.inc * 3.7321;
                    const double index = 4.0 * std::exp(-age / 0.03);
                    y = std::sin(v.phase + index * std::sin(v.phase2) + 0.6 * index * std::sin(v.phase3)) * v.env;
                }
                else if (kind_ == Deep)
                {
                    // low struck metal: modulators at 1.41 and 2.45 times the note, the index fading over
                    // a quarter second and the pitch settling a little, so it rings dark for a long time
                    v.phase += v.inc * (1 + 0.04 * std::exp(-age / 0.08));
                    v.phase2 += v.inc * 1.4142;
                    v.phase3 += v.inc * 2.4495;
                    const double index = 2.6 * std::exp(-age / 0.25) + 0.35;
                    y = std::sin(v.phase + index * std::sin(v.phase2) + 0.5 * index * std::sin(v.phase3)) * v.env;
                }
                else if (kind_ == Boom)
                {
                    // the skin's modes: the fundamental, falling from a fourth above, and two higher ones dying first
                    const double drop = 1 + 0.335 * std::exp(-age / 0.04);
                    v.phase += v.inc * drop;
                    v.phase2 += v.inc * 1.593 * drop;
                    v.phase3 += v.inc * 2.136 * drop;
                    // the mallet: low-passed noise for the first few milliseconds
                    seed_ = seed_ * 1664525u + 1013904223u;
                    v.lastNoise += (float(int32_t(seed_)) * (1.0f / 2147483648.0f) - v.lastNoise) * 0.06f;
                    const double env = v.env / std::max(v.gain, 1e-9);
                    const double x = std::sin(v.phase) + 0.35 * std::sin(v.phase2) * std::exp(-age / 0.18) + 0.18 * std::sin(v.phase3) * std::exp(-age / 0.09)
                                   + 6.0 * v.lastNoise * std::exp(-age / 0.006);
                    y = std::tanh(1.4 * x * env) / std::tanh(1.4) * v.gain * 0.9;
                }
                else
                {
                    v.phase += v.inc * (1 + 15 * std::exp(-age / 0.012));
                    y = std::sin(v.phase) * v.env;
                }
                v.phase = std::fmod(v.phase, juce::MathConstants<double>::twoPi);
                v.phase2 = std::fmod(v.phase2, juce::MathConstants<double>::twoPi);
                v.phase3 = std::fmod(v.phase3, juce::MathConstants<double>::twoPi);
                ++v.age;
                l[n] += float(y) * v.panL;
                r[n] += float(y) * v.panR;
                continue;
            }
            if (kind_ == Kick)
            {
                // the pitch falls from two octaves above the note in a few milliseconds, then saturates a little
                v.phase += v.inc * 3.0 * std::exp(-double(v.age) / (0.022 * sr_));
                x = float(std::tanh(2.0 * x * v.env) * 0.55);
                v.phase += v.inc;
                if (v.phase > juce::MathConstants<double>::twoPi) v.phase -= juce::MathConstants<double>::twoPi;
                ++v.age;
                l[n] += x * v.panL;
                r[n] += x * v.panR;
                continue;
            }
            ++v.age;
            v.phase += v.inc;
            if (v.phase > juce::MathConstants<double>::twoPi) v.phase -= juce::MathConstants<double>::twoPi;
            x *= float(v.env);
            l[n] += x * v.panL;
            r[n] += x * v.panR;
        }
        const bool done = kind_ == Sub || kind_ == Bass808 ? (!v.held && v.env < 1e-5) : kind_ == Blip ? (v.attack == 0 && v.env < 1e-5) : v.env < 1e-5;
        if (done) v.note = -1;
    }
}

void InternalSynth::render(const juce::MidiBuffer& midi, float* l, float* r, int numSamples)
{
    int at = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit(0, numSamples, meta.samplePosition);
        run(l, r, at, pos);
        at = pos;
        const auto m = meta.getMessage();
        if (m.isNoteOn()) start(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) stopNote(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    run(l, r, at, numSamples);
}

// ---------------------------------------------------------------- the master FX rack
void MasterFx::prepare(double sr, int maxBlock)
{
    sr_ = sr;
    glide_ = 1.0f - std::exp(-1.0f / float(0.03 * sr)); // a knob's move glides over about 30 ms
    toneA_ = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 900.0f / float(sr));
    lowA_ = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 160.0f / float(sr));
    envA_ = 1.0f - std::exp(-1.0f / float(0.3 * sr));
    for (int c = 0; c < 2; ++c)
    {
        push_[c].peak(sr, 900, 0.7, 9);   // the mid hump an overdrive or an amp is driven through
        ampTight_[c].highpass(sr, 80);
        ampInter_[c].highpass(2 * sr, 30); // between the amp's stages, at the doubled rate
        cab_[c].lowpass(sr, 5200, 0.9);    // the speaker's top roll-off
        last_[c] = toneLp_[c] = dcIn_[c] = dcOut_[c] = ic1_[c] = ic2_[c] = lowPre_[c] = lowPost_[c] = 0;
    }
    delay_.prepare(sr);
    timeIndex_ = -1;
    bpm_ = 0;
    echoL_.assign(size_t(maxBlock), 0.0f);
    echoR_.assign(size_t(maxBlock), 0.0f);
    envIn_ = envOut_ = 1e-6f;
    for (auto& ch : eq_) for (auto& f : ch) f = {};
    for (auto& d : eqDb_) d = 0;
}

float MasterFx::shape(float x, int style, float k, int c)
{
    switch (style)
    {
        case 0: return std::tanh(k * x);                                  // tape: round and symmetric
        case 1: return std::tanh(k * x + 0.3f) - std::tanh(0.3f);         // tube: lopsided, even harmonics
        case 2: return 0.6366f * std::atan(k * x);                        // overdrive: a long soft knee
        case 3: x = ampInter_[c].process(std::tanh(k * x));               // amp: two stages, the second lopsided
                return std::tanh(3 * x + 0.25f) - std::tanh(0.25f);
        default: return juce::jlimit(-0.6f, 0.8f, 3 * k * x);             // fuzz: clipped flat, unevenly
    }
}

void MasterFx::process(float* l, float* r, int n, double bpm, const float* addL, const float* addR, float addGain)
{
    // ponytail: the shapers run at twice the rate by midpoint only, so the fuzz aliases a little; oversample properly if it fizzes
    const int style = juce::jlimit(0, 4, int(p[Style].load()));
    const float driveT = p[Drive], lowT = p[Low], toneT = p[Tone], outT = juce::Decibels::decibelsToGain(p[Output].load()),
                driveWetT = p[DriveOn] > 0.5f ? p[DriveMix].load() : 0.0f, cutT = juce::jlimit(20.0f, 4000.0f, p[LowCut].load()),
                sendT = p[DelayOn] > 0.5f ? 1.0f : 0.0f, delayMixT = p[DelayMix];

    // the delay: its time a bar division of the song's tempo (a new time jumps; the feedback glides)
    if (const int ti = juce::jlimit(0, 6, int(p[Time].load())); ti != timeIndex_ || bpm != bpm_)
    {
        timeIndex_ = ti;
        bpm_ = bpm;
        delay_.set(timeBeats[ti] * 60.0 / bpm, p[Feedback]);
    }
    delay_.glideTo(p[Feedback], float(n / (0.5 * sr_)));

    for (int i = 0; i < n; ++i)
    {
        const float g = glide_;
        drive_ += (driveT - drive_) * g;  low_ += (lowT - low_) * g;  tone_ += (toneT - tone_) * g;
        driveWet_ += (driveWetT - driveWet_) * g;  out_ += (outT - out_) * g;  cutHz_ += (cutT - cutHz_) * g;  send_ += (sendT - send_) * g;
        float io[2] = { l[i], r[i] };

        if (driveWet_ > 1e-4f)
        {
            const float k = 1 + 40 * drive_ * drive_;      // up to +32 dB into the shaper
            const float lo = std::exp(-0.69f * tone_);     // tone tilts the result about 900 Hz, +-6 dB
            // the low push (Kerem: "good low frequency driving"): below 160 Hz up to +12 dB into the shaper, half of it
            // taken back after, so the bass is what saturates hardest and comes out thicker, not thinner
            const float push = juce::Decibels::decibelsToGain(12.0f * low_), back = 1 / std::sqrt(push);
            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                float x = io[c];
                if (style == 3) x = ampTight_[c].process(x);
                if (style >= 2 && style <= 3) x = push_[c].process(x);
                lowPre_[c] += (x - lowPre_[c]) * lowA_;
                x += lowPre_[c] * (push - 1);
                float y = 0.5f * (shape(0.5f * (last_[c] + x), style, k, c) + shape(x, style, k, c));
                last_[c] = x;
                if (style == 3) y = cab_[c].process(y);
                lowPost_[c] += (y - lowPost_[c]) * lowA_;
                y += lowPost_[c] * (back - 1);
                toneLp_[c] += (y - toneLp_[c]) * toneA_;
                y = toneLp_[c] * lo + (y - toneLp_[c]) / lo;
                const float o = y - dcIn_[c] + 0.9995f * dcOut_[c]; // the lopsided styles leave DC behind
                dcIn_[c] = y;
                dcOut_[c] = o;
                wet[c] = o;
            }
            // the level follows the dry mix's (slowly, so it still squashes like a driven tape); the output knob sits on top
            envIn_ += (0.5f * (io[0] * io[0] + io[1] * io[1]) - envIn_) * envA_;
            envOut_ += (0.5f * (wet[0] * wet[0] + wet[1] * wet[1]) - envOut_) * envA_;
            const float match = juce::jlimit(0.25f, 4.0f, std::sqrt((envIn_ + 1e-9f) / (envOut_ + 1e-9f)));
            for (int c = 0; c < 2; ++c) io[c] += (wet[c] * match * out_ - io[c]) * driveWet_;
        }

        l[i] = io[0];
        r[i] = io[1];
        // the delay's send, high-passed (12 dB/oct) at the low cut: the low end is never echoed
        const float G = std::tan(juce::MathConstants<float>::pi * cutHz_ / float(sr_)), K = 1.4142f;
        const float a1 = 1 / (1 + G * (G + K)), a2 = G * a1, a3 = G * a2;
        float sent[2];
        for (int c = 0; c < 2; ++c)
        {
            const float v3 = io[c] - ic2_[c], v1 = a1 * ic1_[c] + a2 * v3, v2 = ic2_[c] + a2 * ic1_[c] + a3 * v3;
            ic1_[c] = 2 * v1 - ic1_[c];
            ic2_[c] = 2 * v2 - ic2_[c];
            sent[c] = (io[c] - K * v1 - v2) * send_;
        }
        echoL_[size_t(i)] = sent[0];
        echoR_[size_t(i)] = sent[1];
    }

    delay_.process(echoL_.data(), echoR_.data(), n); // only the echoes come back
    for (int i = 0; i < n; ++i)
    {
        delayMix_ += (delayMixT - delayMix_) * glide_;
        l[i] += echoL_[size_t(i)] * delayMix_;
        r[i] += echoR_[size_t(i)] * delayMix_;
    }

    if (addL) { juce::FloatVectorOperations::addWithMultiply(l, addL, addGain, n); juce::FloatVectorOperations::addWithMultiply(r, addR, addGain, n); }

    // the EQ: each band glides a block at a time (about 30 ms across), flat when off; a flat band is passed by
    const bool eqOn = p[EqOn] > 0.5f;
    const float step = juce::jmin(1.0f, glide_ * float(n));
    for (int b = 0; b < 4; ++b)
    {
        const float target = eqOn ? juce::jlimit(-12.0f, 12.0f, p[EqLow + b].load()) : 0.0f;
        if (std::abs(target - eqDb_[b]) < 0.01f && std::abs(eqDb_[b]) < 0.01f) { eqDb_[b] = target; continue; }
        eqDb_[b] += (target - eqDb_[b]) * step;
        Biquad t;
        if (b == 0 || b == 3) t.shelf(sr_, eqHz[b], eqDb_[b], b == 3);
        else t.peak(sr_, eqHz[b], 0.9, eqDb_[b]);
        for (auto& ch : eq_) ch[b].retune(t);
        for (int i = 0; i < n; ++i) { l[i] = eq_[0][b].process(l[i]); r[i] = eq_[1][b].process(r[i]); }
    }
}

// ---------------------------------------------------------------- the ears' guard
void EarGuard::prepare(double sr)
{
    sr_ = sr;
    hp_.highpass(sr, 2000);
    lp_.lowpass(sr, 6000);
    for (int c = 0; c < 2; ++c)
    {
        bell_[c].peak(sr, 3500, 0.45, 0);
        top_[c][0].lowpass(sr, juce::jmin(16000.0, 0.45 * sr), 0.5412); // Butterworth, fourth order
        top_[c][1].lowpass(sr, juce::jmin(16000.0, 0.45 * sr), 1.3066);
    }
    eBand_ = eAll_ = cutDb = 0;
    a_ = 1.0f - std::exp(-1.0f / float(0.05 * sr)); // 50 ms: the level a listener's ear integrates
}

void EarGuard::process(float* l, float* r, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]), b = lp_.process(hp_.process(m));
        eBand_ += (b * b - eBand_) * a_;
        eAll_ += (m * m - eAll_) * a_;
    }
    // how far the band stands out, in dB of the whole; the cut follows in 5 ms, lets go over half a second
    const float over = eAll_ > 1e-9f ? 10.0f * std::log10(juce::jmax(1e-12f, eBand_) / eAll_) - threshold : -1.0f;
    const float want = juce::jlimit(0.0f, 8.0f, 2.0f * over); // two dB down for each it stands out: a peak comes back under
    const float dt = float(n / sr_);
    cutDb += (want - cutDb) * (want > cutDb ? juce::jmin(1.0f, dt / 0.005f) : juce::jmin(1.0f, dt / 0.5f));
    Biquad t;
    t.peak(sr_, 3500, 0.45, -cutDb); // wide: the whole 2-6 kHz band
    for (auto& b : bell_) b.retune(t);
    for (int i = 0; i < n; ++i)
    {
        l[i] = top_[0][1].process(top_[0][0].process(bell_[0].process(l[i])));
        r[i] = top_[1][1].process(top_[1][0].process(bell_[1].process(r[i])));
    }
}

// ---------------------------------------------------------------- one track's echo
void FeedbackDelay::prepare(double sr)
{
    l_.assign(size_t(sr * 4.5) + 8, 0.0f);   // up to a whole bar at 60 bpm
    r_.assign(l_.size(), 0.0f);
    w_ = 0;
    lpA_ = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 4500.0f / float(sr));
    hpA_ = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 140.0f / float(sr));
    lpL_ = lpR_ = hpL_ = hpR_ = 0;
}

void FeedbackDelay::set(double seconds, float feedback, float mix, bool throwsOnly)
{
    throwsOnly_ = throwsOnly;
    send_ = throwsOnly ? 0.0f : 1.0f;
    sendStep_ = 1.0f / 96.0f; // the send opens and closes over 2 ms: no click on a throw
    d_ = juce::jlimit(1, int(l_.size()) - 1, int(std::lround(seconds * (double(l_.size() - 8) / 4.5))));
    fb_ = juce::jlimit(0.0f, 0.95f, feedback);
    mix_ = juce::jlimit(0.0f, 1.0f, mix);
}

void FeedbackDelay::process(float* left, float* right, int n)
{
    const int size = int(l_.size());
    for (int i = 0; i < n; ++i)
    {
        const int rd = (w_ - d_ + size) % size;
        const float el = l_[size_t(rd)], er = r_[size_t(rd)];
        // darker and thinner every time round
        lpL_ += (er - lpL_) * lpA_;  lpR_ += (el - lpR_) * lpA_;
        hpL_ += (lpL_ - hpL_) * hpA_; hpR_ += (lpR_ - hpR_) * hpA_;
        const float backL = lpL_ - hpL_, backR = lpR_ - hpR_;
        if (throwsOnly_)
        {
            const bool open = throwLeft_ > 0 && i >= throwAt_;
            if (open && --throwLeft_ == 0) throwAt_ = -1;
            send_ = juce::jlimit(0.0f, 1.0f, send_ + (open ? sendStep_ : -sendStep_));
        }
        const float in = 0.5f * (left[i] + right[i]) * send_;
        l_[size_t(w_)] = in + backL * fb_;   // ping-pong: left feeds right feeds left
        r_[size_t(w_)] = backR * fb_;
        left[i] += el * mix_;
        right[i] += er * mix_;
        w_ = (w_ + 1) % size;
    }
    if (throwAt_ >= 0) throwAt_ = 0; // a throw still open carries into the next block from its start
}

void FeedbackBus::prepare(double sr)
{
    sr_ = sr;
    l_.assign(size_t(sr * 4.5) + 8, 0.0f);
    r_.assign(l_.size(), 0.0f);
    w_ = 0;
    lpA_ = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 3500.0f / float(sr));
    hpA_ = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 70.0f / float(sr));
    lpL_ = lpR_ = hpL_ = hpR_ = 0;
    env_ = 0;
    envRel_ = std::exp(-1.0f / (0.5f * float(sr)));
}

void FeedbackBus::process(float* left, float* right, int n)
{
    const int size = int(l_.size());
    auto clip = [](float x) { return 0.5f * std::tanh(2.0f * x); }; // gain 1 when quiet, never past 0.5
    for (int i = 0; i < n; ++i)
    {
        const int rd = (w_ - d_ + size) % size;
        const float el = l_[size_t(rd)], er = r_[size_t(rd)];
        lpL_ += (er - lpL_) * lpA_;  lpR_ += (el - lpR_) * lpA_;
        hpL_ += (lpL_ - hpL_) * hpA_; hpR_ += (lpR_ - hpR_) * hpA_;
        // Kerem (2026-11-22): in Hollow Wake the feedback "loses the pitch, feedback at a noise": at 0.99 the loop sat in
        // the clip at every peak, and the clip's distortion, fed back again and again, became noise. A limiter in the
        // loop keeps it under the clip (0.2, where the tanh is still nearly straight): as long, but pitched.
        env_ = juce::jmax(std::abs(el), std::abs(er), env_ * envRel_);
        const float g = fb_ * (env_ > 0.2f ? 0.2f / env_ : 1.0f);
        l_[size_t(w_)] = 0.5f * (left[i] + right[i]) + clip(g * (lpL_ - hpL_)); // ping-pong: left feeds right feeds left
        r_[size_t(w_)] = clip(g * (lpR_ - hpR_));
        left[i] = el;
        right[i] = er;
        w_ = (w_ + 1) % size;
    }
}

} // namespace liszt
