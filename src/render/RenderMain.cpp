// liszt_render: the concert, offline and faster than real time.
//   liszt_render [--seed N] [--seconds S] [--out file.wav] [--solo <track of the scene>]
//                [--feedback 0..0.95]              every track's echo feeds back this much
//                [--patch bass="Basses/Bass 1"]  (repeatable; any Surge factory patch)
//                [--from Summit|<bar>] [--bars N]  start at a section or bar; render N bars + tail
//                [--dry]                           no delay or reverb
//                [--scene berlin|chorale]          which composer (and its palette, sounds/<scene>.txt)
//                [--palette berlin]                sounds/<name>.txt
//                                                             render a concert (+ .mid and .sections.txt)
//   liszt_render --audition "Category/Patch" [note] [--pure] hold one note on a Surge patch, measure it
//   liszt_render --probe <plugin.vst3>                        list what a plugin exposes
#include "Engine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <iostream>

using namespace liszt;

static float dbfs(double x) { return juce::Decibels::gainToDecibels(float(x), -150.0f); }

static int probe(const juce::String& path, const juce::String& filter = {}, const juce::String& patch = {})
{
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile(types, path);
    if (types.isEmpty()) { std::cerr << "no plugin in " << path << "\n"; return 1; }
    juce::String err;
    auto p = vst3.createInstanceFromDescription(*types[0], 48000.0, 512, err);
    if (!p) { std::cerr << "load failed: " << err << "\n"; return 1; }
    std::cout << p->getName() << ": " << p->getNumPrograms() << " programs, "
              << p->getParameters().size() << " parameters\n";
    if (patch.isNotEmpty()) // show the values a patch sets
        if (auto e = loadSurgePatch(*p, findSurgePatch(patch)); e.isNotEmpty()) { std::cerr << e << "\n"; return 1; }
    juce::StringArray words;
    words.addTokens(filter.isEmpty() ? juce::String("cutoff") : filter, ",", "");
    for (auto* prm : p->getParameters())
        if (p->getParameters().size() < 200 || std::any_of(words.begin(), words.end(), [&](auto& w) { return prm->getName(64).containsIgnoreCase(w); }))
            std::cout << "  " << prm->getParameterIndex() << " " << prm->getName(64) << " = " << prm->getValue()
                      << " (" << prm->getCurrentValueAsText() << ")\n";

    // A plugin with an "FX Type" switch (Surge XT Effects): walk every type and show what its
    // generic parameters mean there, by name and by value text.
    p->prepareToPlay(48000.0, 512);
    for (auto* type : p->getParameters())
    {
        if (type->getName(64) != "FX Type") continue;
        juce::String last;
        for (int s = 0; s <= 100; ++s) // the switch reports itself continuous, so sweep it
        {
            type->setValue(float(s) / 100.0f);
            if (type->getCurrentValueAsText() == last) continue;
            last = type->getCurrentValueAsText();
            juce::AudioBuffer<float> buf(juce::jmax(2, p->getTotalNumOutputChannels()), 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 4; ++b) p->processBlock(buf, midi); // let the type change take
            std::cout << "type " << s << " (" << type->getValue() << "): " << type->getCurrentValueAsText() << "\n   ";
            for (auto* prm : p->getParameters())
                if (prm != type && prm->getName(64) != "Bypass")
                    std::cout << " [" << prm->getName(64).fromLastOccurrenceOf(" ", false, false) << "] "
                              << p->getParameterName(prm->getParameterIndex()) << "=" << prm->getCurrentValueAsText();
            std::cout << "\n";
        }
    }
    return 0;
}

// Pitch of a window by autocorrelation; rough, but enough to catch a patch that bends or detunes.
static double pitchOf(const float* x, int n, double sr)
{
    const int lo = int(sr / 1000), hi = std::min(int(sr / 50), n / 2);
    std::vector<double> r(size_t(hi + 2), 0.0);
    double best = 0;
    for (int lag = lo; lag <= hi + 1; ++lag)
    {
        double num = 0, a = 0, b = 0;
        for (int i = 0; i + lag < n; ++i) { num += x[i] * x[i + lag]; a += x[i] * x[i]; b += x[i + lag] * x[i + lag]; }
        r[size_t(lag)] = num / std::sqrt(a * b + 1e-20);
        best = std::max(best, r[size_t(lag)]);
    }
    // The first peak close to the best one is the period; later peaks are its multiples.
    for (int lag = lo + 1; lag <= hi; ++lag)
        if (r[size_t(lag)] >= 0.85 * best && r[size_t(lag)] >= r[size_t(lag - 1)] && r[size_t(lag)] >= r[size_t(lag + 1)] && best > 0.5)
            return sr / lag;
    return 0;
}

