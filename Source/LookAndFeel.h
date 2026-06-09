#pragma once

#include "Common.h"

namespace dawalka {

class DAWalkaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DAWalkaLookAndFeel();

    juce::Colour background;
    juce::Colour surface;
    juce::Colour surfaceAlt;
    juce::Colour text;
    juce::Colour textMuted;
    juce::Colour accent;
    juce::Colour accentSoft;
    juce::Colour success;
    juce::Colour warning;
    juce::Colour danger;

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& bg, bool isHighlighted, bool isDown) override;
    void drawComboBox (juce::Graphics& g, int w, int h, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override;
    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e) override;
    void drawProgressBar (juce::Graphics& g, juce::ProgressBar& pb,
                          int w, int h, double progress, const juce::String& text) override;
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider& s) override;
};

} // namespace dawalka
