#include <juce_gui_basics/juce_gui_basics.h>
struct App : juce::JUCEApplication {
    const juce::String getApplicationName() override { return "Lisztmachine"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override {}
    void shutdown() override {}
};
START_JUCE_APPLICATION(App)
