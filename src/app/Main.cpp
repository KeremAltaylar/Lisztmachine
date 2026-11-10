// Lisztmachine: the rehearsal panel. Plays a concert live through the audio device;
// outputs 1/2 are the PA mix, 3/4 the click for the drummer's in-ears.
#include "Engine.h"
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
        if (sceneBox.getSelectedId() == 0) { scene = "berlin"; sceneBox.setSelectedId(1, juce::dontSendNotification); }
        sceneBox.onChange = [this] {
            scene = kScenes[sceneBox.getSelectedId() - 1];
            props.getUserSettings()->setValue("scene", juce::String(scene));
            reload();
            preview();
        };

        auto saved = props.getUserSettings()->getXmlValue("audio");
        deviceManager.initialise(0, 4, saved.get(), true);
        deviceManager.addChangeListener(this);
        reload();

        seed = juce::jmax<int64_t>(1, props.getUserSettings()->getIntValue("seed", 1));
        seedBox.setText(juce::String(seed), false);
        seedBox.setInputRestrictions(10, "0123456789");
        seedBox.setJustification(juce::Justification::centred);
        seedBox.onReturnKey = [this] { seed = juce::jmax<int64_t>(1, seedBox.getText().getLargeIntValue()); preview(); };

        play.onClick = [this] { togglePlay(); };
        fresh.onClick = [this] {
            seed = juce::Random::getSystemRandom().nextInt({ 1, 1000000 });
            seedBox.setText(juce::String(seed), false);
            preview();
            if (engine && engine->isPlaying()) engine->start(uint64_t(seed));
        };
        audio.onClick = [this] { showAudioSettings(); };
        // the feedback bus (Kerem, 2026-11-09: "the feedback amount from 0 to 80 percent as a slider"), per song, remembered
        feedback.setSliderStyle(juce::Slider::LinearHorizontal);
        feedback.setRange(0, 80, 1);
        feedback.setTextValueSuffix(" %");
        feedback.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
        feedback.onValueChange = [this] {
            props.getUserSettings()->setValue("feedback." + juce::String(scene), feedback.getValue());
            if (engine) engine->feedbackBus = float(feedback.getValue() / 100.0);
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
        click.setToggleState(true, juce::dontSendNotification);
        click.onClick = [this] { if (engine) engine->clickLevel = click.getToggleState() ? 0.5f : 0.0f; };

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
        setSize(1000, 500);
        startTimerHz(20);
    }

    ~MainComponent() override
    {
        deviceManager.removeAudioCallback(this);
        if (auto xml = deviceManager.createStateXml()) props.getUserSettings()->setValue("audio", xml.get());
        props.getUserSettings()->setValue("seed", juce::String(seed));
        props.saveIfNeeded();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(tok::ground);
        auto r = getLocalBounds().reduced(tok::s5);

        g.setColour(tok::faint);
        g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
        g.drawText("LISZTMACHINE" + dot + juce::String(kSceneTitles[sceneBox.getSelectedId() - 1]).toUpperCase(), r.removeFromTop(16), juce::Justification::left);
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
        const auto& info = sceneInfo(scene);
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
            g.drawText("master " + juce::String(peak, 1) + " dBFS " + dot + " limiter " + juce::String(engine->limiter.maxReductionDb, 1)
                           + " dB" + (deviceManager.getCurrentAudioDevice() ? " " + dot + " " + deviceManager.getCurrentAudioDevice()->getName() : ""),
                       r.removeFromTop(18), juce::Justification::left);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(tok::s5);
        auto row = area.removeFromBottom(36);
        auto fbRow = area.removeFromBottom(tok::s3 + 28).removeFromTop(28);
        feedbackLabel.setBounds(fbRow.removeFromLeft(190));
        feedback.setBounds(fbRow.removeFromLeft(360));
        juce::FlexBox fb;
        // JUCE's FlexBox has no gap property, so each item carries the gap as a right margin.
        auto item = [](juce::Component& c, float w) { return juce::FlexItem(c).withWidth(w).withMargin({ 0, float(tok::s3), 0, 0 }); };
        fb.items = { item(sceneBox, 190), item(play, 100), item(fresh, 130), item(seedBox, 90), item(click, 150), item(audio, 110) };
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
        previewComposer = makeComposer(scene, uint64_t(seed));
        composer = previewComposer.get();
        props.getUserSettings()->setValue("seed", juce::String(seed));
        repaint();
    }

    void togglePlay()
    {
        if (!engine || error.isNotEmpty()) return;
        if (engine->isPlaying()) engine->stop();
        else
        {
            seed = juce::jmax<int64_t>(1, seedBox.getText().getLargeIntValue());
            engine->limiter.maxReductionDb = 0;
            engine->start(uint64_t(seed));
        }
    }

    // (Re)build the engine for the device's current rate and block size.
    void reload()
    {
        deviceManager.removeAudioCallback(this);
        auto* dev = deviceManager.getCurrentAudioDevice();
        loadedRate = dev ? dev->getCurrentSampleRate() : 48000.0;
        loadedBlock = dev ? dev->getCurrentBufferSizeSamples() : 512;
        engine = std::make_unique<Engine>();
        Palette palette;
        error = palette.load(findPalette(scene), scene);
        engine->scene = scene;
        if (error.isEmpty())
            error = dev ? engine->load(loadedRate, juce::jmax(512, loadedBlock), palette) : juce::String("no audio output device");
        if (error.isNotEmpty()) engine.reset();
        // the song's own amount until Kerem moves it (at most 80 %); a song without the bus greys the slider out
        const bool bus = engine && engine->hasFeedbackBus();
        const double own = engine ? juce::jmin(80.0, std::round(engine->palette().fbAmount * 100.0)) : 0.0;
        feedback.setValue(bus ? double(props.getUserSettings()->getDoubleValue("feedback." + juce::String(scene), own)) : 0.0, juce::dontSendNotification);
        feedback.setEnabled(bus);
        feedback.setAlpha(bus ? 1.0f : 0.35f);
        feedbackLabel.setText(bus ? "Feedback" : "Feedback (none in this song)", juce::dontSendNotification);
        if (engine && bus) engine->feedbackBus = float(feedback.getValue() / 100.0);
        deviceManager.addAudioCallback(this);
        repaint();
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
            if (engine->isPlaying() || engine->isFinished()) composer = engine->composer();
        }
        play.setButtonText(engine && engine->isPlaying() ? "Stop" : "Play");
        repaint();
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        if (engine) engine->render(out, numOut, n);
        else for (int c = 0; c < numOut; ++c) juce::FloatVectorOperations::clear(out[c], n);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

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
    juce::ToggleButton click { "Click on out 3/4" };
    juce::Slider feedback;
    juce::Label feedbackLabel { {}, "Feedback" };
};

struct App : juce::JUCEApplication
{
    const juce::String getApplicationName() override { return "Lisztmachine"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override
    {
        window = std::make_unique<Window>("Lisztmachine", tok::ground, juce::DocumentWindow::allButtons);
        window->setUsingNativeTitleBar(true);
        window->setContentOwned(new MainComponent(), true);
        window->setResizable(true, false);
        window->setResizeLimits(960, 500, 4000, 2000);
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
