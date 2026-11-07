#include "PluginEditor.h"

using namespace juce;

StereoDelayHallAudioProcessorEditor::StereoDelayHallAudioProcessorEditor(StereoDelayHallAudioProcessor& p)
    : AudioProcessorEditor(&p), apvts(p.apvts) {
    studioLookAndFeel = std::make_unique<StudioLookAndFeel>();
    setLookAndFeel(studioLookAndFeel.get());
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
        {"pitchL",     "Pitch L",    Colour(0xff4da3ff)}, // small, bottom of LEFT
        {"pitchTimeL", "Time",       Colour(0xff4da3ff)},
        {"timeR",      "Time R",     Colour(0xff4dff88)}, // RIGHT
        {"feedbackR",  "Feedback R", Colour(0xff4dff88)},
        {"toneR",      "Tone R",     Colour(0xff4dff88)},
        {"mixR",       "Mix R",      Colour(0xff4dff88)},
        {"pitchR",     "Pitch R",    Colour(0xff4dff88)}, // small, bottom of RIGHT
        {"pitchTimeR", "Time",       Colour(0xff4dff88)},
        {"master",     "Master",     Colour(0xffffa500)}, // MASTER
        {"smear",      "Smear|Crystal", Colour(0xffaaaaaa)}, // SMEAR
        {"swell",      "Swell",      Colour(0xffaaaaaa)},
        {"grit",       "Grit",       Colour(0xffaaaaaa)},
        {"modRate",    "Rate",       Colour(0xffc77dff)}, // MOD
        {"modDepth",   "Depth",      Colour(0xffc77dff)},
        {"modVol",     "Vol",        Colour(0xffc77dff)}, // small, bottom of MOD
        {"revDecay",   "Decay",      Colour(0xff40e0d0)}, // REV
        {"revMix",     "Mix",        Colour(0xff40e0d0)},
        {"revWidth",   "Width",      Colour(0xff40e0d0)},
        {"wetGain",    "Wet Gain",   Colour(0xffeeeeee)}, // WET
    };

    titleLabel.setText("essentia", dontSendNotification);
    titleLabel.setJustificationType(Justification::right);
    titleLabel.setColour(Label::textColourId, Colours::white);
    titleLabel.setFont(Font(FontOptions(26.0f, Font::bold)));
    addAndMakeVisible(titleLabel);

    taglineLabel.setText("simplicitas est summa subtilitas", dontSendNotification);
    taglineLabel.setJustificationType(Justification::centred);
    taglineLabel.setColour(Label::textColourId, Colour(0xff9a9fa8));
    taglineLabel.setFont(Font(FontOptions(13.0f, Font::italic)));
    addAndMakeVisible(taglineLabel);

    // single-knob sections (PRE, MASTER, ATTACK) show only the coloured group
    // title, not an extra label above the knob
    auto hasInnerLabel = [](int i) { return i != 0 && i != 13; };

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

    // bipolar knobs (smear|crystal, pitch L/R): double-click returns to center (off/0)
    sliders[14].setDoubleClickReturnValue(true, 0.0);
    sliders[5].setDoubleClickReturnValue(true, 0.0);
    sliders[11].setDoubleClickReturnValue(true, 0.0);

    swellInputButton.setButtonText("On Input");
    swellInputButton.setColour(ToggleButton::textColourId, Colours::white);
    addAndMakeVisible(swellInputButton);
    swellInputAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(apvts, "swellInput", swellInputButton);

    auto setupToggleLabel = [this](Label& l, const char* text, Colour c) {
        l.setText(text, dontSendNotification);
        l.setJustificationType(Justification::centred);
        l.setColour(Label::textColourId, c);
        addAndMakeVisible(l);
    };
    setupToggleLabel(swellInputLabel, "On Input", Colour(0xffaaaaaa));
    setupToggleLabel(wetLabel, "100% Wet", Colour(0xffeeeeee));

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

    setSize(1100, 580);
}

StereoDelayHallAudioProcessorEditor::~StereoDelayHallAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void StereoDelayHallAudioProcessorEditor::paint(Graphics& g) {
    StudioBackground::paintBrushedMetalPanel(g, getLocalBounds(), Colour(0xff2b2e34));
    StudioBackground::paintRecessedSection(g, groupPre.getBounds());
    StudioBackground::paintRecessedSection(g, groupLeft.getBounds());
    StudioBackground::paintRecessedSection(g, groupRight.getBounds());
    StudioBackground::paintRecessedSection(g, groupMaster.getBounds());
    StudioBackground::paintRecessedSection(g, groupSmear.getBounds());
    StudioBackground::paintRecessedSection(g, groupMod.getBounds());
    StudioBackground::paintRecessedSection(g, groupRev.getBounds());
    StudioBackground::paintRecessedSection(g, groupWet.getBounds());
    StudioBackground::paintScrew(g, Rectangle<float>(8.0f, 8.0f, 14.0f, 14.0f));
    StudioBackground::paintScrew(g, Rectangle<float>((float) getWidth() - 22.0f, 8.0f, 14.0f, 14.0f));
    StudioBackground::paintScrew(g, Rectangle<float>(8.0f, (float) getHeight() - 22.0f, 14.0f, 14.0f));
    StudioBackground::paintScrew(g, Rectangle<float>((float) getWidth() - 22.0f, (float) getHeight() - 22.0f, 14.0f, 14.0f));
}

void StereoDelayHallAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel.setBounds(area.removeFromTop(32));
    area.removeFromTop(6);
    taglineLabel.setBounds(area.removeFromBottom(18));
    area.removeFromBottom(4);

    auto hasInnerLabel = [](int i) { return i != 0 && i != 13; };
    auto placeKnob = [&](int i, Rectangle<int> cell) {
        if (hasInnerLabel(i))
            labels[i].setBounds(cell.removeFromTop(16));
        sliders[i].setBounds(cell.reduced(4));
    };
    auto cellOf = [](Rectangle<int> groupArea, int index, int count) {
        const int w = (groupArea.getWidth() - 20) / count;
        return Rectangle<int>(groupArea.getX() + 10 + index * w, groupArea.getY() + 18,
                              w, groupArea.getHeight() - 26);
    };
    auto singleCell = [](Rectangle<int> r) {
        return Rectangle<int>(r.getX() + 10, r.getY() + 22, r.getWidth() - 20, r.getHeight() - 30);
    };
    // small knob (pitch L/R, mod vol): label + compact knob centered
    auto placeSmall = [&](int i, Rectangle<int> bottomArea) {
        labels[i].setBounds(bottomArea.removeFromTop(14));
        const int d = std::min(52, bottomArea.getWidth() - 8);
        sliders[i].setBounds(bottomArea.withSizeKeepingCentre(d, d));
    };

    // Row 1: PRE | LEFT (4 knobs + Pitch L small) | RIGHT (4 knobs + Pitch R small) | MASTER
    auto row1 = area.removeFromTop(240);

    auto pre = row1.removeFromLeft(96);
    groupPre.setBounds(pre);
    placeKnob(0, singleCell(pre));
    row1.removeFromLeft(8);

    auto left = row1.removeFromLeft(400);
    groupLeft.setBounds(left);
    {
        auto la = Rectangle<int>(left.getX() + 6, left.getY() + 16, left.getWidth() - 12, left.getHeight() - 22);
        auto lb = la.removeFromBottom(70);
        for (int i = 0; i < 4; ++i)
            placeKnob(1 + i, cellOf(la, i, 4));
        auto lbHalf = lb.getWidth() / 2;
        placeSmall(5, Rectangle<int>(lb.getX(), lb.getY(), lbHalf, lb.getHeight()));      // Pitch L
        placeSmall(6, Rectangle<int>(lb.getX() + lbHalf, lb.getY(), lbHalf, lb.getHeight())); // Pitch Time L
    }
    row1.removeFromLeft(8);

    auto right = row1.removeFromLeft(400);
    groupRight.setBounds(right);
    {
        auto ra = Rectangle<int>(right.getX() + 6, right.getY() + 16, right.getWidth() - 12, right.getHeight() - 22);
        auto rb = ra.removeFromBottom(70);
        for (int i = 0; i < 4; ++i)
            placeKnob(7 + i, cellOf(ra, i, 4));
        auto rbHalf = rb.getWidth() / 2;
        placeSmall(11, Rectangle<int>(rb.getX(), rb.getY(), rbHalf, rb.getHeight()));      // Pitch R
        placeSmall(12, Rectangle<int>(rb.getX() + rbHalf, rb.getY(), rbHalf, rb.getHeight())); // Pitch Time R
    }
    row1.removeFromLeft(8);

    auto master = row1;
    groupMaster.setBounds(master);
    placeKnob(13, singleCell(master));

    // Row 2: SMEAR (3 knobs + On Input) | MOD (Rate, Depth + Vol small) | REV (3) | WET (Wet Gain + 100% Wet)
    auto row2 = area;
    row2.removeFromTop(8);

    auto smr = row2.removeFromLeft(330);
    groupSmear.setBounds(smr);
    {
        auto sa = Rectangle<int>(smr.getX() + 6, smr.getY() + 16, smr.getWidth() - 12, smr.getHeight() - 22);
        auto sb = sa.removeFromBottom(46);
        swellInputLabel.setBounds(sb.removeFromTop(16));
        swellInputButton.setBounds(sb.withSizeKeepingCentre(110, 22));
        for (int i = 0; i < 3; ++i)
            placeKnob(14 + i, cellOf(sa, i, 3));
    }
    row2.removeFromLeft(8);

    auto mod = row2.removeFromLeft(250);
    groupMod.setBounds(mod);
    {
        auto ma = Rectangle<int>(mod.getX() + 6, mod.getY() + 16, mod.getWidth() - 12, mod.getHeight() - 22);
        auto mb = ma.removeFromBottom(70);
        for (int i = 0; i < 2; ++i)
            placeKnob(17 + i, cellOf(ma, i, 2));
        placeSmall(19, mb); // Mod Vol
    }
    row2.removeFromLeft(8);

    auto rev = row2.removeFromLeft(300);
    groupRev.setBounds(rev);
    for (int i = 0; i < 3; ++i)
        placeKnob(20 + i, cellOf(Rectangle<int>(rev.getX() + 6, rev.getY() + 16, rev.getWidth() - 12, rev.getHeight() - 22), i, 3));
    row2.removeFromLeft(8);

    auto wet = row2;
    groupWet.setBounds(wet);
    {
        auto wa = Rectangle<int>(wet.getX() + 6, wet.getY() + 16, wet.getWidth() - 12, wet.getHeight() - 22);
        auto wb = wa.removeFromBottom(46);
        wetLabel.setBounds(wb.removeFromTop(16));
        wetButton.setBounds(wb.withSizeKeepingCentre(120, 24));
        placeKnob(23, wa);
    }
}
