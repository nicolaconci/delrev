#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class StereoDelayHallAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit StereoDelayHallAudioProcessorEditor(StereoDelayHallAudioProcessor&);
    ~StereoDelayHallAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label titleLabel;
    static constexpr int numControls = 9;
    juce::Slider sliders[numControls];
    juce::Label labels[numControls];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[numControls];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoDelayHallAudioProcessorEditor)
};
