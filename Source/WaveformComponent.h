#pragma once

#include "Common.h"

namespace dawalka {

class WaveformComponent : public juce::Component,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    WaveformComponent();
    ~WaveformComponent() override;

    // Load an audio file and create the thumbnail
    void loadFile (const juce::File& f);
    bool hasFile() const { return thumbnail != nullptr; }
    juce::File getFile() const { return currentFile; }

    // Transport
    void play();
    void stop();
    void toggleLoop();
    bool isPlaying() const;
    bool isLooping() const { return loop; }
    void setLooping (bool l) { loop = l; }

    // Called from mouseDown/mouseDrag when the user clicks/drags on
    // the waveform to seek.  The editor wires this to
    // processor.setRenderPosition() so the audio thread picks up the
    // new position on the next processBlock.  Pass `wasPlaying` to
    // tell the editor whether playback was active when the drag
    // started (it can decide whether to resume on release).
    std::function<void (double sec, bool wasPlaying)> onSeek;

    // Optional external playhead position (seconds) — used when the
    // component is driven by an external player (e.g. the audio
    // processor's render buffer) and the internal transport is not
    // running.  When negative, the component falls back to its
    // internal transport position.
    void setExternalPosition (double sec) { externalPos = sec; }

    // DragnDrop source
    void startDrag();

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void rebuildThumbnail (const juce::AudioBuffer<float>& buf, double sr);
    void seekToFraction (double f);

    std::unique_ptr<juce::AudioThumbnailCache> cache;
    std::unique_ptr<juce::AudioThumbnail>      thumbnail;
    juce::AudioFormatManager                   formatManager;
    juce::File                                 currentFile;
    juce::AudioTransportSource                 transport;
    juce::AudioSourcePlayer                    player;
    std::unique_ptr<juce::MixerAudioSource>    mixer;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;

    bool    loop            = true;
    double  lengthSec       = 0.0;
    double  sampleRate      = 44100.0;
    double  externalPos     = -1.0;
    bool    wasPlayingAtDrag = false;
};

} // namespace dawalka
