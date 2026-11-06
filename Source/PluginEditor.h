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
    juce::Slider sliders[8];
    juce::Label labels[8];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[8];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoDelayHallAudioProcessorEditor)
};
