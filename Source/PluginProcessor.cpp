#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

using namespace juce;

static AudioParameterFloatAttributes makeAttrs(const String& label, int decimals) {
    return AudioParameterFloatAttributes()
        .withLabel(label)
        .withStringFromValueFunction([decimals](float v, int) {
            if (decimals <= 0)
                return String((int) std::lround(v)); // clean integer, no trailing decimal point
            return String(v, decimals);
        })
        .withValueFromStringFunction([](const String& s) { return s.getFloatValue(); });
}

// rate label: 1 decimal above 1 Hz, 2 decimals below (so 0.05, 0.06, ... are visible near zero)
static AudioParameterFloatAttributes makeRateAttrs() {
    return AudioParameterFloatAttributes()
        .withLabel("Hz")
        .withStringFromValueFunction([](float v, int) {
            return v < 1.0f ? String(v, 2) : String(v, 1);
        })
        .withValueFromStringFunction([](const String& s) { return s.getFloatValue(); });
}

// ratio knob label: discrete steps 1.5:1, 2:1, 2.5:1, 3:1, 3.5:1, 4:1 vs the main delay time
static AudioParameterFloatAttributes makeRatioAttrs() {
    return AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) {
            const float r = std::clamp(std::round(v * 2.0f) * 0.5f, 1.5f, 4.0f);
            if (r == std::floor(r))
                return String((int) std::lround(r)) + ":1";
            return String(r, 1) + ":1"; // "2.5:1"
        })
        .withValueFromStringFunction([](const String& s) {
            float x = s.getFloatValue();
            const int colon = s.indexOfChar(':');
            if (colon > 0) {
                const float a = s.substring(0, colon).getFloatValue();
                const float b = s.substring(colon + 1).getFloatValue();
                if (a > 0.0f && b > 0.0f)
                    x = a / b;
            }
            return std::clamp(std::round(x * 2.0f) * 0.5f, 1.5f, 4.0f);
        });
}

