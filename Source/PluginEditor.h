#pragma once

#include "PluginProcessor.h"

namespace carve
{
    inline constexpr int kEditorWidth  = 820;
    inline constexpr int kEditorHeight = 540;

    namespace colours
    {
        inline const juce::Colour background { 0xff0a0a0d };
        inline const juce::Colour panel      { 0xff0d0e12 };
        inline const juce::Colour ref        { 0xff00d4ff };   // priority sources
        inline const juce::Colour bus        { 0xffb44cff };   // your (carved) content
        inline const juce::Colour carved     { 0xffe14ce8 };   // removed spectrum
        inline const juce::Colour active     { 0xff2ee6a8 };   // status: carving
        inline const juce::Colour standby    { 0xffe6b12e };   // status: sidechain silent
    }

    inline const char* kJedexSpotifyUrl  = "https://open.spotify.com/artist/2opw8FDhiTfWeGvbc6ZtEu";
    inline const char* kBigIceSpotifyUrl = "https://open.spotify.com/artist/6ms1OtzZNm2ONxOcTfSlme";
}

//==============================================================================
/** Metallic big knob + flat small knobs + Eco pill toggle + flat buttons. */
class CarveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    /** When auto-calibration is on the big knob is a bias trim, not a raw depth,
        so it draws a different centre read-out. Set on toggle, not per frame. */
    bool autoMode = true;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;
};

//==============================================================================
/**
    Horizontal mirrored spectrum view.

    x = log frequency (30 Hz .. 16 kHz), y = level, mirrored around the centre
    line. Blue = priority sources (sidechain), purple = your content after
    carving, magenta = the spectrum being removed right now. Includes a status
    pill (NO SIDECHAIN / STANDBY / CARVING ACTIVE + gain-reduction read-out)
    and a routing hint when no sidechain is connected.

    The static chrome (frame, grid, labels, legend) is cached in an Image.
*/
class SpectrumView : public juce::Component,
                     private juce::Timer
{
public:
    explicit SpectrumView (CarveAudioProcessor&);
    ~SpectrumView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildBackground();

    CarveAudioProcessor& processor;
    juce::Image bgCache;
    int bgScale = 1;

    static constexpr int kPts = 140;
    std::array<float, kPts> refCurve {}, mainCurve {}, carveCurve {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};

//==============================================================================
/** Click-to-dismiss overlay with the JeDEx and Big Ice logos linking to Spotify. */
class CreditsOverlay : public juce::Component
{
public:
    CreditsOverlay() { setVisible (false); }

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void ensureLogosLoaded();

    juce::Image jedexLogo, bigiceLogo;
    bool logosLoaded = false;
    juce::Rectangle<int> cardArea, jedexArea, bigiceArea, closeArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CreditsOverlay)
};

//==============================================================================
class CarveAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit CarveAudioProcessorEditor (CarveAudioProcessor&);
    ~CarveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    CarveAudioProcessor& processor;

    CarveLookAndFeel lookAndFeel;
    SpectrumView spectrum;

    juce::Slider amountKnob, smoothKnob, mixKnob, outputKnob;
    juce::ToggleButton ecoToggle { "ECO MODE" };
    juce::ToggleButton autoToggle { "AUTO CALIBRATE" };
    juce::TextButton creditsButton { "CREDITS" };
    CreditsOverlay credits;

    std::unique_ptr<SliderAttachment> amountAtt, smoothAtt, mixAtt, outputAtt;
    std::unique_ptr<ButtonAttachment> ecoAtt, autoAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveAudioProcessorEditor)
};
