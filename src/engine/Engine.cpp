#include "Engine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

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

// ---------------------------------------------------------------- the sampled kit
namespace {
// drum -> loudness layers (softest first) -> alternate hits
std::map<std::string, std::vector<std::vector<std::vector<float>>>> gKit;
bool gKitLoaded = false;
}

juce::File findDrumKit()
{
    auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int up = 0; up < 4 && dir.exists(); ++up, dir = dir.getParentDirectory())
        for (auto sub : { "Gogodze", "Resources/Gogodze" })
            if (auto d = dir.getChildFile(sub); d.getChildFile("Samples").isDirectory()) return d;
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("LisztmachineSounds/Gogodze");
}

bool DrumSampler::kitLoaded() { return gKitLoaded; }

juce::String DrumSampler::loadKit()
{
    if (gKitLoaded) return {};
    const auto kit = findDrumKit();
    static const std::pair<const char*, const char*> drums[] = {
        { "ks", "kick_mic" }, { "sc", "snare_mic" }, { "se", "snare_mic" }, { "ss", "snare_mic" },
        { "hc", "oh_mic" }, { "ho", "oh_mic" }, { "tl", "front_mic" }, { "tm", "front_mic" }, { "th", "front_mic" },
    };
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (auto& [name, mic] : drums)
    {
        auto& layers = gKit[name];
        for (int vl = 1; vl <= 9; ++vl)
        {
            std::vector<std::vector<float>> hits;
            for (int rr = 1; rr <= 4; ++rr)
            {
                const auto f = kit.getChildFile("Samples").getChildFile(mic).getChildFile(juce::String(name) + "_vl" + juce::String(vl) + "_rr" + juce::String(rr) + ".wav");
                std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
                if (!r) continue;
                std::vector<float> x(size_t(r->lengthInSamples));
                float* ch[] = { x.data() };
                r->read(ch, 1, 0, int(r->lengthInSamples));
                // a 2 ms fade at the end, so a sample cut short of silence does not tick
                const size_t fade = std::min<size_t>(x.size(), 88);
                for (size_t i = 0; i < fade; ++i) x[x.size() - 1 - i] *= float(i) / float(fade);
                hits.push_back(std::move(x));
            }
            if (!hits.empty()) layers.push_back(std::move(hits));
        }
        if (layers.empty()) { gKit.clear(); return "no drum kit at " + kit.getFullPathName() + " (run tools/fetch_drums.py)"; }
    }
    gKitLoaded = true;
    return {};
}

void DrumSampler::prepare(double sr, Part part)
{
    rate_ = 44100.0 / sr;
    part_ = part;
    chokeStep_ = float(1.0 / (0.006 * sr)); // an open hat closes in 6 ms
    allOff();
}

void DrumSampler::start(int note, float vel)
{
    const char* name = "ks";
    switch (part_)
    {
        case KickPart: name = "ks"; break;
        case SnarePart: name = note >= 60 ? "ss" : vel < .45f ? "se" : "sc"; break;
        case HatsPart: name = note == 46 ? "ho" : "hc"; break;
        case TomPart: name = note < 45 ? "tl" : note < 51 ? "tm" : "th"; break;
    }
    auto it = gKit.find(name);
    if (it == gKit.end()) return;
    const auto& layers = it->second;
    const int layer = juce::jlimit(0, int(layers.size()) - 1, int(vel * float(layers.size())));
    const auto& hits = layers[size_t(layer)];
    const auto* sample = &hits[size_t(rr_++ % int(hits.size()))];
    if (part_ == HatsPart) // the next hat chokes an open one
        for (auto& v : voices_) if (v.sample && v.open) v.fadeStep = chokeStep_;
    // a free voice, else the oldest (24 back it has long rung out)
    int at = next_;
    for (int k = 0; k < int(voices_.size()); ++k)
        if (!voices_[size_t((next_ + k) % int(voices_.size()))].sample) { at = (next_ + k) % int(voices_.size()); break; }
    auto& v = voices_[size_t(at)];
    next_ = (at + 1) % int(voices_.size());
    // within a layer the velocity still shapes the level a little
    v = { sample, 0, 0.55f + 0.45f * vel, 1, 0, name[1] == 'o' };
}

