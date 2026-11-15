// Lisztmachine: the rehearsal panel. Plays a concert live through the audio device;
// outputs 1/2 are the PA mix, 3/4 the click for the drummer's in-ears.
#include "Engine.h"
#include "Improv.h"
#include "LiveLink.h"
#include <map>
#include <mutex>
#include <juce_audio_utils/juce_audio_utils.h>

using namespace liszt;

// Design System/Tokens.md, converted from OKLCH to sRGB.
namespace tok {
const juce::Colour sunk { 0xff0d1310 }, ground { 0xff19211d }, panel { 0xff222b27 }, raised { 0xff2e3833 },
    hairline { 0xff36403b }, ink { 0xffe3e7e4 }, dim { 0xffb3b9b4 }, faint { 0xff9ca49d }, accent { 0xffbbceb5 },
    lamp { 0xffbae6b1 };
constexpr int s2 = 8, s3 = 12, s4 = 16, s5 = 24;
}

// JUCE reads a plain string literal as ASCII; the middle dot is spelled out as UTF-8 bytes.
static const juce::String dot = juce::String::fromUTF8("  \xc2\xb7  ");

// The master FX panel's knobs: which engine value each moves, and how the knob shows it (engine value = knob / scale).
struct FxKnob { MasterFx::Param param; const char* name; double lo, hi, step, scale; const char* suffix; int unit; };
static const FxKnob kFxKnobs[] = {
    { MasterFx::Drive, "Drive", 0, 100, 1, 100, " %", 0 },      { MasterFx::Low, "Low", 0, 100, 1, 100, " %", 0 },
    { MasterFx::Tone, "Tone", -100, 100, 1, 100, "", 0 },       { MasterFx::DriveMix, "Mix", 0, 100, 1, 100, " %", 0 },
    { MasterFx::Output, "Output", -12, 12, 0.5, 1, " dB", 0 },
    { MasterFx::LowCut, "Low cut", 20, 4000, 10, 1, " Hz", 1 }, { MasterFx::Feedback, "Feedback", 0, 110, 1, 100, " %", 1 },
    { MasterFx::DelayMix, "Mix", 0, 100, 1, 100, " %", 1 },
    { MasterFx::EqLow, "Low 100", -12, 12, 0.5, 1, " dB", 2 },       { MasterFx::EqLowMid, "Low mid 400", -12, 12, 0.5, 1, " dB", 2 },
    { MasterFx::EqHighMid, "High mid 2.5k", -12, 12, 0.5, 1, " dB", 2 }, { MasterFx::EqHigh, "High 8k", -12, 12, 0.5, 1, " dB", 2 },
};
static const MasterFx::Param kFxOn[] = { MasterFx::DriveOn, MasterFx::DelayOn, MasterFx::EqOn };
static const char* const kFxUnits[] = { "Drive", "Delay", "EQ" };

static juce::String clock(double secs)
{
    int s = juce::jmax(0, int(secs));
    return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
}

class MainComponent : public juce::Component, juce::AudioIODeviceCallback, juce::ChangeListener, juce::Timer
{
public:
    MainComponent()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Lisztmachine";
        o.filenameSuffix = ".settings";
        o.folderName = "Lisztmachine";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters(o);

        // the song: every scene the composer knows, the last one chosen first
        scene = props.getUserSettings()->getValue("scene", "berlin").toStdString();
        for (int i = 0; i < int(std::size(kScenes)); ++i)
        {
            sceneBox.addItem(kSceneTitles[i], i + 1);
            if (scene == kScenes[i]) sceneBox.setSelectedId(i + 1, juce::dontSendNotification);
        }
        // 2026-11-14 (Kerem): Improvisation, endless generated songs (src/core/Improv.cpp), after the songs
        sceneBox.addSeparator();
        sceneBox.addItem("Improvisation", kImprovId);
        sceneBox.addItem("The whole concert", kConcertId); // 2026-11-14 (Kerem): the nine songs back to back, live
        if (scene == "improv") sceneBox.setSelectedId(kImprovId, juce::dontSendNotification);
        if (scene == "concert") sceneBox.setSelectedId(kConcertId, juce::dontSendNotification);
        if (sceneBox.getSelectedId() == 0) { scene = "berlin"; sceneBox.setSelectedId(1, juce::dontSendNotification); }
        sceneBox.onChange = [this] {
            scene = sceneBox.getSelectedId() == kImprovId ? std::string("improv") : sceneBox.getSelectedId() == kConcertId ? std::string("concert")
                  : std::string(kScenes[sceneBox.getSelectedId() - 1]);
            props.getUserSettings()->setValue("scene", juce::String(scene));
            reload();
            preview();
        };

        seed = juce::jmax<int64_t>(1, props.getUserSettings()->getIntValue("seed", 1)); // before the engine: Improvisation's chain is the seed
        levelThread.tick = [this] { sendLevels(); };
        levelThread.hit = [this] { sendHits(); };
        onset.thr = float(props.getUserSettings()->getDoubleValue("padThr", 0.02));
        levelThread.startThread();
        auto saved = props.getUserSettings()->getXmlValue("audio");
        deviceManager.initialise(1, 4, saved.get(), true);
        // input 1 on, for the kick (a setup saved before 0.13 had no inputs)
        if (auto setup = deviceManager.getAudioDeviceSetup(); !setup.inputChannels[0])
        {
            setup.inputChannels.setBit(0);
            setup.useDefaultInputChannels = false;
            deviceManager.setAudioDeviceSetup(setup, true);
        }
        deviceManager.addChangeListener(this);
        reload();

        seedBox.setText(juce::String(seed), false);
        seedBox.setInputRestrictions(10, "0123456789");
        seedBox.setJustification(juce::Justification::centred);
        seedBox.onReturnKey = [this] { seed = juce::jmax<int64_t>(1, seedBox.getText().getLargeIntValue()); preview(); };

