#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <atomic>
#include <functional>

using namespace juce;

static AudioParameterFloatAttributes makeAttrs(const String& label, int decimals) {
    return AudioParameterFloatAttributes()
        .withLabel(label)
        .withStringFromValueFunction([decimals](float v, int) { return String(v, decimals); })
        .withValueFromStringFunction([](const String& s) { return s.getFloatValue(); });
}

AudioProcessorValueTreeState::ParameterLayout StereoDelayHallAudioProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout](const String& id, const String& name, NormalisableRange<float> range,
                              float def, const AudioParameterFloatAttributes& attrs) {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, range, def, attrs));
    };

    addFloat("mixL", "Mix L", {0.0f, 100.0f, 0.1f}, 30.0f, makeAttrs("%", 1));
    addFloat("mixR", "Mix R", {0.0f, 100.0f, 0.1f}, 30.0f, makeAttrs("%", 1));
    addFloat("feedback", "Feedback", {0.0f, 95.0f, 0.1f}, 35.0f, makeAttrs("%", 1));

    NormalisableRange<float> timeRange(20.0f, 2000.0f, 0.1f);
    timeRange.setSkewForCentre(500.0f);
    addFloat("timeL", "Time L", timeRange, 375.0f, makeAttrs("ms", 1));
    addFloat("timeR", "Time R", timeRange, 500.0f, makeAttrs("ms", 1));

    NormalisableRange<float> rateRange(0.05f, 10.0f, 0.01f);
    rateRange.setSkewForCentre(1.0f);
    addFloat("modRate", "Mod Rate", rateRange, 0.8f, makeAttrs("Hz", 2));
    addFloat("modDepth", "Mod Depth", {0.0f, 30.0f, 0.1f}, 4.0f, makeAttrs("ms", 1));

    NormalisableRange<float> decayRange(0.2f, 10.0f, 0.01f);
    decayRange.setSkewForCentre(2.5f);
    addFloat("revDecay", "Rev Decay", decayRange, 3.0f, makeAttrs("s", 2));
    addFloat("revMix", "Rev Mix", {0.0f, 100.0f, 0.1f}, 35.0f, makeAttrs("%", 1));

    return layout;
}

StereoDelayHallAudioProcessor::StereoDelayHallAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::stereo(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    mixLParam = apvts.getRawParameterValue("mixL");
    mixRParam = apvts.getRawParameterValue("mixR");
    feedbackParam = apvts.getRawParameterValue("feedback");
    timeLParam = apvts.getRawParameterValue("timeL");
    timeRParam = apvts.getRawParameterValue("timeR");
    modRateParam = apvts.getRawParameterValue("modRate");
    modDepthParam = apvts.getRawParameterValue("modDepth");
    revDecayParam = apvts.getRawParameterValue("revDecay");
    revMixParam = apvts.getRawParameterValue("revMix");
}

StereoDelayHallAudioProcessor::~StereoDelayHallAudioProcessor() = default;

void StereoDelayHallAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate, samplesPerBlock);
    engine.reset();
    monoScratch.setSize(2, samplesPerBlock);
}

void StereoDelayHallAudioProcessor::releaseResources() {
    engine.reset();
}

bool StereoDelayHallAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();
    if (mainOut != AudioChannelSet::stereo() && mainOut != AudioChannelSet::mono())
        return false;
    return mainIn == mainOut;
}

void StereoDelayHallAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&) {
    ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() == 0)
        return;

    dsp::EngineParams p;
    p.mixL = mixLParam->load() / 100.0f;
    p.mixR = mixRParam->load() / 100.0f;
    p.feedback = feedbackParam->load() / 100.0f;
    p.timeLms = timeLParam->load();
    p.timeRms = timeRParam->load();
    p.modRateHz = modRateParam->load();
    p.modDepthMs = modDepthParam->load();
    p.revDecaySec = revDecayParam->load();
    p.revMix = revMixParam->load() / 100.0f;
    engine.setParams(p);

    float* left = buffer.getWritePointer(0);
    if (buffer.getNumChannels() >= 2) {
        float* right = buffer.getWritePointer(1);
        engine.process(left, right, numSamples);
    } else {
        if (monoScratch.getNumSamples() < numSamples)
            monoScratch.setSize(2, numSamples, false, false, true);
        float* tmpL = monoScratch.getWritePointer(0);
        float* tmpR = monoScratch.getWritePointer(1);
        std::copy(left, left + numSamples, tmpL);
        std::copy(left, left + numSamples, tmpR);
        engine.process(tmpL, tmpR, numSamples);
        std::copy(tmpL, tmpL + numSamples, left);
    }
}

AudioProcessorEditor* StereoDelayHallAudioProcessor::createEditor() {
    return new StereoDelayHallAudioProcessorEditor(*this);
}

double StereoDelayHallAudioProcessor::getTailLengthSeconds() const {
    return 2.0 + 10.0;
}

void StereoDelayHallAudioProcessor::getStateInformation(MemoryBlock& destData) {
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void StereoDelayHallAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(ValueTree::fromXml(*xml));
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new StereoDelayHallAudioProcessor();
}
