#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

// --probe <plugin.vst3>: list what a plugin exposes (programs, parameters, state header).
static int probe(const juce::String& path)
{
    juce::AudioPluginFormatManager fm;
    fm.addFormat(std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> types;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(types, path);
    if (types.isEmpty()) { std::cerr << "no plugin in " << path << "\n"; return 1; }
    juce::String err;
    auto p = fm.createPluginInstance(*types[0], 48000.0, 512, err);
    if (!p) { std::cerr << "load failed: " << err << "\n"; return 1; }
    std::cout << "name " << p->getName() << "  programs " << p->getNumPrograms()
              << "  params " << p->getParameters().size() << "\n";
    for (int i = 0; i < juce::jmin(15, p->getNumPrograms()); ++i)
        std::cout << "  prog " << i << ": " << p->getProgramName(i) << "\n";
    for (auto* prm : p->getParameters())
        if (prm->getName(64).containsIgnoreCase("cutoff") || prm->getName(64).containsIgnoreCase("macro"))
            std::cout << "  param " << prm->getParameterIndex() << ": " << prm->getName(64)
                      << " = " << prm->getValue() << "\n";
    juce::MemoryBlock mb;
    p->getStateInformation(mb);
    std::cout << "state " << mb.getSize() << " bytes\n";
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize()))
        for (auto* c : xml->getChildIterator())
        {
            juce::MemoryBlock inner;
            inner.fromBase64Encoding(c->getAllSubText());
            std::cout << "  " << xml->getTagName() << "/" << c->getTagName() << " " << inner.getSize() << " bytes: ";
            for (int i = 0; i < juce::jmin(120, (int) inner.getSize()); ++i)
            { auto ch = ((const char*) inner.getData())[i]; std::cout << (ch >= 32 && ch < 127 ? ch : '.'); }
            std::cout << "\n";
        }
    return 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args(argv + 1, argc - 1);
    if (args[0] == "--probe") return probe(args[1]);
    return 0;
}