        play.onClick = [this] { togglePlay(); };
        fresh.onClick = [this] {
            seed = juce::Random::getSystemRandom().nextInt({ 1, 1000000 });
            seedBox.setText(juce::String(seed), false);
            preview();
            if (chained()) { if (engine && engine->isPlaying()) { stopAll(); reload(); togglePlay(); } } // a new chain
            else if (engine && engine->isPlaying()) { engine->start(uint64_t(seed)); announce(*engine, 0); }
        };
        audio.onClick = [this] { showAudioSettings(); };
        // the feedback bus (Kerem, 2026-11-09: "the feedback amount from 0 to 80 percent as a slider"), per song, remembered
        feedback.setSliderStyle(juce::Slider::LinearHorizontal);
        feedback.setRange(0, 80, 1);
        feedback.setTextValueSuffix(" %");
        feedback.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
        feedback.onValueChange = [this] {
            props.getUserSettings()->setValue("feedback." + juce::String(scene), feedback.getValue());
            for (auto* e : { engine.get(), nextEngine.get(), starting.get() }) if (e && e->hasFeedbackBus()) e->feedbackBus = float(feedback.getValue() / 100.0);
        };
        feedback.setColour(juce::Slider::trackColourId, tok::accent);
        feedback.setColour(juce::Slider::backgroundColourId, tok::sunk);
        feedback.setColour(juce::Slider::thumbColourId, tok::lamp);
        feedback.setColour(juce::Slider::textBoxTextColourId, tok::ink);
        feedback.setColour(juce::Slider::textBoxOutlineColourId, tok::hairline);
        feedback.setColour(juce::Slider::textBoxBackgroundColourId, tok::sunk);
        feedbackLabel.setColour(juce::Label::textColourId, tok::dim);
        feedbackLabel.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(feedback);
        addAndMakeVisible(feedbackLabel);
        // the output level (Kerem, 2026-11-14: "clipping because of too high volume, on my sound card's meter it's on the
        // limit roof red"): the mix leaves the limiter at -1 dBFS; this takes it down before the interface, -6 dB at first
        output.setSliderStyle(juce::Slider::LinearHorizontal);
        output.setRange(-30, 0, 0.5);
        output.setTextValueSuffix(" dB");
        output.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
        output.setDoubleClickReturnValue(true, -6.0);
        output.setValue(props.getUserSettings()->getDoubleValue("output", -6.0), juce::dontSendNotification);
        outGain = juce::Decibels::decibelsToGain(float(output.getValue()));
        output.onValueChange = [this] {
            props.getUserSettings()->setValue("output", output.getValue());
            outGain = juce::Decibels::decibelsToGain(float(output.getValue()));
        };
        for (auto id : { juce::Slider::trackColourId, juce::Slider::thumbColourId }) output.setColour(id, id == juce::Slider::thumbColourId ? tok::lamp : tok::accent);
        output.setColour(juce::Slider::backgroundColourId, tok::sunk);
        output.setColour(juce::Slider::textBoxTextColourId, tok::ink);
        output.setColour(juce::Slider::textBoxOutlineColourId, tok::hairline);
        output.setColour(juce::Slider::textBoxBackgroundColourId, tok::sunk);
        outputLabel.setColour(juce::Label::textColourId, tok::dim);
        outputLabel.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(output);
        addAndMakeVisible(outputLabel);
        // the in-ear mix (Kerem, 2026-11-14: "an in-ear monitor channel with its own metronome volume, for out 3 of the
        // Apollo"): outputs 3/4 carry the music (in mono) and the click, each at its own level, apart from the PA on 1/2
        auto ear = [this](juce::Slider& sl, juce::Label& lb, const char* key, double lo, double hi, double def, std::atomic<float>& gain) {
            sl.setSliderStyle(juce::Slider::LinearHorizontal);
            sl.setRange(lo, hi, 0.5);
            sl.setTextValueSuffix(" dB");
            sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
            sl.setDoubleClickReturnValue(true, def);
            sl.setValue(props.getUserSettings()->getDoubleValue(key, def), juce::dontSendNotification);
            auto apply = [this, &sl, &gain, key, lo] {
                props.getUserSettings()->setValue(key, sl.getValue());
                gain = sl.getValue() <= lo ? 0.0f : juce::Decibels::decibelsToGain(float(sl.getValue())); // all the way down: off
            };
            apply();
            sl.onValueChange = apply;
            sl.setColour(juce::Slider::trackColourId, tok::accent);
            sl.setColour(juce::Slider::thumbColourId, tok::lamp);
            sl.setColour(juce::Slider::backgroundColourId, tok::sunk);
            sl.setColour(juce::Slider::textBoxTextColourId, tok::ink);
            sl.setColour(juce::Slider::textBoxOutlineColourId, tok::hairline);
            sl.setColour(juce::Slider::textBoxBackgroundColourId, tok::sunk);
            lb.setColour(juce::Label::textColourId, tok::dim);
            lb.setFont(juce::FontOptions(13.0f));
            addAndMakeVisible(sl);
            addAndMakeVisible(lb);
        };
        ear(earMusic, earMusicLabel, "earMusic", -60, 0, -12, earMusicGain);
        ear(earClick, earClickLabel, "earClick", -30, 12, 0, earClickGain);
        // the master FX panel (Kerem, 2026-11-11): everything through it, every knob remembered, every unit off at first
        for (int u = 0; u < 3; ++u)
        {
            auto& b = fxOn[u];
            b.setButtonText(kFxUnits[u]);
            b.setToggleState(fxValue(kFxOn[u]) > 0.5f, juce::dontSendNotification);
            b.onClick = [this, u] { setFx(kFxOn[u], fxOn[u].getToggleState() ? 1.0f : 0.0f); };
            b.setColour(juce::ToggleButton::textColourId, tok::ink);
            b.setColour(juce::ToggleButton::tickColourId, tok::lamp);
            addAndMakeVisible(b);
        }
        for (size_t k = 0; k < std::size(kFxKnobs); ++k)
        {
            const auto& d = kFxKnobs[k];
            auto& sl = fxKnob[k];
            sl.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            sl.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
            sl.setRange(d.lo, d.hi, d.step);
            if (d.param == MasterFx::LowCut) sl.setSkewFactorFromMidPoint(400);
            sl.setTextValueSuffix(d.suffix);
            sl.setDoubleClickReturnValue(true, MasterFx::defaults[d.param] * d.scale);
            sl.setValue(fxValue(d.param) * d.scale, juce::dontSendNotification);
            sl.onValueChange = [this, k] { setFx(kFxKnobs[k].param, float(fxKnob[k].getValue() / kFxKnobs[k].scale)); };
            sl.setColour(juce::Slider::rotarySliderFillColourId, tok::accent);
            sl.setColour(juce::Slider::rotarySliderOutlineColourId, tok::sunk);
            sl.setColour(juce::Slider::thumbColourId, tok::lamp);
            sl.setColour(juce::Slider::textBoxTextColourId, tok::ink);
            sl.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible(sl);
            auto& lb = fxLabel[k];
            lb.setText(d.name, juce::dontSendNotification);
            lb.setJustificationType(juce::Justification::centred);
            lb.setColour(juce::Label::textColourId, tok::dim);
            lb.setFont(juce::FontOptions(12.0f));
            addAndMakeVisible(lb);
        }
        auto fxBox = [this](juce::ComboBox& box, MasterFx::Param param, const char* const* names, int count) {
            for (int i = 0; i < count; ++i) box.addItem(names[i], i + 1);
            box.setSelectedId(int(fxValue(param)) + 1, juce::dontSendNotification);
            box.onChange = [this, &box, param] { setFx(param, float(box.getSelectedId() - 1)); };
            box.setColour(juce::ComboBox::backgroundColourId, tok::raised);
            box.setColour(juce::ComboBox::textColourId, tok::ink);
            box.setColour(juce::ComboBox::outlineColourId, tok::hairline);
            box.setColour(juce::ComboBox::arrowColourId, tok::dim);
            addAndMakeVisible(box);
        };
        fxBox(styleBox, MasterFx::Style, MasterFx::styleNames, int(std::size(MasterFx::styleNames)));
        fxBox(timeBox, MasterFx::Time, MasterFx::timeNames, int(std::size(MasterFx::timeNames)));

