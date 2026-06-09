#include "LookAndFeel.h"

namespace dawalka {

DAWalkaLookAndFeel::DAWalkaLookAndFeel()
{
    background   = juce::Colour::fromRGB (16, 18, 24);
    surface      = juce::Colour::fromRGB (24, 27, 35);
    surfaceAlt   = juce::Colour::fromRGB (32, 35, 45);
    text         = juce::Colour::fromRGB (235, 237, 245);
    textMuted    = juce::Colour::fromRGB (150, 156, 172);
    accent       = juce::Colour::fromRGB (255, 122, 89);
    accentSoft   = juce::Colour::fromRGB (255, 122, 89).withAlpha (0.18f);
    success      = juce::Colour::fromRGB (90, 220, 150);
    warning      = juce::Colour::fromRGB (255, 200, 90);
    danger       = juce::Colour::fromRGB (255, 90, 100);

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId,  background);
    setColour (juce::TextButton::buttonColourId,          surfaceAlt);
    setColour (juce::TextButton::buttonOnColourId,        accent);
    setColour (juce::TextButton::textColourOnId,          text);
    setColour (juce::TextButton::textColourOffId,         text);
    setColour (juce::ComboBox::backgroundColourId,        surface);
    setColour (juce::ComboBox::textColourId,              text);
    setColour (juce::ComboBox::outlineColourId,           surfaceAlt);
    setColour (juce::ComboBox::arrowColourId,             accent);
    setColour (juce::PopupMenu::backgroundColourId,       surface);
    setColour (juce::PopupMenu::textColourId,             text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentSoft);
    setColour (juce::PopupMenu::highlightedTextColourId,  text);
    setColour (juce::TextEditor::backgroundColourId,     surface);
    setColour (juce::TextEditor::textColourId,           text);
    setColour (juce::TextEditor::outlineColourId,        surfaceAlt);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId,      accentSoft);
    setColour (juce::TextEditor::shadowColourId,         juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,                text);
    setColour (juce::ToggleButton::textColourId,         text);
    setColour (juce::ToggleButton::tickColourId,         accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textMuted);
    setColour (juce::ProgressBar::backgroundColourId,    surface);
    setColour (juce::ProgressBar::foregroundColourId,    accent);
    setColour (juce::Slider::thumbColourId,              accent);
    setColour (juce::Slider::trackColourId,              surfaceAlt);
    setColour (juce::Slider::backgroundColourId,         surfaceAlt);
    setColour (juce::ScrollBar::thumbColourId,           accent);
    setColour (juce::ScrollBar::trackColourId,           surface);
}

void DAWalkaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                               const juce::Colour& bg, bool isHighlighted, bool isDown)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = bg;
    if (isDown)            fill = accent;
    else if (isHighlighted) fill = surfaceAlt.brighter (0.15f);
    else                    fill = surfaceAlt;

    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (fill.brighter (0.2f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void DAWalkaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool isButtonDown,
                                       int buttonX, int buttonY, int buttonW, int buttonH,
                                       juce::ComboBox&)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (surface);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (surfaceAlt.brighter (0.2f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    // arrow
    juce::Path p;
    const float cx = buttonX + buttonW * 0.5f;
    const float cy = buttonY + buttonH * 0.5f;
    p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (accent);
    g.fillPath (p);
}

void DAWalkaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto r = b.getLocalBounds();
    auto box = juce::Rectangle<float> (4, r.getCentreY() - 6, 12, 12);

    g.setColour (b.getToggleState() ? accent : surface);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (b.getToggleState() ? accent : textMuted);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    if (b.getToggleState())
    {
        g.setColour (background);
        auto tick = juce::Rectangle<float> (box.getX() + 3, box.getY() + 3, 6, 6);
        g.fillRoundedRectangle (tick, 1.5f);
    }

    g.setColour (b.getToggleState() ? text : textMuted);
    g.setFont (juce::Font (14.0f));
    g.drawText (b.getButtonText(),
                juce::Rectangle<float> (24, 0, r.getWidth() - 24, r.getHeight()),
                juce::Justification::centredLeft);
}

void DAWalkaLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (e.hasKeyboardFocus (true) ? accent : surfaceAlt.brighter (0.2f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void DAWalkaLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (surface);
    g.fillRoundedRectangle (r, 6.0f);
}

void DAWalkaLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& pb,
                                          int w, int h, double progress, const juce::String& text)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (surface);
    g.fillRoundedRectangle (r, 4.0f);

    if (progress > 0.0)
    {
        auto filled = r.withWidth (r.getWidth() * static_cast<float> (progress));
        g.setColour (accent);
        g.fillRoundedRectangle (filled, 4.0f);
    }

    g.setColour (juce::Colour::fromRGB (235, 237, 245));
    g.setFont (juce::Font (12.0f));
    g.drawText (text, r, juce::Justification::centred);
}

void DAWalkaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                           float sliderPos, float, float,
                                           const juce::Slider::SliderStyle, juce::Slider&)
{
    auto trackY = y + h * 0.5f - 2.0f;
    auto track  = juce::Rectangle<float> (x, trackY, w, 4.0f);
    g.setColour (surfaceAlt);
    g.fillRoundedRectangle (track, 2.0f);

    auto filled = track.withWidth (sliderPos - x);
    g.setColour (accent);
    g.fillRoundedRectangle (filled, 2.0f);

    g.setColour (accent);
    g.fillEllipse (sliderPos - 6.0f, trackY - 5.0f, 12.0f, 14.0f);
}

} // namespace dawalka
