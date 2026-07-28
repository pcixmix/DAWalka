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
    setColour (juce::ScrollBar::thumbColourId,           juce::Colour::fromRGB (255, 122, 89).withAlpha (0.6f));
    setColour (juce::ScrollBar::trackColourId,           surface);
}

void DAWalkaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                               const juce::Colour& bg, bool isHighlighted, bool isDown)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = bg;
    bool isActive = b.getProperties().contains("active") && (bool)b.getProperties()["active"];
    if (isDown)            fill = accent;
    else if (isActive)     fill = accent;
    else if (isHighlighted) fill = surfaceAlt.brighter (0.15f);
    else                    fill = surfaceAlt;

    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);

    auto& props = b.getProperties();
    bool isOutlined = props.contains("outlined") && (bool)props["outlined"];
    bool isToggled = b.getToggleState();
    if (isOutlined && isToggled && !isDown)
    {
        g.setColour (accent);
        g.drawRoundedRectangle (r, 6.0f, 1.5f);
    }
    else
    {
        g.setColour (fill.brighter (0.2f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
}

void DAWalkaLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool isHighlighted, bool isDown)
{
    g.setFont (getButtonFont (b));
    g.setColour (b.findColour (isDown ? juce::TextButton::textColourOnId
                                      : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2),
                juce::Justification::centred, false);
}

void DAWalkaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool isButtonDown,
                                       int buttonX, int buttonY, int buttonW, int buttonH,
                                       juce::ComboBox&)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (surface);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (surfaceAlt.brighter (0.25f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    // arrow — slightly larger and more centered
    juce::Path p;
    const float cx = buttonX + buttonW * 0.5f;
    const float cy = buttonY + buttonH * 0.5f;
    p.addTriangle (cx - 4.5f, cy - 2.0f, cx + 4.5f, cy - 2.0f, cx, cy + 3.5f);
    g.setColour (accent);
    g.fillPath (p);
}

void DAWalkaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto r = b.getLocalBounds();
    const float boxW = 14.0f;
    const float boxH = 14.0f;
    auto box = juce::Rectangle<float> (3, r.getCentreY() - boxH * 0.5f, boxW, boxH);

    g.setColour (b.getToggleState() ? accent : surface);
    g.fillRoundedRectangle (box, 3.5f);
    g.setColour (b.getToggleState() ? accent : textMuted);
    g.drawRoundedRectangle (box, 3.5f, 1.0f);

    if (b.getToggleState())
    {
        // Draw a checkmark path instead of a filled square
        g.setColour (background);
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
        tick.lineTo (box.getCentreX(), box.getY() + boxH - 3.5f);
        tick.lineTo (box.getRight() - 2.0f, box.getY() + 3.0f);
        g.strokePath (tick, juce::PathStrokeType (1.8f));
    }

    g.setColour (b.getToggleState() ? text : textMuted);
    g.setFont (juce::Font (11.5f, juce::Font::bold));
    g.drawText (b.getButtonText(),
                juce::Rectangle<float> (24, 0, r.getWidth() - 24, r.getHeight()),
                juce::Justification::centredLeft);
}

void DAWalkaLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (e.hasKeyboardFocus (true) ? accent : surfaceAlt.brighter (0.25f));
    g.drawRoundedRectangle (r, 6.0f, e.hasKeyboardFocus (true) ? 1.5f : 1.0f);
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
                                            float sliderPos, float minSliderPos, float maxSliderPos,
                                            const juce::Slider::SliderStyle, juce::Slider&)
{
    auto trackY = y + h * 0.5f - 1.5f;
    auto track  = juce::Rectangle<float> (x + 2, trackY, w - 4, 3.0f);
    g.setColour (surfaceAlt.brighter (0.1f));
    g.fillRoundedRectangle (track, 1.5f);

    auto filled = track.withWidth (juce::jmax (0.0f, sliderPos - x - 2.0f));
    g.setColour (accent);
    g.fillRoundedRectangle (filled, 1.5f);

    // Thumb — circle with subtle shadow effect
    g.setColour (accent.withAlpha (0.3f));
    g.fillEllipse (sliderPos - 7.0f, trackY - 6.0f + 1.0f, 14.0f, 14.0f);
    g.setColour (accent);
    g.fillEllipse (sliderPos - 7.0f, trackY - 6.0f, 14.0f, 14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawEllipse (sliderPos - 7.0f, trackY - 6.0f, 14.0f, 14.0f, 1.0f);
}

} // namespace dawalka