        click.setToggleState(true, juce::dontSendNotification);
        click.onClick = [this] { applyClick(); };
        // the click on the main outputs too, for rehearsing without the in-ear outputs (remembered)
        clickMain.setToggleState(props.getUserSettings()->getBoolValue("clickMain", false), juce::dontSendNotification);
        clickMain.onClick = [this] { props.getUserSettings()->setValue("clickMain", clickMain.getToggleState()); applyClick(); };
        clickMain.setColour(juce::ToggleButton::textColourId, tok::dim);
        clickMain.setColour(juce::ToggleButton::tickColourId, tok::lamp);
        addAndMakeVisible(clickMain);
        // the visuals page drives the app (LiveLink.h): its commands come on the link's thread, run here
        live.onCommand = [sp = juce::Component::SafePointer<MainComponent>(this)](const juce::String& c) {
            juce::MessageManager::callAsync([sp, c] { if (sp) sp->command(c); });
        };

        for (auto* b : { &play, &fresh, &audio })
        {
            b->setColour(juce::TextButton::buttonColourId, tok::raised);
            b->setColour(juce::TextButton::textColourOffId, tok::ink);
            addAndMakeVisible(b);
        }
        play.setColour(juce::TextButton::buttonColourId, tok::accent);
        play.setColour(juce::TextButton::textColourOffId, tok::sunk);
        seedBox.setColour(juce::TextEditor::backgroundColourId, tok::sunk);
        seedBox.setColour(juce::TextEditor::textColourId, tok::ink);
        seedBox.setColour(juce::TextEditor::outlineColourId, tok::hairline);
        seedBox.setColour(juce::TextEditor::focusedOutlineColourId, tok::lamp);
        click.setColour(juce::ToggleButton::textColourId, tok::dim);
        click.setColour(juce::ToggleButton::tickColourId, tok::lamp);
        sceneBox.setColour(juce::ComboBox::backgroundColourId, tok::raised);
        sceneBox.setColour(juce::ComboBox::textColourId, tok::ink);
        sceneBox.setColour(juce::ComboBox::outlineColourId, tok::hairline);
        sceneBox.setColour(juce::ComboBox::arrowColourId, tok::dim);
        getLookAndFeel().setColour(juce::PopupMenu::backgroundColourId, tok::panel);
        getLookAndFeel().setColour(juce::PopupMenu::textColourId, tok::ink);
        getLookAndFeel().setColour(juce::PopupMenu::highlightedBackgroundColourId, tok::raised);
        getLookAndFeel().setColour(juce::PopupMenu::highlightedTextColourId, tok::lamp);
        addAndMakeVisible(sceneBox);
        addAndMakeVisible(seedBox);
        addAndMakeVisible(click);