void DrumSampler::run(float* l, float* r, int from, int to)
{
    for (auto& v : voices_)
    {
        if (!v.sample) continue;
        const auto& x = *v.sample;
        for (int n = from; n < to; ++n)
        {
            const size_t i = size_t(v.pos);
            if (i + 1 >= x.size() || v.fade <= 0) { v.sample = nullptr; break; }
            const double f = v.pos - double(i);
            const float y = float(x[i] + (x[i + 1] - x[i]) * f) * v.gain * v.fade;
            l[n] += y;
            r[n] += y;
            v.pos += rate_;
            v.fade -= v.fadeStep;
        }
    }
}

void DrumSampler::render(const juce::MidiBuffer& midi, float* l, float* r, int numSamples)
{
    int at = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit(0, numSamples, meta.samplePosition);
        run(l, r, at, pos);
        at = pos;
        const auto m = meta.getMessage();
        if (m.isNoteOn()) start(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isAllSoundOff()) allOff();
    }
    run(l, r, at, numSamples);
}

void Biquad::highpass(double sr, double hz)
{
    const double w = juce::MathConstants<double>::twoPi * hz / sr, c = std::cos(w), alpha = std::sin(w) / (2 * 0.7071), a0 = 1 + alpha;
    b0 = float((1 + c) / 2 / a0);
    b1 = float(-(1 + c) / a0);
    b2 = b0;
    a1 = float(-2 * c / a0);
    a2 = float((1 - alpha) / a0);
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
        { "clap", { 0, 1.2f, 0, 150 } },   { "hats", { 0, 1.0f, .25f, 300 } }, { "perc", { .1f, 1.3f, -.2f, 200 } },
        { "fx", { .5f, 1.6f, 0, 200 } },
    };
    for (auto& [n, m] : table) if (name == n) return m;
    return { 0, 1, 0, 0 }; // the chorale's voices and anything else: as they are
}
} // namespace

// The engine's own synths and its sampled kit: no Surge instance, one sound for the whole song.
static bool isEngineSound(const juce::String& patch) { return patch.startsWith("internal/") || patch.startsWith("sample/"); }

