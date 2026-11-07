#include "PluginEditor.h"

using namespace juce;

StereoDelayHallAudioProcessorEditor::StereoDelayHallAudioProcessorEditor(StereoDelayHallAudioProcessor& p)
    : AudioProcessorEditor(&p), apvts(p.apvts) {
    struct KnobDef {
        const char* id;
        const char* name;
        Colour colour;
    };
    // Order matches the layout: row 1 = PRE | LEFT | RIGHT | MASTER, row 2 = ATTACK | MOD | REV | WET
    static const KnobDef defs[numKnobs] = {
        {"preGain",    "Pre Gain",   Colour(0xffffa500)}, // PRE
        {"timeL",      "Time L",     Colour(0xff4da3ff)}, // LEFT
        {"feedbackL",  "Feedback L", Colour(0xff4da3ff)},
        {"toneL",      "Tone L",     Colour(0xff4da3ff)},
        {"mixL",       "Mix L",      Colour(0xff4da3ff)},
        {"timeR",      "Time R",     Colour(0xff4dff88)}, // RIGHT
        {"feedbackR",  "Feedback R", Colour(0xff4dff88)},
        {"toneR",      "Tone R",     Colour(0xff4dff88)},
        {"mixR",       "Mix R",      Colour(0xff4dff88)},
        {"master",     "Master",     Colour(0xffffa500)}, // MASTER
        {"smear",      "Smear|Crystal", Colour(0xffaaaaaa)}, // SMEAR
        {"swell",      "Swell",      Colour(0xffaaaaaa)},
        {"modRate",    "Rate",       Colour(0xffc77dff)}, // MOD
        {"modDepth",   "Depth",      Colour(0xffc77dff)},
        {"revDecay",   "Decay",      Colour(0xff40e0d0)}, // REV
        {"revMix",     "Mix",        Colour(0xff40e0d0)},
    };

    titleLabel.setText("StereoDelayHall", dontSendNotification);
    titleLabel.setJustificationType(Justification::centred);
    titleLabel.setColour(Label::textColourId, Colours::white);
    titleLabel.setFont(Font(FontOptions(20.0f, Font::bold)));
    addAndMakeVisible(titleLabel);

    // single-knob sections (PRE, MASTER, ATTACK) show only the coloured group
    // title, not an extra label above the knob
    auto hasInnerLabel = [](int i) { return i != 0 && i != 9; };

    for (int i = 0; i < numKnobs; ++i) {
        sliders[i].setSliderStyle(Slider::RotaryVerticalDrag);
        sliders[i].setTextBoxStyle(Slider::TextBoxBelow, false, 80, 18);
        sliders[i].setColour(Slider::rotarySliderFillColourId, defs[i].colour);
        sliders[i].setColour(Slider::textBoxTextColourId, Colours::white);
        sliders[i].setColour(Slider::textBoxOutlineColourId, Colours::darkgrey);
        addAndMakeVisible(sliders[i]);

        labels[i].setText(defs[i].name, dontSendNotification);
        labels[i].setJustificationType(Justification::centred);
        labels[i].setColour(Label::textColourId, defs[i].colour);
        if (hasInnerLabel(i))
            addAndMakeVisible(labels[i]);

        attachments[i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, defs[i].id, sliders[i]);
    }

    // bipolar smear|crystal knob: double-click returns to center (off)
    sliders[10].setDoubleClickReturnValue(true, 0.0);

    swellInputButton.setButtonText("On Input");
    swellInputButton.setColour(ToggleButton::textColourId, Colours::white);
    addAndMakeVisible(swellInputButton);
    swellInputAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(apvts, "swellInput", swellInputButton);

    auto setupGroup = [](GroupComponent& g, const char* text, Colour c) {
        g.setText(text);
        g.setColour(GroupComponent::outlineColourId, c);
        g.setColour(GroupComponent::textColourId, c);
        g.setTextLabelPosition(Justification::centredLeft);
    };
    setupGroup(groupPre, "PRE", Colour(0xffffa500));
    setupGroup(groupLeft, "LEFT", Colour(0xff4da3ff));
    setupGroup(groupRight, "RIGHT", Colour(0xff4dff88));
    setupGroup(groupMaster, "MASTER", Colour(0xffffa500));
    setupGroup(groupSmear, "SMEAR", Colour(0xffaaaaaa));
    setupGroup(groupMod, "MOD", Colour(0xffc77dff));
    setupGroup(groupRev, "REV", Colour(0xff40e0d0));
    setupGroup(groupWet, "WET", Colour(0xffeeeeee));
    addAndMakeVisible(groupPre);
    addAndMakeVisible(groupLeft);
    addAndMakeVisible(groupRight);
    addAndMakeVisible(groupMaster);
    addAndMakeVisible(groupSmear);
    addAndMakeVisible(groupMod);
    addAndMakeVisible(groupRev);
    addAndMakeVisible(groupWet);

    wetButton.setButtonText("100% Wet");
    wetButton.setColour(ToggleButton::textColourId, Colours::white);
    addAndMakeVisible(wetButton);
    wetAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(apvts, "wet100", wetButton);

    setSize(1040, 470);
}

