#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/DelayHallEngine.h"

class StereoDelayHallAudioProcessor : public juce::AudioProcessor {
public:
    StereoDelayHallAudioProcessor();
    ~StereoDelayHallAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    friend class StereoDelayHallAudioProcessorEditor;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    dsp::StereoDelayHallEngine engine;
    int currentProgram = 0;
    juce::AudioBuffer<float> monoScratch;

    std::atomic<float>* preGainParam = nullptr;
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* mixLParam = nullptr;
    std::atomic<float>* mixRParam = nullptr;
    std::atomic<float>* toneLParam = nullptr;
    std::atomic<float>* toneRParam = nullptr;
    std::atomic<float>* feedbackLParam = nullptr;
    std::atomic<float>* feedbackRParam = nullptr;
    std::atomic<float>* smearParam = nullptr;
    std::atomic<float>* swellParam = nullptr;
    std::atomic<float>* swellInputParam = nullptr;
    std::atomic<float>* timeLParam = nullptr;
    std::atomic<float>* timeRParam = nullptr;
    std::atomic<float>* modRateParam = nullptr;
    std::atomic<float>* modDepthParam = nullptr;
    std::atomic<float>* revDecayParam = nullptr;
    std::atomic<float>* revMixParam = nullptr;
    std::atomic<float>* revWidthParam = nullptr;
    std::atomic<float>* wet100Param = nullptr;
    std::atomic<float>* wetGainParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoDelayHallAudioProcessor)
};