// Spectral centroid of a stretch of audio: the "brightness". Lower is darker.
static double centroidOf(const std::vector<float>& x, size_t from, double sr)
{
    constexpr int order = 12, n = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> win(n), work(2 * n);
    juce::dsp::WindowingFunction<float>::fillWindowingTables(win.data(), n, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<double> mag(n / 2, 0.0);
    for (size_t at = from; at + n <= x.size(); at += n)
    {
        std::fill(work.begin(), work.end(), 0.0f);
        for (int i = 0; i < n; ++i) work[size_t(i)] = x[at + size_t(i)] * win[size_t(i)];
        fft.performFrequencyOnlyForwardTransform(work.data());
        for (int k = 0; k < n / 2; ++k) mag[size_t(k)] += work[size_t(k)];
    }
    double num = 0, den = 0;
    for (int k = 1; k < n / 2; ++k) { num += mag[size_t(k)] * k * sr / n; den += mag[size_t(k)]; }
    return den > 0 ? num / den : 0;
}

static int audition(const juce::String& patch, int note, bool pure)
{
    const double sr = 48000;
    const int block = 256;
    juce::String err;
    const bool dexed = isDexedSound(patch), sampled = isSfzSound(patch);
    setOfflineRendering(true);
    auto surge = sampled ? createSfz(sr, block, findSfz(patch), err) : dexed ? createDexed(sr, block, err) : createSurge(sr, block, err);
    if (!surge) { std::cerr << err << "\n"; return 1; }
    if (!sampled)
        if (auto e = dexed ? loadDexedVoice(*surge, patch) : loadSurgePatch(*surge, findSurgePatch(patch)); e.isNotEmpty()) { std::cerr << e << "\n"; return 1; }
    if (pure && !dexed && !sampled) purify(*surge);
    surge->prepareToPlay(sr, block);

    const int holdBlocks = int(3.0 * sr / block), tailBlocks = int(2.0 * sr / block);
    juce::AudioBuffer<float> buf(juce::jmax(2, surge->getTotalNumOutputChannels()), block);
    std::vector<float> hold, tail;
    for (int b = 0; b < holdBlocks + tailBlocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        if (b == holdBlocks) midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        buf.clear();
        surge->processBlock(buf, midi);
        auto& dst = b < holdBlocks ? hold : tail;
        dst.insert(dst.end(), buf.getReadPointer(0), buf.getReadPointer(0) + block);
    }

    double peak = 0, sq = 0;
    for (float v : hold) { peak = std::max(peak, double(std::abs(v))); sq += v * v; }
    std::vector<double> cents;
    for (size_t at = size_t(0.5 * sr); at + 2048 < hold.size(); at += size_t(0.05 * sr))
        if (double f = pitchOf(hold.data() + at, 2048, sr); f > 0)
            cents.push_back(1200 * std::log2(f / (440.0 * std::pow(2.0, (note - 69) / 12.0))));
    std::sort(cents.begin(), cents.end());
    double tailSq = 0;
    for (size_t i = tail.size() - size_t(0.1 * sr); i < tail.size(); ++i) tailSq += tail[i] * tail[i];

    std::cout << patch << ": peak " << dbfs(peak) << " dBFS, rms " << dbfs(std::sqrt(sq / hold.size())) << " dBFS";
    if (!cents.empty())
        std::cout << ", centroid " << juce::roundToInt(centroidOf(hold, size_t(0.5 * sr), sr)) << " Hz, pitch vs note " << juce::roundToInt(cents[cents.size() / 2]) << " cents (spread "
                  << juce::roundToInt(cents.back() - cents.front()) << ")";
    else
        std::cout << ", no stable pitch";
    // Beating: detuned copies of a note make its loudness waver. The spread of a held note's level
    // in 10 ms windows (1 to 2.8 s, after the attack), as a percentage of its mean.
    std::vector<double> env;
    for (size_t at = size_t(1.0 * sr); at + size_t(0.01 * sr) < hold.size() && at < size_t(2.8 * sr); at += size_t(0.01 * sr))
    {
        double e = 0;
        for (size_t i = at; i < at + size_t(0.01 * sr); ++i) e += hold[i] * hold[i];
        env.push_back(std::sqrt(e / (0.01 * sr)));
    }
    double mean = 0, var = 0;
    for (double e : env) mean += e;
    mean /= double(std::max<size_t>(1, env.size()));
    for (double e : env) var += (e - mean) * (e - mean);
    var /= double(std::max<size_t>(1, env.size()));
    std::cout << ", beating " << juce::roundToInt(mean > 0 ? 100 * std::sqrt(var) / mean : 0) << " %";
    std::cout << ", tail after 2s " << dbfs(std::sqrt(tailSq / (0.1 * sr))) << " dBFS\n";
    return 0;
}

static std::string sceneName = "berlin"; // --scene berlin|chorale

static void writeMidi(uint64_t seed, const juce::File& file)
{
    auto cp = makeComposer(sceneName, seed);
    auto& c = *cp;
    std::vector<Event> ev;
    for (int b = 0; b < c.totalBars(); ++b) c.generateBar(b, ev);
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kPPQ);
    juce::MidiMessageSequence tempo;
    tempo.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / c.bpm())), 0);
    mf.addTrack(tempo);
    const auto& info = sceneInfo(sceneName);
    for (int t = 0; t < info.numTracks; ++t)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::textMetaEvent(3, info.trackNames[t]), 0);
        const int ch = t % 16 + 1;
        for (auto& e : ev)
            if (e.track == t && e.type == Event::NoteOn) seq.addEvent(juce::MidiMessage::noteOn(ch, e.pitch, e.value), double(e.tick));
            else if (e.track == t && e.type == Event::NoteOff) seq.addEvent(juce::MidiMessage::noteOff(ch, e.pitch), double(e.tick));
        seq.sort();
        mf.addTrack(seq);
    }
    juce::FileOutputStream os(file);
    if (os.openedOk()) { os.setPosition(0); os.truncate(); mf.writeTo(os); }
}

