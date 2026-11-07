// ============================================================================
//  StudioLookAndFeel.h — LookAndFeel JUCE per GUI "hardware da studio"
//
//  USO:
//   1) Nel costruttore del PluginEditor:
//        lookAndFeel = std::make_unique<StudioLookAndFeel>();
//        setLookAndFeel (lookAndFeel.get());
//      (membro std::unique_ptr<StudioLookAndFeel>; nel distruttore:
//       setLookAndFeel (nullptr); PRIMA della distruzione dei membri)
//   2) Per lo sfondo: nel paint() del editor chiama
//        StudioBackground::paintBrushedMetalPanel (g, getLocalBounds(), ...);
//   3) I knob rotary prendono automaticamente l'aspetto "knob" qui sotto.
//      Per un look fotorealistico con immagini: vedi FilmstripKnob in fondo.
//
//  Nessuna risorsa esterna richiesta: tutto è disegnato proceduralmente
//  (gradienti radiali, anello metallico, tacche, vite a brugola).
// ============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel()
    {
        // Font tipico dei pannelli hardware (sostituisci con il tuo .ttf)
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xffe8b84b)); // ambra
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff22242a));
        setColour (juce::Slider::thumbColourId,               juce::Colour (0xffd8dbe0));
        setColour (juce::Label::textColourId,                 juce::Colour (0xffc9ccd2));
    }

    // ------------------------------------------------------------------
    //  KNOB REALISTICO
    // ------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto diameter     = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.72f;
        auto knobArea     = bounds.withSizeKeepingCentre (diameter, diameter);
        auto centre       = knobArea.getCentre();
        auto radius       = knobArea.getWidth() * 0.5f;

        auto angle = rotaryStartAngle
                   + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // --- 1. Base/scalatura attorno al knob --------------------------------
        //      anello scuro incassato nel pannello
        auto recess = knobArea.expanded (radius * 0.22f);
        g.setColour (juce::Colour (0xff101216));
        g.fillEllipse (recess);

        // --- 2. Arc di valore --------------------------------------------------
        auto arcRadius = recess.getWidth() * 0.5f + 2.0f;
        juce::Path arcBg;
        arcBg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        juce::PathStrokeType (arcRadius * 0.09f, juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded)
            .createStrokedPath (arcBg, arcBg);
        g.setColour (juce::Colour (0x50333338));
        g.fillPath (arcBg);

        if (s.isEnabled())
        {
            juce::Path arcFg;
            arcFg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, angle, true);
            juce::PathStrokeType (arcRadius * 0.09f, juce::PathStrokeType::curved,
                                  juce::PathStrokeType::rounded)
                .createStrokedPath (arcFg, arcFg);
            g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
            g.fillPath (arcFg);
        }

        // --- 3. Tacche della scala ----------------------------------------------
        for (int i = 0; i <= 11; ++i)
        {
            auto a = rotaryStartAngle
                   + (rotaryEndAngle - rotaryStartAngle) * (float) i / 11.0f;
            auto  dir   = juce::Point<float> (std::cos (a - juce::MathConstants<float>::halfPi),
                                              std::sin (a - juce::MathConstants<float>::halfPi));
            auto  inner = centre + dir * (arcRadius * 1.12f);
            auto  outer = centre + dir * (arcRadius * 1.12f + radius * 0.07f);
            auto  major = (i % 5 == 0);
            g.setColour (major ? juce::Colour (0xffe8e8e8) : juce::Colour (0xff7c8088));
            g.drawLine ({ inner, outer }, major ? 2.0f : 1.2f);
        }

        // --- 4. Corpo del knob: gradiente radiale con luce in alto a sinistra --
        {
            juce::ColourGradient bodyGrad (juce::Colour (0xff5a5f68),
                                            centre.x - radius * 0.4f,
                                            centre.y - radius * 0.4f,
                                            juce::Colour (0xff171a1f),
                                            centre.x + radius * 0.5f,
                                            centre.y + radius * 0.5f, false);
            g.setGradientFill (bodyGrad);
            g.fillEllipse (knobArea);

            // sottile riflesso a mezzaluna (style "champagne knob")
            juce::Path crescent;
            auto cArea = knobArea.reduced (radius * 0.06f);
            crescent.addEllipse (cArea);
            crescent.addEllipse (cArea.translated (0.0f, radius * 0.10f));
            crescent.setUsingNonZeroWinding (false);
            g.setColour (juce::Colour (0x28ffffff));
            g.fillPath (crescent);
        }

        // --- 5. Anello cromato esterno ------------------------------------------
        {
            juce::ColourGradient chrome (juce::Colour (0xffe6eaef),
                                         centre.x, centre.y - radius,
                                         juce::Colour (0xff6a7078),
                                         centre.x, centre.y + radius, false);
            g.setGradientFill (chrome);
            juce::Path ring;
            ring.addEllipse (knobArea);
            juce::Path hole;
            hole.addEllipse (knobArea.reduced (radius * 0.10f));
            ring.setUsingNonZeroWinding (false);
            // ring = anello esterno, hole lo svuota
            g.strokePath (ring, juce::PathStrokeType (radius * 0.05f));
        }

        // --- 6. Indicatore (linea) ----------------------------------------------
        {
            auto pointerLength = radius * 0.62f;
            auto pointerWidth  = radius * 0.14f;
            juce::Path pointer;
            pointer.addRoundedRectangle (juce::Rectangle<float> (-pointerWidth * 0.5f, -radius,
                                                  pointerWidth, pointerLength), pointerWidth * 0.5f);
            pointer.applyTransform (juce::AffineTransform::translation (centre.x, centre.y)
                                     .rotated (angle, centre.x, centre.y));
            // ombra della lineetta
            g.setColour (juce::Colour (0x66000000));
            g.fillPath (pointer, juce::AffineTransform::translation (1.5f, 2.0f));
            g.setColour (juce::Colours::white);
            g.fillPath (pointer);
        }
    }

    // ------------------------------------------------------------------
    //  LABEL / TESTI in stile serigrafia hardware
    // ------------------------------------------------------------------
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        auto textArea = label.getLocalBounds().toFloat();
        g.setFont (label.getFont());
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.f : 0.4f));
        g.drawText (label.getText(), textArea, juce::Justification::centred, true);
    }

    // Bottone toggle tipo switch metallico (per On/Off o preset)
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlight, bool down) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced (2.0f);
        auto on = b.getToggleState();
        auto track = bounds.reduced (3.0f);

        // guida a scarponcino: ben visibile anche da spento (più chiara + bordo),
        // ambrata quando acceso (alto contrasto)
        g.setColour (on ? juce::Colour (0xff4a3413) : juce::Colour (0xff232830));
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.28f);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.22f : 0.14f));
        g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.28f, 1.2f);

        // LED di stato: ambra acceso a destra, grigio spento a sinistra
        {
            auto ledR = 4.5f;
            auto ledX = on ? track.getRight() - 8.0f : track.getX() + 8.0f;
            auto ledC = juce::Point<float> (ledX, track.getCentreY());
            g.setColour (on ? juce::Colour (0xffe8b84b) : juce::Colour (0xff3a3d42));
            g.fillEllipse (juce::Rectangle<float> (ledC.x - ledR, ledC.y - ledR, ledR * 2.0f, ledR * 2.0f));
            if (on)
            {
                g.setColour (juce::Colour (0xffe8b84b).withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (ledC.x - ledR * 2.2f, ledC.y - ledR * 2.2f, ledR * 4.4f, ledR * 4.4f));
            }
        }

        // leva che scivola
        auto knobW = bounds.getWidth() * 0.46f;
        auto x = on ? track.getRight() - knobW : track.getX();
        auto lever = juce::Rectangle<float> (x, track.getY(), knobW, track.getHeight());
        juce::ColourGradient leverGrad (juce::Colour (0xfff2f5f8), lever.getCentreX(), lever.getY(),
                                        juce::Colour (0xff9aa0a8), lever.getCentreX(), lever.getBottom(), false);
        g.setGradientFill (leverGrad);
        g.fillRoundedRectangle (lever, lever.getHeight() * 0.28f);

        if (b.isEnabled() && b.isMouseOver())
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.28f);
        }
        juce::ignoreUnused (highlight, down);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioLookAndFeel)
};

