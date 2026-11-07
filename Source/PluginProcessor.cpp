#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <atomic>
#include <functional>

using namespace juce;

static AudioParameterFloatAttributes makeAttrs(const String& label, int /*decimals*/) {
    return AudioParameterFloatAttributes()
        .withLabel(label)
        .withStringFromValueFunction([](float v, int) { return String((int) std::lround(v)); }) // round to integer
        .withValueFromStringFunction([](const String& s) { return s.getFloatValue(); });
}

AudioProcessorValueTreeState::ParameterLayout StereoDelayHallAudioProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout](const String& id, const String& name, NormalisableRange<float> range,
                              float def, const AudioParameterFloatAttributes& attrs) {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, range, def, attrs));
    };

    addFloat("preGain", "Pre Gain", {-24.0f, 24.0f, 0.1f}, 0.0f, makeAttrs("dB", 1));
    addFloat("master", "Master", {-48.0f, 12.0f, 0.1f}, 0.0f, makeAttrs("dB", 1));
    addFloat("mixL", "Mix L", {0.0f, 100.0f, 0.1f}, 30.0f, makeAttrs("%", 1));
    addFloat("toneL", "Tone L", {0.0f, 100.0f, 0.1f}, 100.0f, makeAttrs("%", 1));
    addFloat("mixR", "Mix R", {0.0f, 100.0f, 0.1f}, 30.0f, makeAttrs("%", 1));
    addFloat("toneR", "Tone R", {0.0f, 100.0f, 0.1f}, 100.0f, makeAttrs("%", 1));
    addFloat("feedbackL", "Feedback L", {0.0f, 95.0f, 0.1f}, 35.0f, makeAttrs("%", 1));
    addFloat("feedbackR", "Feedback R", {0.0f, 95.0f, 0.1f}, 35.0f, makeAttrs("%", 1));
    addFloat("smear", "Smear | Crystal", {-100.0f, 100.0f, 0.1f}, 0.0f, makeAttrs("%", 0));
    addFloat("swell", "Swell", {0.0f, 500.0f, 1.0f}, 0.0f, makeAttrs("ms", 0));

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
    addFloat("revWidth", "Width", {0.0f, 200.0f, 0.1f}, 100.0f, makeAttrs("%", 1));

    addFloat("wetGain", "Wet Gain", {-12.0f, 12.0f, 0.1f}, 0.0f, makeAttrs("dB", 1));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"wet100", 1}, "100% Wet", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"swellInput", 1}, "Swell on Input", false));

    return layout;
}

StereoDelayHallAudioProcessor::StereoDelayHallAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::stereo(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    preGainParam = apvts.getRawParameterValue("preGain");
    masterParam = apvts.getRawParameterValue("master");
    mixLParam = apvts.getRawParameterValue("mixL");
    toneLParam = apvts.getRawParameterValue("toneL");
    mixRParam = apvts.getRawParameterValue("mixR");
    toneRParam = apvts.getRawParameterValue("toneR");
    feedbackLParam = apvts.getRawParameterValue("feedbackL");
    feedbackRParam = apvts.getRawParameterValue("feedbackR");
    smearParam = apvts.getRawParameterValue("smear");
    swellParam = apvts.getRawParameterValue("swell");
    swellInputParam = apvts.getRawParameterValue("swellInput");
    timeLParam = apvts.getRawParameterValue("timeL");
    timeRParam = apvts.getRawParameterValue("timeR");
    modRateParam = apvts.getRawParameterValue("modRate");
    modDepthParam = apvts.getRawParameterValue("modDepth");
    revDecayParam = apvts.getRawParameterValue("revDecay");
    revMixParam = apvts.getRawParameterValue("revMix");
    revWidthParam = apvts.getRawParameterValue("revWidth");
    wet100Param = apvts.getRawParameterValue("wet100");
    wetGainParam = apvts.getRawParameterValue("wetGain");
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
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    // supports stereo->stereo, mono->stereo (insert on a mono track widens to
    // stereo), stereo->mono (mixdown), mono->mono; size()<=2 also covers disabled buses
    return mainIn.size() <= 2 && mainOut.size() <= 2;
}

void StereoDelayHallAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&) {
    ScopedNoDenormals noDenormals;

    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    dsp::EngineParams p;
    p.preGainDb = preGainParam->load();
    p.masterDb = masterParam->load();
    p.mixL = mixLParam->load() / 100.0f;
    p.toneL = toneLParam->load() / 100.0f;
    p.mixR = mixRParam->load() / 100.0f;
    p.toneR = toneRParam->load() / 100.0f;
    p.feedbackL = feedbackLParam->load() / 100.0f;
    p.feedbackR = feedbackRParam->load() / 100.0f;
    p.smear = smearParam->load() / 100.0f;
    p.swellMs = swellParam->load();
    p.swellOnInput = swellInputParam->load() > 0.5f;
    p.timeLms = timeLParam->load();
    p.timeRms = timeRParam->load();
    p.modRateHz = modRateParam->load();
    p.modDepthMs = modDepthParam->load();
    p.revDecaySec = revDecayParam->load();
    p.revMix = revMixParam->load() / 100.0f;
    p.revWidth = revWidthParam->load() / 100.0f;
    p.wetGainDb = wetGainParam->load();
    if (wet100Param->load() > 0.5f) {
        p.mixL = 1.0f;
        p.mixR = 1.0f;
    }
    engine.setParams(p);

    const int inCh = inBus.getNumChannels();
    const int outCh = outBus.getNumChannels();

    if (monoScratch.getNumSamples() < numSamples)
        monoScratch.setSize(2, numSamples, false, false, true);
    float* tmpL = monoScratch.getWritePointer(0);
    float* tmpR = monoScratch.getWritePointer(1);

    if (inCh >= 2) {
        std::copy(inBus.getReadPointer(0), inBus.getReadPointer(0) + numSamples, tmpL);
        std::copy(inBus.getReadPointer(1), inBus.getReadPointer(1) + numSamples, tmpR);
        // Some hosts upmix a mono track to a stereo bus by sending the signal on
        // the left channel only (right may carry denormals/tiny noise, not exact
        // zero). If the right input peak is negligible vs the left, treat it as
        // mono and duplicate, so the plugin widens correctly on a mono insert.
        if (outCh >= 2) {
            float peakL = 0.0f, peakR = 0.0f;
            for (int i = 0; i < numSamples; ++i) {
                peakL = std::max(peakL, std::fabs(tmpL[i]));
                peakR = std::max(peakR, std::fabs(tmpR[i]));
            }
            if (peakL > 1e-5f && peakR < 0.001f * peakL)
                std::copy(tmpL, tmpL + numSamples, tmpR);
        }
    } else if (inCh == 1) {
        const float* in = inBus.getReadPointer(0);
        std::copy(in, in + numSamples, tmpL);
        std::copy(in, in + numSamples, tmpR);
    } else {
        std::fill(tmpL, tmpL + numSamples, 0.0f);
        std::fill(tmpR, tmpR + numSamples, 0.0f);
    }

    engine.process(tmpL, tmpR, numSamples);

    if (outCh >= 2) {
        // stereo out (stereo->stereo, or mono->stereo widening)
        std::copy(tmpL, tmpL + numSamples, outBus.getWritePointer(0));
        std::copy(tmpR, tmpR + numSamples, outBus.getWritePointer(1));
    } else if (inCh >= 2 && outCh == 1) {
        // stereo -> mono: mix down
        float* out = outBus.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            out[i] = 0.5f * (tmpL[i] + tmpR[i]);
    } else if (outCh == 1) {
        // mono -> mono
        std::copy(tmpL, tmpL + numSamples, outBus.getWritePointer(0));
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