juce::String Palette::load(const juce::File& file, const std::string& sceneName)
{
    scene = sceneName;
    info = &sceneInfo(sceneName);
    for (auto& p : parts) p.clear();
    for (auto& g : trackGainDb) g = 0;
    masterDb = 0;
    for (auto& d : drives) d = {};
    for (auto& c : crushes) c = {};
    for (auto& c : comps) c = {};
    for (auto& d : duckDepth) d = -1;
    for (auto& d : delays) d = {};
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
        if (tokens[0] == "master" && tokens.size() >= 2) { masterDb = tokens[1].getFloatValue(); continue; }
        if (tokens[0] == "gain" || tokens[0] == "delay" || tokens[0] == "drive" || tokens[0] == "crush" || tokens[0] == "comp" || tokens[0] == "duck")
        {
            const int t = info->trackOf(tokens[1].toStdString());
            if (t < 0) return where + "no track '" + tokens[1] + "' in the " + juce::String(scene) + " scene";
            if (tokens[0] == "gain") trackGainDb[t] = tokens[2].getFloatValue();
            else if (tokens[0] == "crush" && tokens.size() >= 5) crushes[t] = { tokens[2].getFloatValue(), tokens[3].getFloatValue(), juce::jlimit(0.0f, 1.0f, tokens[4].getFloatValue()) };
            else if (tokens[0] == "comp" && tokens.size() >= 7) comps[t] = { tokens[2].getFloatValue(), juce::jmax(1.0f, tokens[3].getFloatValue()), tokens[4].getFloatValue(), tokens[5].getFloatValue(), juce::jlimit(0.0f, 1.0f, tokens[6].getFloatValue()) };
            else if (tokens[0] == "duck") duckDepth[t] = juce::jlimit(0.0f, 1.0f, tokens[2].getFloatValue());
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
        auto patch = line.fromFirstOccurrenceOf(tokens[2], false, false).trim();
        if (!isEngineSound(patch) && !findSurgePatch(patch).existsAsFile())
            return where + "no Surge patch '" + patch + "'";
        for (int t : targets) parts[t].push_back({ patch, tokens[1].getFloatValue(), tokens[2].getFloatValue() });
    }
    for (auto& p : parts)
        std::stable_sort(p.begin(), p.end(), [](auto& a, auto& b) { return a.brightness < b.brightness; });
    for (int t = 0; t < info->numTracks; ++t)
        if (parts[t].empty()) return file.getFileName() + ": no patches for " + info->trackNames[t];
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
        const int n = int(palette.parts[t].size());
        int last = -1, beforeLast = -1;
        for (int s = 0; s < int(sections.size()); ++s)
        {
            // Pad and sequence change on even sections, bass and lead on odd ones.
            const bool evenPart = t % 2 == 0;
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

    numTracks_ = palette.info->numTracks;
    scene = palette.scene;
    for (int i = 0; i < numTracks_; ++i)
    {
        auto& t = tracks_[i];
        int channels = 2;
        const auto& first = palette.parts[i].front().patch;
        t.internal = isEngineSound(first);
        // "sample/kit" plays the sampled kit; "sample/kit+kick" layers the synth kick under it
        t.sampled = first.startsWith("sample/");
        t.synthLayer = first.endsWith("+kick");
        if (t.sampled)
        {
            if (auto e = DrumSampler::loadKit(); e.isNotEmpty()) return e;
            const juce::String name = palette.info->trackNames[i];
            t.sampler.prepare(sampleRate, name == "kick" ? DrumSampler::KickPart : name == "hats" ? DrumSampler::HatsPart
                                        : name == "perc" ? DrumSampler::TomPart : DrumSampler::SnarePart);
        }
        t.trim = juce::Decibels::decibelsToGain(palette.trackGainDb[i]);
        t.echo.prepare(sampleRate);
        const auto m = mixFor(palette.info->trackNames[i]);
        t.duck = m.duck;
        t.width = m.width;
        t.panL = std::sqrt(2.0f) * std::cos((m.pan + 1) * juce::MathConstants<float>::pi / 4);
        t.panR = std::sqrt(2.0f) * std::sin((m.pan + 1) * juce::MathConstants<float>::pi / 4);
        t.lowCut = m.lowCutHz > 0;
        for (auto& f : t.hp) f.highpass(sampleRate, juce::jmax(10.0f, m.lowCutHz));
        t.mix.setSize(2, maxBlock);
        t.drive.prepare(sampleRate, palette.drives[i]);
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
            const auto kind = first.endsWith("sub") ? InternalSynth::Sub : first.endsWith("blip") ? InternalSynth::Blip
                            : first.endsWith("kick") ? InternalSynth::Kick : first.endsWith("hat") ? InternalSynth::Hat
                            : first.endsWith("clap") ? InternalSynth::Clap : first.endsWith("snare") ? InternalSynth::Snare
                            : first.endsWith("kick808") ? InternalSynth::Kick808 : first.endsWith("metal") ? InternalSynth::Metal
                            : first.endsWith("zap") ? InternalSynth::Zap : InternalSynth::Click;
            t.synth.prepare(sampleRate, kind);
            for (auto& s : t.slots) { s.plugin.reset(); s.midi.ensureSize(8192); s.state = Idle; s.entry = 0; s.gain = juce::Decibels::decibelsToGain(palette.parts[i].front().gainDb); }
            t.buffer.setSize(2, maxBlock);
            continue;
        }
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
    glue.prepare(sampleRate);
    glue.makeupDb = palette.masterDb;
    for (auto& f : reverbLowCut_) f.highpass(sampleRate, 180);
    kickTrack_ = palette.info->trackOf("kick");
    duckCurve_.assign(size_t(maxBlock), 0);
    kickHits_.reserve(64);
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
    if (auto err = loadSurgePatch(*slot.plugin, findSurgePatch(e.patch)); err.isNotEmpty()) return err;
    if (palette_.info->pure[track]) purify(*slot.plugin, palette_.info->pure[track] == 1);
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
    if (delay_ && reverb_)
    {
        // Their pitch modulation (the delay's tape wobble, the reverb's chorus) reads as detune
        // under a pure-tuned chorale; Berlin keeps it.
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
        if (auto e = loadInto(t.slots[0], i, entry); e.isNotEmpty()) DBG(e);
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
            if (t.internal) { t.synth.allOff(); t.sampler.allOff(); continue; }
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

    // One track's sound into the mix and the sends: its low cut, width, pan and duck, then its gain.
    auto mixTrack = [&](int i, const float* L, const float* R) {
        auto& t = tracks_[i];
        const float* sends = palette_.info->sends[i];
        float peak = 0;
        double sq = 0;
        for (int n = 0; n < len; ++n)
        {
            float l = L[n], r = R[n], g = gains_[size_t(n)] * t.trim;
            if (mixStage)
            {
                if (t.drive.on()) { l = t.drive.process(l, 0); r = t.drive.process(r, 1); }
                if (t.crush.on()) t.crush.process(l, r);
                if (t.comp.on()) t.comp.process(l, r);
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
            for (int c = 0; c < 2; ++c)
            {
                pa[c][n] += x[c];
                delayIn.getWritePointer(c)[n] += x[c] * sends[0];
                reverbIn.getWritePointer(c)[n] += x[c] * sends[1];
                peak = juce::jmax(peak, std::abs(x[c]));
                sq += double(x[c]) * x[c];
            }
        }
        meters[i].peak = juce::jmax(meters[i].peak.load(), peak);
        meters[i].sumSquares = meters[i].sumSquares.load() + sq;
        meters[i].samples = meters[i].samples.load() + 2 * len;
    };

    for (int i = 0; i < numTracks_; ++i)
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

        if (t.internal)
        {   // the engine's own instrument: one voice pool, its MIDI in the first slot's buffer
            auto& slot = t.slots[0];
            t.buffer.clear();
            if (t.sampled) t.sampler.render(slot.midi, t.buffer.getWritePointer(0), t.buffer.getWritePointer(1), len);
            if (!t.sampled || t.synthLayer) t.synth.render(slot.midi, t.buffer.getWritePointer(0), t.buffer.getWritePointer(1), len);
            slot.midi.clear();
            if (t.hasEcho) t.echo.process(t.buffer.getWritePointer(0), t.buffer.getWritePointer(1), len);
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply(t.buffer.getWritePointer(c), slot.gain, len);
            mixTrack(i, t.buffer.getReadPointer(0), t.buffer.getReadPointer(1));
            continue;
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
            if (t.hasEcho && view.getNumChannels() >= 2) t.echo.process(view.getWritePointer(0), view.getWritePointer(1), len);

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
        mixTrack(i, t.mix.getReadPointer(0), t.mix.getReadPointer(1));
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
        // The echoes and the reverb pump with the kick too, so the tail breathes instead of washing.
        auto duckReturn = [&](juce::AudioBuffer<float>& b, float depth) {
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < len; ++n) b.getWritePointer(c)[n] *= 1 - depth * duckCurve_[size_t(n)];
        };
        duckReturn(delayIn, 0.35f);
        for (int c = 0; c < 2; ++c)
        {
            juce::FloatVectorOperations::add(pa[c], delayIn.getReadPointer(c), len);
            juce::FloatVectorOperations::addWithMultiply(reverbIn.getWritePointer(c), delayIn.getReadPointer(c), kDelayToReverb, len);
        }
        meter(delayMeter, delayIn);
        runFx(*reverb_, reverbIn);
        duckReturn(reverbIn, 0.5f);
        if (mixStage)
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < len; ++n) reverbIn.getWritePointer(c)[n] = reverbLowCut_[c].process(reverbIn.getReadPointer(c)[n]);
        meter(reverbMeter, reverbIn);
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::add(pa[c], reverbIn.getReadPointer(c), len);
    }

    if (mixStage && numChannels > 1) glue.process(pa[0], pa[1], len);
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

// ---------------------------------------------------------------- the engine's own instrument
void InternalSynth::start(int note, float velocity)
{
    Voice* v = &voices_[0];
    for (auto& x : voices_) if (x.note < 0) { v = &x; break; }       // a free voice, or steal the first
    v->note = note;
    v->phase = 0;
    v->held = kind_ == Sub;
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
    if (kind_ == Kick808) tau = 0.3 + 0.6 * velocity;   // the long 808 boom: the harder, the longer
    if (kind_ == Metal) tau = 0.05 + 0.25 * velocity;   // struck metal, tuned to the note
    if (kind_ == Zap) tau = 0.04 + 0.08 * velocity;     // a laser: a sine diving onto the note
    v->decay = std::exp(-1.0 / (tau * sr_));
    v->phase2 = v->phase3 = 0;
    const bool struck = kind_ != Sub && kind_ != Blip;
    v->attack = struck ? 1.0 : 1.0 / ((kind_ == Sub ? 0.04 : 0.001) * sr_);
    v->release = std::exp(-1.0 / (0.4 * sr_));
    v->env = struck ? v->gain : 0.0;
    // stereo: clicks and hats alternate a little left and right, blips spread by pitch, the rest sits centre
    float pan = kind_ == Click || kind_ == Hat ? ((flip_++ & 1) ? 0.3f : -0.3f) : kind_ == Blip ? juce::jlimit(-0.5f, 0.5f, float(note - 88) / 24.0f) : 0.0f;
    v->panL = std::sqrt(0.5f * (1 - pan));
    v->panR = std::sqrt(0.5f * (1 + pan));
}

void InternalSynth::stopNote(int note)
{
    for (auto& v : voices_) if (v.note == note && v.held) v.held = false;
}

void InternalSynth::run(float* l, float* r, int from, int to)
{
    for (auto& v : voices_)
    {
        if (v.note < 0) continue;
        for (int n = from; n < to; ++n)
        {
            if (kind_ == Click || kind_ == Kick || kind_ == Hat || kind_ == Snare || kind_ >= Kick808) v.env *= v.decay;
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
            if (kind_ >= Kick808)
            {
                const double age = double(v.age) / sr_;
                double y;
                if (kind_ == Kick808)
                {
                    // two octaves down onto the note in 40 ms, a little drive that the decay lets go of
                    v.phase += v.inc * (1 + 3 * std::exp(-age / 0.04));
                    y = std::tanh(2.5 * std::sin(v.phase) * v.env) * 0.5;
                }
                else if (kind_ == Metal)
                {
                    // FM: two inharmonic modulators (1.41 and 3.73 times the note), their brightness gone first
                    v.phase += v.inc;
                    v.phase2 += v.inc * 1.4142;
                    v.phase3 += v.inc * 3.7321;
                    const double index = 4.0 * std::exp(-age / 0.03);
                    y = std::sin(v.phase + index * std::sin(v.phase2) + 0.6 * index * std::sin(v.phase3)) * v.env;
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
        const bool done = kind_ == Sub ? (!v.held && v.env < 1e-5) : kind_ == Blip ? (v.attack == 0 && v.env < 1e-5) : v.env < 1e-5;
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

} // namespace liszt
