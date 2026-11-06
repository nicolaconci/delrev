#include "PluginEditor.h"

using namespace juce;

StereoDelayHallAudioProcessorEditor::StereoDelayHallAudioProcessorEditor(StereoDelayHallAudioProcessor& p)
    : AudioProcessorEditor(&p), apvts(p.apvts) {
    titleLabel.setText("Stereo Delay + Hall Reverb", dontSendNotification);
    titleLabel.setJustificationType(Justification::centred);
    titleLabel.setColour(Label::textColourId, Colours::white);
    titleLabel.setFont(Font(FontOptions(20.0f, Font::bold)));
    addAndMakeVisible(titleLabel);

    const char* ids[numControls] = {"mixL", "mixR", "feedback", "timeL", "timeR", "modRate", "modDepth", "revDecay", "revMix"};
    const char* names[numControls] = {"Mix L", "Mix R", "Feedback", "Time L", "Time R", "Mod Rate", "Mod Depth", "Rev Decay", "Rev Mix"};

    for (int i = 0; i < numControls; ++i) {
        sliders[i].setSliderStyle(Slider::RotaryVerticalDrag);
        sliders[i].setTextBoxStyle(Slider::TextBoxBelow, false, 80, 18);
        sliders[i].setColour(Slider::rotarySliderFillColourId, Colours::orange);
        sliders[i].setColour(Slider::textBoxTextColourId, Colours::white);
        sliders[i].setColour(Slider::textBoxOutlineColourId, Colours::darkgrey);
        addAndMakeVisible(sliders[i]);

        labels[i].setText(names[i], dontSendNotification);
        labels[i].setJustificationType(Justification::centred);
        labels[i].setColour(Label::textColourId, Colours::lightgrey);
        addAndMakeVisible(labels[i]);

        attachments[i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, ids[i], sliders[i]);
    }

    setSize(660, 440);
}

StereoDelayHallAudioProcessorEditor::~StereoDelayHallAudioProcessorEditor() = default;

void StereoDelayHallAudioProcessorEditor::paint(Graphics& g) {
    g.fillAll(Colour(0xff1e1e24));
}

void StereoDelayHallAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel.setBounds(area.removeFromTop(28));

    const int cols = 3;
    const int rows = 3;
    const int cellW = area.getWidth() / cols;
    const int cellH = area.getHeight() / rows;

    for (int i = 0; i < numControls; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        auto cell = Rectangle<int>(area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH);
        labels[i].setBounds(cell.removeFromTop(18));
        sliders[i].setBounds(cell.reduced(6));
    }
}