        preview();
        setWantsKeyboardFocus(true);
        setSize(1000, 690);
        startTimerHz(20);
    }

    ~MainComponent() override
    {
        levelThread.stopThread(1000);
        deviceManager.removeAudioCallback(this);
        stopAll(); // the next-song thread first: it uses this component
        for (auto& t : graveyard) t->stopThread(4000);
        graveyard.clear();
        { const std::lock_guard<std::mutex> l(loadedMutex); results.clear(); }
        if (auto xml = deviceManager.createStateXml()) props.getUserSettings()->setValue("audio", xml.get());
        props.getUserSettings()->setValue("seed", juce::String(seed));
        props.saveIfNeeded();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(tok::ground);
        g.setColour(tok::panel);
        for (auto& u : fxUnit) g.fillRoundedRectangle(u.toFloat(), 6.0f);
        auto r = getLocalBounds().reduced(tok::s5);

        g.setColour(tok::faint);
        g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
        const juce::String head = chained() ? juce::String(improv() ? "Improvisation" : "The whole concert") + dot + "song " + juce::String(improvK + 1)
                                                  + (concert() ? juce::String("/") + juce::String(int(std::size(kScenes))) : juce::String())
                                              + (oldEngine ? dot + "changing over" : nextEngine || starting ? dot + "next song ready" : loader ? dot + "writing the next song" : juce::String())
                                           : juce::String(kSceneTitles[sceneBox.getSelectedId() - 1]);
        g.drawText("LISZTMACHINE" + dot + head.toUpperCase(), r.removeFromTop(16), juce::Justification::left);
        r.removeFromTop(tok::s2);

        if (error.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffe8a598));
            g.setFont(juce::FontOptions(16.0f));
            g.drawFittedText("Could not start the sound engine:\n" + error, r.removeFromTop(80), juce::Justification::topLeft, 4);
            return;
        }
        if (!composer) return;

        const bool playing = engine && engine->isPlaying();
        const int bar = playing ? engine->bar.load() : 0;
        const double now = playing ? engine->seconds.load() : 0;
        const double total = composer->totalBars() * 4 * 60.0 / composer->bpm();

        g.setColour(tok::ink);
        g.setFont(juce::FontOptions(34.0f));
        auto title = r.removeFromTop(44);
        g.drawText(playing ? composer->sectionAt(bar).name : "Ready", title, juce::Justification::left);
        g.setColour(tok::dim);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText(clock(now) + " / " + clock(total) + "   bar " + juce::String(bar + 1) + " / " + juce::String(composer->totalBars()),
                   title, juce::Justification::right);
        g.drawText(composer->describe(), r.removeFromTop(22), juce::Justification::left);
        r.removeFromTop(tok::s2);

        // What each part is playing now: the palette rotates these over the concert. The part's name
        // above, its sound below, up to ten parts across.
        const auto& info = sceneInfo(engine ? engine->scene : scene);
        if (engine && playing)
        {
            auto row = r.removeFromTop(34);
            for (int t = 0; t < info.numTracks; ++t)
            {
                auto cell = row.removeFromLeft(row.getWidth() / (info.numTracks - t)).withTrimmedRight(tok::s2);
                g.setColour(tok::faint);
                g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
                g.drawText(juce::String(info.trackNames[t]).toUpperCase(), cell.removeFromTop(15), juce::Justification::left);
                g.setColour(tok::ink);
                g.setFont(juce::FontOptions(13.0f));
                auto patch = engine->currentPatch(t).fromFirstOccurrenceOf("/", false, false);
                g.drawFittedText(patch.isEmpty() ? engine->currentPatch(t) : patch, cell, juce::Justification::left, 1, 0.8f);
            }
        }
        else
            r.removeFromTop(34);
        r.removeFromTop(tok::s3);

        // The form: one block per section, its height the section's energy.
        auto form = r.removeFromTop(150);
        g.setColour(tok::sunk);
        g.fillRect(form);
        const float pxPerBar = float(form.getWidth()) / float(composer->totalBars());
        for (auto& s : composer->sections())
        {
            juce::Rectangle<float> b(form.getX() + s.startBar * pxPerBar, 0, s.bars * pxPerBar - 2, 0);
            float h = (form.getHeight() - 26) * juce::jmax(0.06f, s.energy);
            b = b.withY(float(form.getBottom()) - h).withHeight(h);
            const bool current = playing && bar >= s.startBar && bar < s.startBar + s.bars;
            g.setColour(current ? tok::accent : tok::raised);
            g.fillRect(b);
            g.setColour(current ? tok::ink : tok::faint);
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(s.name, juce::Rectangle<float>(b.getX(), float(form.getY()) + 4, b.getWidth(), 14), juce::Justification::centredLeft);
        }
        if (playing)
        {
            float x = form.getX() + float(now / total) * form.getWidth();
            g.setColour(tok::lamp);
            g.fillRect(juce::Rectangle<float>(x - 1, float(form.getY()), 2, float(form.getHeight())));
        }
        r.removeFromTop(tok::s3);

        // Master level, read from the signal (A-6).
        if (engine)
        {
            g.setColour(tok::dim);
            g.setFont(juce::FontOptions(13.0f));
            auto peak = juce::Decibels::gainToDecibels(meterPeak, -90.0f);
            auto* dev = deviceManager.getCurrentAudioDevice();
            const int xruns = dev ? dev->getXRunCount() : -1;   // the audio not computed in time: crackles, a stumbling click
            g.setColour(xruns > 0 ? juce::Colour(0xffe8a598) : tok::dim);
            g.drawText(juce::String(xruns >= 0 ? "dropouts " + juce::String(xruns) + " " + dot + " " : juce::String())
                       + "master " + juce::String(peak, 1) + " dBFS " + dot + " limiter " + juce::String(engine->limiter.maxReductionDb, 1)
                           + " dB" + (deviceManager.getCurrentAudioDevice() ? " " + dot + " " + deviceManager.getCurrentAudioDevice()->getName() : ""),
                       r.removeFromTop(18), juce::Justification::left);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(tok::s5);
        auto row = area.removeFromBottom(36);
        auto fbRow = area.removeFromBottom(tok::s3 + 28).removeFromTop(28);
        auto earRow = area.removeFromBottom(tok::s3 + 28).removeFromTop(28);
        earMusicLabel.setBounds(earRow.removeFromLeft(190));
        earMusic.setBounds(earRow.removeFromLeft(360));
        earRow.removeFromLeft(tok::s5);
        earClickLabel.setBounds(earRow.removeFromLeft(110));
        earClick.setBounds(earRow.removeFromLeft(juce::jmin(300, earRow.getWidth())));
        // the FX panel above it: the three units side by side, each as wide as its controls
        auto fx = area.removeFromBottom(tok::s3 + 140).removeFromTop(140);
        const int cells[3] = { 5, 3, 4 }; // knobs per unit
        const float cellW = float(fx.getWidth() - 2 * tok::s3) / 12.0f;
        for (int u = 0; u < 3; ++u)
        {
            fxUnit[u] = u == 2 ? fx : fx.removeFromLeft(int(cellW * float(cells[u])));
            fx.removeFromLeft(tok::s3);
            auto in = fxUnit[u].reduced(tok::s2);
            auto head = in.removeFromTop(28);
            if (u < 2) (u == 0 ? styleBox : timeBox).setBounds(head.removeFromRight(juce::jmin(130, head.getWidth() - 90)));
            fxOn[u].setBounds(head.removeFromLeft(90));
            in.removeFromTop(tok::s2 / 2);
            const int w = in.getWidth() / cells[u];
            for (size_t k = 0; k < std::size(kFxKnobs); ++k)
                if (kFxKnobs[k].unit == u)
                {
                    auto cell = in.removeFromLeft(w);
                    fxLabel[k].setBounds(cell.removeFromTop(16));
                    fxKnob[k].setBounds(cell);
                }
        }
        feedbackLabel.setBounds(fbRow.removeFromLeft(190));
        feedback.setBounds(fbRow.removeFromLeft(360));
        fbRow.removeFromLeft(tok::s5);
        outputLabel.setBounds(fbRow.removeFromLeft(70));
        output.setBounds(fbRow.removeFromLeft(juce::jmin(300, fbRow.getWidth())));
        juce::FlexBox fb;
        // JUCE's FlexBox has no gap property, so each item carries the gap as a right margin.
        auto item = [](juce::Component& c, float w) { return juce::FlexItem(c).withWidth(w).withMargin({ 0, float(tok::s3), 0, 0 }); };
        fb.items = { item(sceneBox, 190), item(play, 100), item(fresh, 130), item(seedBox, 90), item(click, 150), item(clickMain, 120), item(audio, 110) };
        fb.performLayout(row);
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        return false;
    }

