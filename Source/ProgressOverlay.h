#pragma once

#include "Common.h"

namespace dawalka {

class ProgressOverlay : public juce::Component,
                        public juce::Timer
{
public:
    ProgressOverlay();
    ~ProgressOverlay() override;

    void begin (const juce::String& titleText);
    void setProgress (double p);
    void setEstimatedSecondsLeft (double s);
    void cancel();
    void end();

    void setOnCancel (std::function<void()> cb) { onCancel = std::move (cb); }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    juce::Label       title;
    juce::Label       progressLabel;
    juce::Label       timeLabel;
    juce::TextButton  cancelButton { "Cancel" };

    double           progressVal = 0.0;  // declared before bar so bar's ref is valid
    juce::ProgressBar bar        { progressVal };

    double  etaSeconds       = -1.0;
    juce::Time startTime;
    std::function<void()> onCancel;
};

} // namespace dawalka