// What the visuals follow, for a render: sections, every note, and each part's level per
// audio block. The live app will send the same over a socket.
static void writeShow(uint64_t seed, double blockRate, const std::vector<std::array<float, 7>>& levels, const juce::File& file)
{
    auto cp = makeComposer(sceneName, seed);
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
    j << "],\"notes\":["; // [time s, duration s, role, pitch, velocity]: the visuals read roles, not tracks
    const auto& info = sceneInfo(sceneName);
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
              << info.role[e.track] << "," << int(e.pitch) << "," << num(velAt[e.track][e.pitch]) << "]";
            first = false;
            onAt[e.track][e.pitch] = -1;
        }
    }
    j << "],\"levels\":["; // RMS per audio block, linear
    for (size_t i = 0; i < levels.size(); ++i)
    {
        j << (i ? ",[" : "[");
        for (int k = 0; k < 7; ++k) j << (k ? "," : "") << num(levels[i][size_t(k)]);
        j << "]";
    }
    j << "]}\n";
    file.replaceWithText(j.toString());
}

static juce::String paletteName = "berlin"; // --palette <name>: sounds/<name>.txt
static float feedbackOverride = -1;            // --feedback 0..0.95: every track's echo
static bool gRaw = false;                      // --raw: without the mix stage (ducking, low cuts, width, glue)