AudioProcessorValueTreeState::ParameterLayout StereoDelayHallAudioProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout](const String& id, const String& name, NormalisableRange<float> range,
                              float def, const AudioParameterFloatAttributes& attrs) {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, range, def, attrs));
    };

    addFloat("preGain", "Pre Gain", {-24.0f, 24.0f, 0.1f}, 0.0f, makeAttrs("dB", 0));
    addFloat("master", "Master", {-48.0f, 12.0f, 0.1f}, 0.0f, makeAttrs("dB", 0));
    addFloat("mixL", "Mix L", {0.0f, 100.0f, 0.1f}, 30.0f, makeAttrs("%", 0));
    addFloat("toneL", "Tone L", {0.0f, 100.0f, 0.1f}, 100.0f, makeAttrs("%", 0));
    addFloat("mixR", "Mix R", {0.0f, 100.0f, 0.1f}, 30.0f, makeAttrs("%", 0));
    addFloat("toneR", "Tone R", {0.0f, 100.0f, 0.1f}, 100.0f, makeAttrs("%", 0));
    addFloat("feedbackL", "Feedback L", {0.0f, 95.0f, 0.1f}, 35.0f, makeAttrs("%", 0));
    addFloat("feedbackR", "Feedback R", {0.0f, 95.0f, 0.1f}, 35.0f, makeAttrs("%", 0));
    addFloat("smear", "Smear | Crystal", {-100.0f, 100.0f, 0.1f}, 0.0f, makeAttrs("%", 0));
    addFloat("swell", "Swell", {0.0f, 500.0f, 1.0f}, 0.0f, makeAttrs("ms", 0));

    NormalisableRange<float> timeRange(20.0f, 2000.0f, 0.1f);
    timeRange.setSkewForCentre(500.0f);
    addFloat("timeL", "Time L", timeRange, 375.0f, makeAttrs("ms", 0));
    addFloat("timeR", "Time R", timeRange, 500.0f, makeAttrs("ms", 0));

    NormalisableRange<float> rateRange(0.05f, 10.0f, 0.01f);
    rateRange.setSkewForCentre(1.0f);
    addFloat("modRate", "Mod Rate", rateRange, 0.8f, makeRateAttrs());
    addFloat("modDepth", "Mod Depth", {0.0f, 30.0f, 0.1f}, 4.0f, makeAttrs("ms", 0));

    NormalisableRange<float> decayRange(0.2f, 10.0f, 0.01f);
    decayRange.setSkewForCentre(2.5f);
    addFloat("revDecay", "Rev Decay", decayRange, 3.0f, makeAttrs("s", 0));
    addFloat("revMix", "Rev Mix", {0.0f, 100.0f, 0.1f}, 35.0f, makeAttrs("%", 0));
    addFloat("revWidth", "Width", {0.0f, 200.0f, 0.1f}, 100.0f, makeAttrs("%", 0));

    addFloat("wetGain", "Wet Gain", {-12.0f, 12.0f, 0.1f}, 0.0f, makeAttrs("dB", 0));
    addFloat("grit", "Grit", {0.0f, 100.0f, 0.1f}, 0.0f, makeAttrs("%", 0));
    addFloat("modVol", "Mod Vol", {0.0f, 100.0f, 0.1f}, 100.0f, makeAttrs("%", 0));
    addFloat("pitchL", "Pitch L", {-12.0f, 12.0f, 1.0f}, 0.0f, makeAttrs("st", 0));
    addFloat("pitchR", "Pitch R", {-12.0f, 12.0f, 1.0f}, 0.0f, makeAttrs("st", 0));
    NormalisableRange<float> ratioRange(1.5f, 4.0f, 0.5f); // interval 0.5, no skew -> 6 steps evenly spread on the knob
    addFloat("ratioL", "Ratio L", ratioRange, 2.0f, makeRatioAttrs());
    addFloat("ratioR", "Ratio R", ratioRange, 2.0f, makeRatioAttrs());
    addFloat("pitchMixL", "Pitch Mix L", {0.0f, 100.0f, 0.1f}, 50.0f, makeAttrs("%", 0));
    addFloat("pitchMixR", "Pitch Mix R", {0.0f, 100.0f, 0.1f}, 50.0f, makeAttrs("%", 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"wet100", 1}, "100% Wet", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"swellInput", 1}, "Swell on Input", false));

    return layout;
}

