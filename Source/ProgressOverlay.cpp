#include "ProgressOverlay.h"
#include "LookAndFeel.h"

namespace dawalka {

ProgressOverlay::ProgressOverlay()
    : bar (progressVal)
{
    addAndMakeVisible (title);
    addAndMakeVisible (bar);
    addAndMakeVisible (progressLabel);
    addAndMakeVisible (timeLabel);
    addAndMakeVisible (cancelButton);

    title.setFont (juce::Font (16.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour::fromRGB (235, 237, 245));
    title.setJustificationType (juce::Justification::centred);

    progressLabel.setFont (juce::Font (12.0f));
    progressLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (180, 184, 200));
    progressLabel.setJustificationType (juce::Justification::centred);

    timeLabel.setFont (juce::Font (12.0f));
    timeLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (150, 156, 172));
    timeLabel.setJustificationType (juce::Justification::centred);

    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
}

ProgressOverlay::~ProgressOverlay() { stopTimer(); }

void ProgressOverlay::begin (const juce::String& titleText)
{
    title.setText (titleText, juce::dontSendNotification);
    progressVal = 0.0;
    etaSeconds = -1.0;
    startTime = juce::Time::getCurrentTime();
    setVisible (true);
    startTimerHz (10);
    repaint();
}

void ProgressOverlay::setProgress (double p)
{
    progressVal = juce::jlimit (0.0, 1.0, p);
    progressLabel.setText (juce::String (static_cast<int> (progressVal * 100.0)) + "%",
                           juce::dontSendNotification);
    // ProgressBar monitors progressVal by reference and repaints itself.
}

void ProgressOverlay::setEstimatedSecondsLeft (double s)
{
    etaSeconds = s;
    if (s >= 0.0)
        timeLabel.setText ("Estimated Time Remaining: " +
                           juce::String (static_cast<int> (s)) + " sec",
                           juce::dontSendNotification);
    else
        timeLabel.setText ("", juce::dontSendNotification);
    repaint();
}

void ProgressOverlay::cancel() { end(); }
void ProgressOverlay::end()   { setVisible (false); stopTimer(); }

void ProgressOverlay::timerCallback()
{
    if (etaSeconds < 0.0 && progressVal > 0.01)
    {
        auto elapsed = (juce::Time::getCurrentTime() - startTime).inSeconds();
        double eta = (elapsed / progressVal) * (1.0 - progressVal);
        setEstimatedSecondsLeft (eta);
    }
    repaint();
}

void ProgressOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (16, 18, 24).withAlpha (0.92f));
}

void ProgressOverlay::resized()
{
    auto r = getLocalBounds().reduced (40);
    auto block = r.withSizeKeepingCentre (r.getWidth(), 160);
    title.setBounds       (block.removeFromTop (28));
    block.removeFromTop (12);
    bar.setBounds         (block.removeFromTop (10));
    block.removeFromTop (12);
    progressLabel.setBounds (block.removeFromTop (20));
    block.removeFromTop (4);
    timeLabel.setBounds   (block.removeFromTop (20));
    block.removeFromTop (12);
    cancelButton.setBounds (block.removeFromTop (32).withSizeKeepingCentre (140, 32));
}

} // namespace dawalka