static int renderConcert(uint64_t seed, double seconds, const juce::File& out, int solo, const juce::StringArray& patches, bool dry,
                         const juce::String& from, int bars)
{
    const double sr = 48000;
    const int block = 512;
    Palette palette;
    if (auto e = palette.load(findPalette(paletteName), sceneName); e.isNotEmpty()) { std::cerr << e << "\n"; return 1; }
    const auto& info = *palette.info;
    for (auto& p : patches) // "bass=Templates/Init Square": that part plays only this patch
        if (int t = info.trackOf(p.upToFirstOccurrenceOf("=", false, false).toStdString()); t >= 0)
            palette.parts[t] = { { p.fromFirstOccurrenceOf("=", false, false), 0.0f, 0.0f } };
    Engine engine;
    engine.scene = sceneName;
    if (auto e = engine.load(sr, block, palette); e.isNotEmpty()) { std::cerr << e << "\n"; return 1; }
    if (solo >= 0) engine.enabledTracks = uint16_t(1 << solo);
    engine.feedbackOverride = feedbackOverride;
    engine.effects = !dry;
    engine.mixStage = !gRaw;
    int startBar = 0;
    {
        auto planned = makeComposer(sceneName, seed);
        auto& plan = *planned;
        for (auto& s : plan.sections())
            if (from.equalsIgnoreCase(s.name)) startBar = s.startBar;
        if (from.isNotEmpty() && from.containsOnly("0123456789")) startBar = from.getIntValue() - 1; // bars count from 1
        if (bars > 0) seconds = bars * 4 * 60.0 / plan.bpm() + 4.0; // plus a tail for the delay and reverb
    }
    engine.start(seed, startBar);
    auto* c = engine.composer();
    std::cout << "seed " << seed << ": " << c->describe() << "\n";
    juce::String sections = "bpm " + juce::String(c->bpm()) + "\n";
    for (auto& s : c->sections())
    {
        double t = s.startBar * 4 * 60.0 / c->bpm();
        std::cout << "  " << int(t) / 60 << ":" << juce::String(int(t) % 60).paddedLeft('0', 2) << "  " << s.name << "\n";
        sections << t << " " << s.energy << " " << s.name << "\n";
    }
    sections << c->totalBars() * 4 * 60.0 / c->bpm() << " 0 end\n";
    out.withFileExtension("sections.txt").replaceWithText(sections);
    std::cout << "sounds:\n";
    for (auto& ch : engine.soundPlan())
    {
        double t = ch.bar * 4 * 60.0 / c->bpm();
        std::cout << "  " << int(t) / 60 << ":" << juce::String(int(t) % 60).paddedLeft('0', 2) << "  " << info.trackNames[ch.track]
                  << " -> " << engine.palette().parts[ch.track][size_t(ch.entry)].patch << "\n";
    }

    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(out);
    auto writer = juce::WavAudioFormat().createWriterFor(
        stream, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(2).withBitsPerSample(24));
    if (!writer) { std::cerr << "cannot write " << out.getFullPathName() << "\n"; return 1; }

    // Play the pre-roll into nothing, so sample 0 of the WAV is bar 0 and lines up with the MIDI.
    juce::AudioBuffer<float> preRoll(2, int(engine.preRollSamples()));
    engine.render(preRoll.getArrayOfWritePointers(), 2, preRoll.getNumSamples());

    juce::AudioBuffer<float> buf(2, block);
    const int64_t total = seconds > 0 ? int64_t(seconds * sr) : std::numeric_limits<int64_t>::max();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    int64_t done = 0;
    std::vector<std::array<float, 7>> levels;
    double lastSq[kMaxTracks + 2] = {};
    while (done < total && !engine.isFinished())
    {
        engine.service(); // what the app's timer does: load each part's next sound ahead
        engine.render(buf.getArrayOfWritePointers(), 2, block);
        writer->writeFromAudioSampleBuffer(buf, 0, block);
        // The visuals' four roles (harmony, bass and drums, rhythm, melody), each the power sum of its tracks.
        std::array<float, 7> lv {};
        double rolePower[4] = {};
        for (int k = 0; k < info.numTracks + 2; ++k)
        {
            const Engine::Meter& m = k < info.numTracks ? engine.meters[k] : k == info.numTracks ? engine.delayMeter : engine.reverbMeter;
            const double sq = m.sumSquares.load();
            const double power = juce::jmax(0.0, sq - lastSq[k]) / block;
            lastSq[k] = sq;
            if (k < info.numTracks) rolePower[info.role[k]] += power;
            else lv[size_t(4 + k - info.numTracks)] = float(std::sqrt(power));
        }
        for (int r = 0; r < 4; ++r) lv[size_t(r)] = float(std::sqrt(rolePower[r]));
        const float l = buf.getRMSLevel(0, 0, block), r = buf.getRMSLevel(1, 0, block);
        lv[6] = std::sqrt(0.5f * (l * l + r * r));
        levels.push_back(lv);
        done += block;
    }
    writer.reset();
    writeMidi(seed, out.withFileExtension("mid"));
    if (startBar == 0) writeShow(seed, sr / block, levels, out.withFileExtension("show.json"));

    const double secs = double(done) / sr, took = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    std::cout << "rendered " << secs << " s in " << took << " s (" << secs / took << "x real time) -> "
              << out.getFullPathName() << "\n";
    std::cout << "master peak before limiter " << dbfs(engine.peakPre) << " dBFS, after " << dbfs(engine.peakPost) << " dBFS; limiter deepest "
              << engine.limiter.maxReductionDb << " dB, clamped samples " << engine.limiter.clipped << "; glue deepest " << engine.glue.maxReductionDb << " dB\n";
    auto show = [](const char* name, const Engine::Meter& m) {
        std::cout << "  " << name << ": peak " << dbfs(m.peak) << " dBFS, rms "
                  << dbfs(std::sqrt(m.sumSquares / juce::jmax<int64_t>(1, m.samples))) << " dBFS\n";
    };
    for (int t = 0; t < info.numTracks; ++t) show(info.trackNames[t], engine.meters[t]);
    show("delay return", engine.delayMeter);
    show("reverb return", engine.reverbMeter);
    std::cout << "  delay " << engine.delayDescription() << "\n";
    std::cout << "sound changes made " << engine.changesMade << ", missed " << engine.missedChanges << "\n";
    return 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    setOfflineRendering(true); // a render is not live: sfizz may wait for its disk streaming
    juce::StringArray args(argv + 1, argc - 1);
    gRaw = args.contains("--raw");
    if (args[0] == "--probe") return probe(args[1], args[2], args[3]); // --probe <vst3> [names,to,match] [Category/Patch]
    if (args[0] == "--test-echo") // one impulse through a track's echo: where each repeat lands and how loud
    {
        const double sr = 48000;
        FeedbackDelay d;
        d.prepare(sr);
        d.set(0.5, args[1].isEmpty() ? 0.6f : args[1].getFloatValue(), 0.5f);
        std::vector<float> l(size_t(sr * 6), 0.0f), r(l.size(), 0.0f);
        l[0] = r[0] = 1.0f;
        for (size_t at = 0; at < l.size(); at += 512) d.process(l.data() + at, r.data() + at, int(std::min<size_t>(512, l.size() - at)));
        for (int k = 1; k <= 8; ++k)
        {
            const size_t c = size_t(k * 0.5 * sr);
            float pl = 0, pr = 0;
            for (size_t i = c - 200; i < c + 200 && i < l.size(); ++i) { pl = std::max(pl, std::abs(l[i])); pr = std::max(pr, std::abs(r[i])); }
            std::cout << "echo " << k << " at " << k * 0.5 << " s: left " << dbfs(pl) << " dB, right " << dbfs(pr) << " dB\n";
        }
        return 0;
    }
    if (args[0] == "--audition") return audition(args[1], args.size() > 2 && args[2].containsOnly("0123456789") ? args[2].getIntValue() : 57, args.contains("--pure"));

    uint64_t seed = 1;
    double seconds = 0;
    int solo = -1, bars = 0;
    juce::String from;
    juce::StringArray patches;
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile("lisztmachine.wav");
    if (int i = args.indexOf("--scene"); i >= 0) paletteName = sceneName = args[i + 1].toStdString();   // a scene brings its own palette
    if (int i = args.indexOf("--feedback"); i >= 0) feedbackOverride = args[i + 1].getFloatValue();
    for (int i = 0; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--seed") seed = uint64_t(args[i + 1].getLargeIntValue());
        if (args[i] == "--seconds") seconds = args[i + 1].getDoubleValue();
        if (args[i] == "--out") out = juce::File::getCurrentWorkingDirectory().getChildFile(args[i + 1]);
        if (args[i] == "--patch") patches.add(args[i + 1]);
        if (args[i] == "--solo") solo = sceneInfo(sceneName).trackOf(args[i + 1].toStdString());
    }
    for (int i = 0; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--from") from = args[i + 1];
        if (args[i] == "--bars") bars = args[i + 1].getIntValue();
    }
    if (int i = args.indexOf("--palette"); i >= 0) paletteName = args[i + 1];
    return renderConcert(seed, seconds, out, solo, patches, args.contains("--dry"), from, bars);
}