private:
    void preview()
    {
        previewComposer = makeComposer(songScene(0), uint64_t(seed));
        composer = previewComposer.get();
        props.getUserSettings()->setValue("seed", juce::String(seed));
        repaint();
    }

    void togglePlay()
    {
        if (!engine || error.isNotEmpty()) return;
        if (engine->isPlaying()) { if (chained()) stopAll(); else engine->stop(); live.stop(); }
        else
        {
            seed = juce::jmax<int64_t>(1, seedBox.getText().getLargeIntValue());
            // Improvisation always begins at its first song (the chain is the seed)
            if (chained() && (improvK != 0 || chainSeed != seed || engine->isFinished())) { reload(); if (!engine) return; }
            engine->limiter.maxReductionDb = 0;
            engine->start(uint64_t(seed));
            if (chained()) changeAt = changeTime(*engine->composer());
            announce(*engine, improvK);
        }
    }

    // (Re)build the engine for the device's current rate and block size.
    void reload()
    {
        deviceManager.removeAudioCallback(this);
        stopAll();
        auto* dev = deviceManager.getCurrentAudioDevice();
        loadedRate = dev ? dev->getCurrentSampleRate() : 48000.0;
        loadedBlock = dev ? dev->getCurrentBufferSizeSamples() : 512;
        mixBuf.setSize(8, juce::jmax(8192, loadedBlock));
        improvK = 0;
        chainSeed = seed;
        { std::unique_ptr<Engine> gone; { const juce::SpinLock::ScopedLockType sl(audioLock); gone = std::move(engine); } }
        error = {};
        std::unique_ptr<Engine> fresh; // swapped in under the lock: the level thread reads the engines
        if (dev) fresh = build(0, error, false);
        else error = "no audio output device";
        if (error.isNotEmpty()) fresh.reset();
        { const juce::SpinLock::ScopedLockType sl(audioLock); engine = std::move(fresh); }
        // the song's own amount until Kerem moves it (at most 80 %); a song without the bus greys the slider out
        const bool bus = engine && engine->hasFeedbackBus();
        const double own = engine ? juce::jmin(80.0, std::round(engine->palette().fbAmount * 100.0)) : 0.0;
        feedback.setValue(bus ? double(props.getUserSettings()->getDoubleValue("feedback." + juce::String(scene), own)) : 0.0, juce::dontSendNotification);
        feedback.setEnabled(bus);
        feedback.setAlpha(bus ? 1.0f : 0.35f);
        feedbackLabel.setText(bus ? "Song feedback" : "Song feedback (none in this song)", juce::dontSendNotification);
        if (engine && bus) engine->feedbackBus = float(feedback.getValue() / 100.0);
        if (engine) settle(*engine);
        deviceManager.addAudioCallback(this);
        repaint();
    }

    float fxValue(MasterFx::Param p) { return float(props.getUserSettings()->getDoubleValue("fx." + juce::String(MasterFx::paramNames[p]), MasterFx::defaults[p])); }
    void setFx(MasterFx::Param p, float v)
    {
        props.getUserSettings()->setValue("fx." + juce::String(MasterFx::paramNames[p]), v);
        for (auto* e : { engine.get(), nextEngine.get(), oldEngine.get(), starting.get() }) if (e) e->masterFx.p[p] = v;
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        if (!dev || dev->getCurrentSampleRate() != loadedRate || dev->getCurrentBufferSizeSamples() != loadedBlock)
            reload();
        if (auto xml = deviceManager.createStateXml()) props.getUserSettings()->setValue("audio", xml.get());
    }

    void showAudioSettings()
    {
        auto* sel = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 0, 2, 4, false, false, true, false);
        sel->setSize(520, 420);
        juce::DialogWindow::LaunchOptions opt;
        opt.content.setOwned(sel);
        opt.dialogTitle = "Audio device";
        opt.dialogBackgroundColour = tok::panel;
        opt.useNativeTitleBar = true;
        opt.launchAsync();
    }

    void timerCallback() override
    {
        if (engine)
        {
            meterPeak = engine->peakPost.exchange(0);
            engine->service(); // load each part's next sound ahead of its change
            if (chained()) improvise();
            sendState();
            if (engine->isPlaying() || engine->isFinished()) composer = engine->composer();
        }
        play.setButtonText(engine && engine->isPlaying() ? "Stop" : "Play");
        repaint();
    }

    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        if (numIn > 0 && in[0] != nullptr) onset.process(in[0], n, loadedRate > 0 ? loadedRate : 48000.0);
        const juce::SpinLock::ScopedLockType sl(audioLock); // held by the message thread only to swap the engines
        // the whole concert's next song starts on the sample its first beat is due (where the click's glide lands); read
        // before this block moves the playing song on
        if (starting && !starting->isPlaying())
        {
            const bool on = engine && engine->isPlaying();
            const int64_t p = on ? engine->position() : 0;
            if (!on || p + n > startAt) starting->goIn(on ? juce::jmax<int64_t>(0, startAt - p) : 0);
        }
        if (engine) engine->render(out, numOut, n);
        else for (int c = 0; c < numOut; ++c) juce::FloatVectorOperations::clear(out[c], n);
        // Improvisation's changeover: the last song's Release rings out under the new song's Seed (and the concert's next
        // song, from its first sample until the message thread makes it the playing one)
        for (auto* e : { oldEngine.get(), starting && starting->isPlaying() ? starting.get() : nullptr })
            if (e && numOut <= mixBuf.getNumChannels() && n <= mixBuf.getNumSamples())
            {
                e->render(mixBuf.getArrayOfWritePointers(), numOut, n);
                for (int c = 0; c < numOut; ++c) juce::FloatVectorOperations::add(out[c], mixBuf.getReadPointer(c), n);
            }
        // the output level, then a ceiling under 0 dBFS: two songs at a changeover, or the click on the main outputs,
        // add to a limited mix
        // the in-ear mix on 3 (and 4): the click the engine wrote there at its level, and the music in mono, each its own
        if (numOut >= 3)
        {
            const float mg = earMusicGain.load(), cg = earClickGain.load();
            for (int i = 0; i < n; ++i)
            {
                const float x = ceilingClip(cg * out[2][i] + mg * 0.5f * (out[0][i] + out[1][i]));
                out[2][i] = x;
                if (numOut >= 4) out[3][i] = x;
            }
        }
        const float g = outGain.load();
        for (int c = 0; c < juce::jmin(2, numOut); ++c) for (int i = 0; i < n; ++i) out[c][i] = ceilingClip(g * out[c][i]);
        double sq = 0;
        for (int c = 0; c < juce::jmin(2, numOut); ++c) for (int i = 0; i < n; ++i) sq += double(out[c][i]) * out[c][i];
        masterSq = masterSq.load() + sq / double(juce::jmax(1e-6f, g * g)); // the visuals read the mix before the output level
        masterN = masterN.load() + 2 * n;
    }
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    // ---- Improvisation (Kerem, 2026-11-14: "generate songs on the run ... near the end it will generate another song and
    // transition to it component by component"). Song k plays in `engine`; song k+1 is written and its instruments loaded
    // on a thread meanwhile (40 s on the PC). JUCE makes plugins on the message thread only, so each plugin step is handed
    // there and waited for (a lock held instead deadlocked: the creation waited for the thread the lock had stopped);
    // between steps the panel keeps moving.
    // A quarter of the way into song k's Release, song k+1 starts (its Seed: the drone alone, inside k's last chord,
    // src/core/Improv.cpp) and song k rings out under it in `oldEngine` until it has finished. Endless; the seed is the chain.
    static constexpr int kImprovId = 1000;
    static constexpr int kConcertId = 1001;
    bool improv() const { return scene == "improv"; }
    bool concert() const { return scene == "concert"; }   // the nine songs in order, each loaded while the one before plays
    bool chained() const { return improv() || concert(); }
    std::string songScene(int k) const
    {
        return improv() ? "improv/" + std::to_string(k) : concert() ? std::string(kScenes[size_t(k) % std::size(kScenes)]) : scene;
    }
    // seconds into a song where the next one begins: Improvisation a quarter into its Release; the concert when the
    // song's last bar has ended and two seconds of its tail have rung
    double changeTime(const Composer& c) const
    {
        if (concert()) return c.totalBars() * 240.0 / c.bpm() + 2.0;
        const auto& r = c.sections().back();
        return (r.startBar + 0.25 * r.bars) * 240.0 / c.bpm();
    }
    std::unique_ptr<Engine> build(int k, juce::String& err, bool offThread)
    {
        Palette palette;
        const auto sc = songScene(k);
        err = palette.load(improv() ? writeImprovPalette(uint64_t(chainSeed), k) : findPalette(sc), sc);
        if (err.isNotEmpty()) return nullptr;
        auto e = std::make_unique<Engine>();
        if (offThread)
            e->onMessageThread = [](const std::function<void()>& f) {
                if (juce::Thread::currentThreadShouldExit()) return; // cancelled: the rest is skipped, the load fails
                juce::MessageManager::getInstance()->callFunctionOnMessageThread(
                    [](void* p) -> void* { (*static_cast<const std::function<void()>*>(p))(); return nullptr; },
                    const_cast<void*>(static_cast<const void*>(&f)));
            };
        e->scene = sc;
        err = e->load(loadedRate, juce::jmax(512, loadedBlock), palette);
        if (err.isNotEmpty()) return nullptr;
        return e;
    }
    // the panel's settings on an engine: master FX, click, the song feedback (where the song has the bus)
    void settle(Engine& e)
    {
        for (int k = 0; k < MasterFx::NumParams; ++k) e.masterFx.p[k] = fxValue(MasterFx::Param(k));
        e.clickLevel = click.getToggleState() ? 0.5f : 0.0f;
        e.clickToMain = clickMain.getToggleState();
        if (e.hasFeedbackBus()) e.feedbackBus = float(feedback.getValue() / 100.0);
    }
    struct Loader : juce::Thread
    {
        std::function<void()> job;
        explicit Loader(std::function<void()> j) : juce::Thread("Lisztmachine next song"), job(std::move(j)) {}
        void run() override { job(); }
    };
    void prepareNext()
    {
        const int k = improvK + 1, gen = ++loadGen;
        if (improv()) improvSong(uint64_t(chainSeed), k); // planned here: the chain's cache is filled on this thread only
        loader = std::make_unique<Loader>([this, k, gen] {
            juce::String err;
            auto e = build(k, err, true);
            if (e && !juce::Thread::currentThreadShouldExit()) e->start(uint64_t(chainSeed), 0, false); // ready, silent
            const std::lock_guard<std::mutex> l(loadedMutex);
            results.push_back({ gen, std::move(e), err });
        });
        loader->startThread();
    }
    void improvise()
    {
        std::vector<Loaded> got;
        { const std::lock_guard<std::mutex> l(loadedMutex); got.swap(results); }
        for (auto& r : got) // a result of a cancelled load is let go here, on this thread, like every plugin
            if (r.gen == loadGen && loader)
            {
                nextEngine = std::move(r.engine);
                if (nextEngine) settle(*nextEngine);
                else error = "next song: " + r.error;
            }
        if (loader && !loader->isThreadRunning()) loader.reset();
        graveyard.erase(std::remove_if(graveyard.begin(), graveyard.end(), [](auto& t) { return !t->isThreadRunning(); }), graveyard.end());
        const bool going = engine->isPlaying() || engine->isFinished();
        const bool more = !concert() || improvK + 1 < int(std::size(kScenes)); // the concert ends after its ninth song
        if (going && more && !loader && !nextEngine && !starting && engine->seconds.load() > 2.0) prepareNext();
        // the concert: the click glides into the next song's tempo and the audio thread starts it where the glide lands
        if (concert() && nextEngine && !starting && engine->isPlaying())
        {
            engine->glideClick(nextEngine->composer()->bpm(), changeAt);
            const juce::SpinLock::ScopedLockType sl(audioLock);
            startAt = engine->landing() - nextEngine->preRollSamples();
            starting = std::move(nextEngine);
        }
        if (starting && starting->isPlaying()) changeover();
        if (nextEngine && (engine->isFinished() || (engine->isPlaying() && engine->seconds.load() >= changeAt))) changeover();
        if (oldEngine)
        {
            oldEngine->service();
            if (oldEngine->isFinished() || !oldEngine->isPlaying())
            {
                std::unique_ptr<Engine> gone;
                { const juce::SpinLock::ScopedLockType sl(audioLock); gone = std::move(oldEngine); }
            }
        }
    }
    void changeover()
    {
        std::unique_ptr<Engine> gone;
        engine->clickLevel = 0; // the click follows the new song from its first beat
        const bool started = starting != nullptr; // the concert: already started by the audio thread
        {
            const juce::SpinLock::ScopedLockType sl(audioLock);
            gone = std::move(oldEngine);
            if (engine->isFinished()) { if (!gone) gone = std::move(engine); else engine.reset(); }
            else oldEngine = std::move(engine);
            engine = std::move(started ? starting : nextEngine);
        }
        if (!started) engine->go();
        ++improvK;
        changeAt = changeTime(*engine->composer());
        announce(*engine, improvK, started ? engine->seconds.load() : 1e9);
        composer = engine->composer();
        const bool bus = engine->hasFeedbackBus();
        feedback.setEnabled(bus);
        feedback.setAlpha(bus ? 1.0f : 0.35f);
    }
    void stopAll()
    {
        // a load under way is told to stop and left to finish on its own (joining it here could wait on this very thread)
        if (loader) { loader->signalThreadShouldExit(); graveyard.push_back(std::move(loader)); }
        ++loadGen;
        std::unique_ptr<Engine> a, b, c;
        {
            const juce::SpinLock::ScopedLockType sl(audioLock);
            a = std::move(oldEngine);
            b = std::move(nextEngine);
            c = std::move(starting);
        }
        if (engine) engine->stop();
    }
    // the click's switches on every engine (an old song ringing out keeps its click off: the click follows the new one)
    void applyClick()
    {
        for (auto* e : { engine.get(), nextEngine.get(), starting.get() })
            if (e) { e->clickLevel = click.getToggleState() ? 0.5f : 0.0f; e->clickToMain = clickMain.getToggleState(); }
    }
    // a command from the visuals page
    void command(const juce::String& c)
    {
        const bool playing = engine && engine->isPlaying();
        if (c == "play") { if (!playing) togglePlay(); }
        else if (c == "stop") { if (playing) togglePlay(); }
        else if (c.startsWith("scene/"))
        {
            const auto id = c.fromFirstOccurrenceOf("scene/", false, false);
            int sel = id == "improv" ? kImprovId : id == "concert" ? kConcertId : 0;
            for (int i = 0; i < int(std::size(kScenes)); ++i) if (id == kScenes[i]) sel = i + 1;
            if (sel && sel != sceneBox.getSelectedId()) sceneBox.setSelectedId(sel, juce::sendNotificationSync);
        }
        else if (c.startsWith("earmusic/")) earMusic.setValue(c.fromFirstOccurrenceOf("earmusic/", false, false).getDoubleValue(), juce::sendNotificationSync);
        else if (c.startsWith("padthr/")) // the kick detector's threshold (the page's [ ] keys)
        {
            onset.thr = juce::jlimit(0.001f, 0.5f, c.fromFirstOccurrenceOf("padthr/", false, false).getFloatValue());
            props.getUserSettings()->setValue("padThr", double(onset.thr.load()));
        }
        else if (c.startsWith("earclick/")) earClick.setValue(c.fromFirstOccurrenceOf("earclick/", false, false).getDoubleValue(), juce::sendNotificationSync);
        else if (c.startsWith("click/")) { click.setToggleState(c.endsWith("1"), juce::dontSendNotification); applyClick(); }
        else if (c.startsWith("clickmain/"))
        {
            clickMain.setToggleState(c.endsWith("1"), juce::dontSendNotification);
            props.getUserSettings()->setValue("clickMain", clickMain.getToggleState());
            applyClick();
        }
        sendState();
    }
    // the app's controls for the page: what plays, which song, the click (sent only when something changed)
    void sendState()
    {
        juce::String j = "{\"playing\":" + juce::String(engine && engine->isPlaying() ? "true" : "false") + ",\"scene\":\"" + juce::String(scene)
                       + "\",\"click\":" + (click.getToggleState() ? "true" : "false") + ",\"clickMain\":" + (clickMain.getToggleState() ? "true" : "false")
                       + ",\"earMusic\":" + juce::String(earMusic.getValue(), 1) + ",\"earClick\":" + juce::String(earClick.getValue(), 1)
                       + ",\"padThr\":" + juce::String(onset.thr.load(), 5)
                       + ",\"error\":" + juce::JSON::toString(error) + ",\"scenes\":[";
        for (int i = 0; i < int(std::size(kScenes)); ++i) j << "[\"" << kScenes[i] << "\"," << juce::JSON::toString(juce::String::fromUTF8(kSceneTitles[i])) << "],";
        j << "[\"improv\",\"Improvisation\"],[\"concert\",\"The whole concert\"]]}";
        live.state(j);
    }

    // ---- the live link to the visuals page (LiveLink.h)
    LiveLink live;
    double outLatency() const
    {
        auto* d = const_cast<juce::AudioDeviceManager&>(deviceManager).getCurrentAudioDevice();
        return d ? (d->getOutputLatencyInSamples() + d->getCurrentBufferSizeSamples()) / d->getCurrentSampleRate() : 0.0;
    }
    // a song starts: its show, and when its first bar is heard on the app's clock
    void announce(Engine& e, int k, double head = 1e9) // head: where its playhead already is (1e9: just started)
    {
        if (!e.composer()) return;
        const double at = LiveLink::now() + (head < 1e8 ? -head : double(e.preRollSamples()) / loadedRate) + outLatency();
        juce::MemoryOutputStream j;
        j << showHead(e.scene, uint64_t(improv() ? chainSeed : seed), 20.0) << ",\"at\":" << juce::String(at, 4) << ",\"now\":" << juce::String(LiveLink::now(), 4) << ",\"k\":" << k
          << ",\"piece\":\"" << (improv() ? juce::String("improv") : juce::String(e.scene)) << "\"";
        if (improv()) j << ",\"plan\":" << juce::String(improvJson(improvSong(uint64_t(chainSeed), k)));
        j << "}";
        songAt = at;
        live.song(j.toString());
    }
    // the seven levels of a show.json block (roles, delay, reverb, master), from the engines' meters since the last tick
    struct MeterMark { double sq[kMaxTracks + 2] = {}; int64_t n[kMaxTracks + 2] = {}; };
    std::map<const Engine*, MeterMark> marks;
    std::atomic<double> masterSq { 0 };
    std::atomic<int64_t> masterN { 0 };
    double masterSqWas = 0;
    std::atomic<double> songAt { 0 }; // when the newest song's bar 0 was heard (app clock): the page's levels and clock follow its playhead
    int64_t masterNWas = 0;
    // On its own thread, 20 times a second: the message thread is busy for seconds at a time while the next song's
    // instruments load, and the page's picture should not stall with it.
    struct LevelThread : juce::Thread
    {
        std::function<void()> tick, hit;
        LevelThread() : juce::Thread("live link levels") {}
        void run() override { for (int i = 0; !threadShouldExit(); ++i) { hit(); if (i % 5 == 0) tick(); wait(10); } }
    } levelThread;
    void sendLevels()
    {
        double role[4] = {}, bus[2] = {};
        const juce::SpinLock::ScopedLockType sl(audioLock); // the engines stay put while their meters are read (a few dozen atomics)
        for (auto it = marks.begin(); it != marks.end();) it = it->first == engine.get() || it->first == oldEngine.get() ? std::next(it) : marks.erase(it);
        for (auto* e : { engine.get(), oldEngine.get() })
        {
            if (!e) continue;
            auto& mk = marks[e];
            const auto& info = sceneInfo(e->scene);
            for (int k = 0; k < info.numTracks + 2; ++k)
            {
                const auto& m = k < info.numTracks ? e->meters[k] : k == info.numTracks ? e->delayMeter : e->reverbMeter;
                const double sq = m.sumSquares.load();
                const int64_t n = m.samples.load();
                const double pw = n > mk.n[k] ? juce::jmax(0.0, sq - mk.sq[k]) / (double(n - mk.n[k]) / 2) : 0.0; // as the renderer: per frame
                mk.sq[k] = sq; mk.n[k] = n;
                if (k < info.numTracks) role[info.role[k]] += pw;
                else bus[k - info.numTracks] += pw;
            }
        }
        const double msq = masterSq.load(), mn = double(masterN.load());
        const double master = mn > double(masterNWas) ? std::sqrt(juce::jmax(0.0, msq - masterSqWas) / (mn - double(masterNWas))) : 0.0;
        masterSqWas = msq; masterNWas = int64_t(mn);
        if (live.clients() == 0 || !engine || !(engine->isPlaying() || oldEngine.get() != nullptr)) return;
        // where the newest song is, as heard: its playhead less what is still on its way to the speakers
        const double pos = engine->seconds.load() - outLatency();
        juce::String j = "{\"now\":" + juce::String(LiveLink::now(), 4) + ",\"at\":" + juce::String(LiveLink::now() + outLatency(), 4)
                       + ",\"song\":" + juce::String(songAt.load(), 4) + ",\"pos\":" + juce::String(pos, 4) + ",\"v\":[";
        for (int r = 0; r < 4; ++r) j << juce::String(std::sqrt(role[r]), 5) << ",";
        j << juce::String(std::sqrt(bus[0]), 5) << "," << juce::String(std::sqrt(bus[1]), 5) << "," << juce::String(master, 5)
          << "],\"in\":" << juce::String(onset.meterOut.load(), 5) << "}";
        live.post("lv", j);
    }

    int improvK = 0;
    int64_t chainSeed = 1;
    double changeAt = 1e9;
    std::unique_ptr<Engine> nextEngine, oldEngine;
    // the whole concert's next song, waiting for the audio thread to start it on the sample the click glide lands on
    // (startAt, in the playing song's samples, less the new song's pre-roll)
    std::unique_ptr<Engine> starting;
    int64_t startAt = 0;
    // The kick on the interface's input 1 lights the visuals' drum-pad effect (Kerem, 2026-11-15): the page's onset
    // detector (visuals/index.html, WORKLET), here on the app's own input, so the page needs no audio input of its own.
    struct Onset
    {
        float hp = 0, px = 0, lp = 0, fast = 0, slow = 0, peak = 0, meter = 0;
        double hi = 0, lo = 0;
        int hold = 0, refr = 0;
        std::atomic<float> thr { 0.02f }, meterOut { 0 }, lastPeak { 0 }, lastRatio { 0 };
        std::atomic<int> hits { 0 };
        void process(const float* x, int n, double sr) // audio thread
        {
            const float t = thr.load();
            for (int i = 0; i < n; ++i)
            {
                hp = 0.996f * (hp + x[i] - px); px = x[i]; // rumble out
                const float a = std::abs(hp), d = hp - lp;
                lp = hp;
                fast = juce::jmax(a, fast * 0.9985f);
                slow += (fast - slow) * 0.0004f;
                meter = juce::jmax(meter * 0.99995f, a);
                if (hold > 0) // 6 ms after the onset: the true peak and the brightness
                {
                    peak = juce::jmax(peak, a); hi += double(d) * d; lo += double(hp) * hp;
                    if (--hold == 0) { lastPeak = peak; lastRatio = float(hi / (lo + 1e-12)); hits.fetch_add(1); }
                }
                if (refr > 0) --refr;
                else if (fast > t && fast > slow * 2.2f)
                {
                    hold = int(0.006 * sr); peak = a; hi = lo = 0; refr = int(0.045 * sr);
                }
            }
            meterOut = meter;
        }
    } onset;
    int hitsSent = 0;
    void sendHits() // the level thread, every 10 ms
    {
        const int h = onset.hits.load();
        if (h == hitsSent) return;
        hitsSent = h;
        if (live.clients() > 0)
            live.post("hit", "{\"peak\":" + juce::String(onset.lastPeak.load(), 5) + ",\"ratio\":" + juce::String(onset.lastRatio.load(), 5) + "}");
    }
    std::unique_ptr<Loader> loader;
    std::vector<std::unique_ptr<Loader>> graveyard;
    struct Loaded { int gen; std::unique_ptr<Engine> engine; juce::String error; };
    std::vector<Loaded> results;
    std::mutex loadedMutex;
    int loadGen = 0;
    juce::SpinLock audioLock;
    juce::AudioBuffer<float> mixBuf;

    juce::ApplicationProperties props;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Composer> previewComposer;
    const Composer* composer = nullptr;
    juce::String error;
    double loadedRate = 0;
    int loadedBlock = 0;
    int64_t seed = 1;
    std::string scene = "berlin";
    float meterPeak = 0;

    juce::TextButton play { "Play" }, fresh { "New concert" }, audio { "Audio..." };
    juce::TextEditor seedBox;
    juce::ComboBox sceneBox;
    juce::ToggleButton click { "Click on out 3/4" }, clickMain { "Click on 1/2" };
    juce::Slider feedback;
    juce::Label feedbackLabel { {}, "Song feedback" }, outputLabel { {}, "Output" };
    juce::Slider output;
    std::atomic<float> outGain { 0.5f }, earMusicGain { 0.25f }, earClickGain { 1.0f };
    juce::Slider earMusic, earClick;
    juce::Label earMusicLabel { {}, "In-ear music (out 3/4)" }, earClickLabel { {}, "In-ear click" };
    juce::ToggleButton fxOn[3];
    juce::Slider fxKnob[std::size(kFxKnobs)];
    juce::Label fxLabel[std::size(kFxKnobs)];
    juce::ComboBox styleBox, timeBox;
    juce::Rectangle<int> fxUnit[3];
};

struct App : juce::JUCEApplication
{
    const juce::String getApplicationName() override { return "Lisztmachine"; }
    const juce::String getApplicationVersion() override { return "0.12.0"; }

    void initialise(const juce::String&) override
    {
        window = std::make_unique<Window>("Lisztmachine", tok::ground, juce::DocumentWindow::allButtons);
        window->setUsingNativeTitleBar(true);
        window->setContentOwned(new MainComponent(), true);
        window->setResizable(true, false);
        window->setResizeLimits(960, 690, 4000, 2000);
        window->centreWithSize(window->getWidth(), window->getHeight());
        window->setVisible(true);
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

    struct Window : juce::DocumentWindow
    {
        using DocumentWindow::DocumentWindow;
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION(App)