// ============================================================================
//  SFONDO: pannello in alluminio spazzolato con viti (procedurale)
// ============================================================================
namespace StudioBackground
{
    // Richiama questa nel paint() del tuo PluginEditor.
    inline void paintBrushedMetalPanel (juce::Graphics& g, juce::Rectangle<int> bounds,
                                        juce::Colour baseColour = juce::Colour (0xff33373d))
    {
        auto w = bounds.getWidth(), h = bounds.getHeight();

        // 1. base + gradiente verticale (luce dall'alto)
        juce::ColourGradient bg (baseColour.brighter (0.18f), (float) w * 0.5f, 0.0f,
                                 baseColour.darker (0.25f),    (float) w * 0.5f, (float) h,
                                 false);
        g.setGradientFill (bg);
        g.fillAll();

        // 2. effetto "brushed metal": linee orizzontali semitrasparenti
        juce::Random rng (42); // seed fisso => aspetto stabile tra un frame e l'altro
        for (int yy = 0; yy < h; yy += 2)
        {
            auto alpha = (float) rng.nextDouble() * 0.045f;
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.drawHorizontalLine (yy, (float) bounds.getX(), (float) bounds.getRight());
        }

        // 3. vignettatura leggera ai bordi
        {
            juce::Rectangle<int> top { bounds.getX(), bounds.getY(), w, juce::jmax (2, h / 40) };
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (top); // scanalatura superiore tipo rack 19"
        }
    }