namespace {
struct FactoryPreset { const char* name; std::map<juce::String, float> values; };

const std::vector<FactoryPreset>& factoryPresets() {
    static const std::vector<FactoryPreset> presets = {
        { "Init", {} },
        { "Double Rhythm", {
            {"preGain", 0.0f}, {"master", 0.0f},
            {"timeL", 80.0f}, {"feedbackL", 5.0f}, {"toneL", 48.0f}, {"mixL", 0.0f},
            {"timeR", 30.0f}, {"feedbackR", 5.0f}, {"toneR", 74.0f}, {"mixR", 100.0f},
            {"smear", 11.0f}, {"swell", 0.0f}, {"swellInput", 0.0f},
            {"modRate", 0.05f}, {"modDepth", 21.0f},
            {"revDecay", 6.0f}, {"revMix", 0.0f}, {"revWidth", 150.0f},
            {"wetGain", 0.0f}, {"wet100", 0.0f},
            {"grit", 0.0f}, {"modVol", 100.0f}, {"pitchL", 0.0f}, {"pitchR", 0.0f},
            {"ratioL", 2.0f}, {"ratioR", 2.0f}, {"pitchMixL", 50.0f}, {"pitchMixR", 50.0f},
        }},
        { "Ambient", {
            {"preGain", 0.0f}, {"master", 0.0f},
            {"timeL", 220.0f}, {"feedbackL", 19.0f}, {"toneL", 36.0f}, {"mixL", 36.0f},
            {"timeR", 300.0f}, {"feedbackR", 17.0f}, {"toneR", 55.0f}, {"mixR", 34.0f},
            {"smear", -49.0f}, {"swell", 0.0f}, {"swellInput", 0.0f},
            {"modRate", 0.05f}, {"modDepth", 5.0f},
            {"revDecay", 5.0f}, {"revMix", 35.0f}, {"revWidth", 150.0f},
            {"wetGain", 0.0f}, {"wet100", 0.0f},
            {"grit", 0.0f}, {"modVol", 100.0f}, {"pitchL", 0.0f}, {"pitchR", 0.0f},
            {"ratioL", 2.0f}, {"ratioR", 2.0f}, {"pitchMixL", 50.0f}, {"pitchMixR", 50.0f},
        }},
        { "Clean Wide", {
            {"preGain", 0.0f}, {"master", 0.0f},
            {"timeL", 280.0f}, {"feedbackL", 33.0f}, {"toneL", 48.0f}, {"mixL", 39.0f},
            {"timeR", 380.0f}, {"feedbackR", 32.0f}, {"toneR", 48.0f}, {"mixR", 41.0f},
            {"smear", 33.0f}, {"swell", 205.0f}, {"swellInput", 0.0f},
            {"modRate", 0.05f}, {"modDepth", 21.0f},
            {"revDecay", 6.0f}, {"revMix", 60.0f}, {"revWidth", 150.0f},
            {"wetGain", 0.0f}, {"wet100", 0.0f},
            {"grit", 0.0f}, {"modVol", 100.0f}, {"pitchL", 0.0f}, {"pitchR", 0.0f},
            {"ratioL", 2.0f}, {"ratioR", 2.0f}, {"pitchMixL", 50.0f}, {"pitchMixR", 50.0f},
        }},
    };
    return presets;
}
} // namespace

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
    gritParam = apvts.getRawParameterValue("grit");
    modVolParam = apvts.getRawParameterValue("modVol");
    pitchLParam = apvts.getRawParameterValue("pitchL");
    pitchRParam = apvts.getRawParameterValue("pitchR");
    ratioLParam = apvts.getRawParameterValue("ratioL");
    ratioRParam = apvts.getRawParameterValue("ratioR");
    pitchMixLParam = apvts.getRawParameterValue("pitchMixL");
    pitchMixRParam = apvts.getRawParameterValue("pitchMixR");
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
    p.grit = gritParam->load() * 0.008f; // knob 0..100% -> engine 0..0.8: 100% = max useful saturation (raw 80%); RMS make-up keeps the volume constant
    p.modVol = modVolParam->load() / 100.0f;
    p.pitchL = pitchLParam->load();
    p.pitchR = pitchRParam->load();
    p.ratioL = ratioLParam->load();
    p.ratioR = ratioRParam->load();
    p.pitchMixL = pitchMixLParam->load() / 100.0f;
    p.pitchMixR = pitchMixRParam->load() / 100.0f;
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

int StereoDelayHallAudioProcessor::getNumPrograms() {
    return (int) factoryPresets().size();
}

int StereoDelayHallAudioProcessor::getCurrentProgram() {
    return currentProgram;
}

void StereoDelayHallAudioProcessor::setCurrentProgram(int index) {
    const auto& presets = factoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;
    currentProgram = index;
    const auto& preset = presets[(size_t) index];
    static const char* ids[] = {
        "preGain", "master", "mixL", "toneL", "mixR", "toneR", "feedbackL", "feedbackR",
        "smear", "swell", "swellInput", "timeL", "timeR", "modRate", "modDepth",
        "revDecay", "revMix", "revWidth", "wetGain", "wet100",
        "grit", "modVol", "pitchL", "pitchR", "ratioL", "ratioR", "pitchMixL", "pitchMixR",
    };
    for (const char* id : ids) {
        auto* p = apvts.getParameter(id);
        if (p == nullptr)
            continue;
        if (index == 0)
            p->setValueNotifyingHost(p->getDefaultValue()); // Init = defaults
        else if (auto it = preset.values.find(id); it != preset.values.end())
            p->setValueNotifyingHost(p->convertTo0to1(it->second));
    }
}

const String StereoDelayHallAudioProcessor::getProgramName(int index) {
    const auto& presets = factoryPresets();
    return (index >= 0 && index < (int) presets.size()) ? String(presets[(size_t) index].name) : String();
}

void StereoDelayHallAudioProcessor::changeProgramName(int, const String&) {}

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