StereoDelayHallAudioProcessorEditor::~StereoDelayHallAudioProcessorEditor() = default;

void StereoDelayHallAudioProcessorEditor::paint(Graphics& g) {
    g.fillAll(Colour(0xff1e1e24));
}

void StereoDelayHallAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);

    auto cellOf = [](Rectangle<int> groupArea, int index, int count) {
        const int w = (groupArea.getWidth() - 8) / count;
        return Rectangle<int>(groupArea.getX() + 4 + index * w, groupArea.getY() + 14,
                              w, groupArea.getHeight() - 18);
    };
    auto hasInnerLabel = [](int i) { return i != 0 && i != 9; };
    auto placeKnob = [&](int i, Rectangle<int> cell) {
        if (hasInnerLabel(i))
            labels[i].setBounds(cell.removeFromTop(16));
        sliders[i].setBounds(cell.reduced(4));
    };
    auto singleCell = [](Rectangle<int> r) {
        return Rectangle<int>(r.getX() + 6, r.getY() + 18, r.getWidth() - 12, r.getHeight() - 22);
    };

    // Row 1: PRE | LEFT (Time L, Feedback L, Mix L) | RIGHT (Time R, Feedback R, Mix R) | MASTER
    auto row1 = area.removeFromTop(190);

    auto pre = row1.removeFromLeft(96);
    groupPre.setBounds(pre);
    placeKnob(0, singleCell(pre));
    row1.removeFromLeft(8);

    auto left = row1.removeFromLeft(400);
    groupLeft.setBounds(left);
    for (int i = 0; i < 4; ++i)
        placeKnob(1 + i, cellOf(left, i, 4));
    row1.removeFromLeft(8);

    auto right = row1.removeFromLeft(400);
    groupRight.setBounds(right);
    for (int i = 0; i < 4; ++i)
        placeKnob(5 + i, cellOf(right, i, 4));
    row1.removeFromLeft(8);

    auto master = row1;
    groupMaster.setBounds(master);
    placeKnob(9, singleCell(master));

    // Row 2: SMEAR | MOD (Rate, Depth) | REV (Decay, Mix) | WET (100% Wet button)
    auto row2 = area;
    row2.removeFromTop(8);

    auto smr = row2.removeFromLeft(280);
    groupSmear.setBounds(smr);
    auto smrArea = Rectangle<int>(smr.getX() + 4, smr.getY() + 14, smr.getWidth() - 8, smr.getHeight() - 18);
    swellInputButton.setBounds(smrArea.removeFromBottom(24).withSizeKeepingCentre(110, 22));
    for (int i = 0; i < 2; ++i)
        placeKnob(10 + i, cellOf(smrArea, i, 2));
    row2.removeFromLeft(8);

    auto mod = row2.removeFromLeft(220);
    groupMod.setBounds(mod);
    for (int i = 0; i < 2; ++i)
        placeKnob(12 + i, cellOf(mod, i, 2));
    row2.removeFromLeft(8);

    auto rev = row2.removeFromLeft(220);
    groupRev.setBounds(rev);
    for (int i = 0; i < 2; ++i)
        placeKnob(14 + i, cellOf(rev, i, 2));
    row2.removeFromLeft(8);

    auto wet = row2;
    groupWet.setBounds(wet);
    wetButton.setBounds(wet.withSizeKeepingCentre(120, 30).withY(wet.getCentreY() - 15));
}