    // Vite a brugola riutilizzabile
    inline void paintScrew (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto c = r.getCentre();
        auto radius = r.getWidth() * 0.5f;
        juce::ColourGradient grad (juce::Colour (0xffb8bdc4), c.x - radius * 0.5f, c.y - radius * 0.5f,
                                   juce::Colour (0xff565b62), c.x + radius * 0.5f, c.y + radius * 0.5f, false);
        g.setGradientFill (grad);
        g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine ({ { c.x - radius * 0.55f, c.y - radius * 0.35f },
                      { c.x + radius * 0.55f, c.y + radius * 0.35f } }, radius * 0.16f);
    }

    // Pannello incassato per raggruppare i knob (tipo "sezione" del pannello)
    inline void paintRecessedSection (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto f = bounds.toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff14161a));
        g.fillRoundedRectangle (f, 6.0f);
        // bordo chiaro in basso = effetto incavo
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (f.translated (0.0f, 1.0f), 6.0f, 1.0f);
    }
}

// ============================================================================
//  ALTERNATIVA FOTOREALISTICA: knob da filmstrip PNG
//  Se vuoi il massimo del realismo, genera 100 frame di una manopola 3D
//  (es. con KnobMan https://www.g200kg.com/en/software/knobman.html,
//  o Blender + render sequence) ed esporta un filmstrip orizzontale.
// ============================================================================
class FilmstripKnob : public juce::Slider
{
public:
    FilmstripKnob (juce::Image filmstripImage, int numFrames_ = 100)
        : filmstrip (filmstripImage), numFrames (numFrames_)
    {
        frameHeight = filmstrip.getHeight();
        frameWidth  = filmstrip.getHeight(); // frame quadrati
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    }

    void paint (juce::Graphics& g) override
    {
        auto pos = (float) valueToProportionOfLength (getValue());
        auto frame = (int) juce::jlimit (0.0f, (float) numFrames - 1.0f,
                                         pos * (float) (numFrames - 1));
        auto dest = getLocalBounds().toFloat().withSizeKeepingCentre ((float) frameWidth,
                                                                      (float) frameHeight);
        g.drawImage (filmstrip.getClippedImage (
                        juce::Rectangle<int> (frame * frameWidth, 0, frameWidth, frameHeight)),
                     (int) dest.getX(), (int) dest.getY(), (int) dest.getWidth(), (int) dest.getHeight(),
                     0, 0, frameWidth, frameHeight);
    }

private:
    juce::Image filmstrip;
    int numFrames = 100, frameWidth = 0, frameHeight = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilmstripKnob)
};
