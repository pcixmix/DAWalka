#include "WaveformComponent.h"

namespace dawalka {

WaveformComponent::WaveformComponent()
{
    formatManager.registerBasicFormats();
    cache     = std::make_unique<juce::AudioThumbnailCache> (4);
    thumbnail = std::make_unique<juce::AudioThumbnail> (256, formatManager, *cache);

    mixer = std::make_unique<juce::MixerAudioSource>();
    mixer->addInputSource (&transport, false);
    player.setSource (mixer.get());
    setOpaque (true);
    startTimerHz (30);
}

WaveformComponent::~WaveformComponent()
{
    transport.stop();
    transport.setSource (nullptr);
    player.setSource (nullptr);
}

void WaveformComponent::loadFile (const juce::File& f)
{
    if (f == currentFile && thumbnail != nullptr) return;
    stop();
    currentFile = f;
    externalPos = -1.0;   // reset external playhead too

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (!reader) return;

    sampleRate = reader->sampleRate;
    lengthSec  = reader->lengthInSamples / sampleRate;

    // Build thumbnail from the file directly (decoded lazily)
    thumbnail->setSource (new juce::FileInputSource (f));

    // Build a reader source for playback
    transport.stop();
    transport.setSource (nullptr);

    auto* newSource = new juce::AudioFormatReaderSource (reader.release(), true);
    transport.setSource (newSource, 0, nullptr, sampleRate);
    resampler = std::make_unique<juce::ResamplingAudioSource> (&transport, false, 2);
    mixer->removeAllInputs();
    mixer->addInputSource (resampler.get(), false);

    sendChangeMessage();
    repaint();
}

void WaveformComponent::play()
{
    if (!thumbnail) return;
    if (transport.isPlaying()) return;
    if (transport.getCurrentPosition() >= lengthSec - 0.05)
        transport.setPosition (0.0);
    transport.start();
}

void WaveformComponent::stop()
{
    transport.stop();
    transport.setPosition (0.0);
    repaint();
}

void WaveformComponent::toggleLoop()
{
    loop = !loop;
}

bool WaveformComponent::isPlaying() const
{
    return transport.isPlaying();
}

void WaveformComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (juce::Colour::fromRGB (16, 18, 24));

    if (thumbnail && thumbnail->getTotalLength() > 0.0)
    {
        g.setColour (juce::Colour::fromRGB (40, 44, 56));
        g.fillRoundedRectangle (r.toFloat().reduced (2.0f), 4.0f);
        g.setColour (juce::Colour::fromRGB (255, 122, 89));
        thumbnail->drawChannels (g, r.reduced (4), 0.0, thumbnail->getTotalLength(), 1.0f);

        // Playhead — prefer an externally-supplied position (used when
        // playback is driven by the audio processor, not by this
        // component's transport).  Falls back to the internal transport.
        double pos = (externalPos >= 0.0) ? externalPos
                                          : transport.getCurrentPosition();
        double total = thumbnail->getTotalLength();
        if (total > 0.0)
        {
            float px = r.getX() + 4 + (r.getWidth() - 8) * static_cast<float> (pos / total);
            g.setColour (juce::Colours::white);
            g.drawLine (px, r.getY() + 2, px, r.getBottom() - 2, 1.5f);
        }
    }
    else
    {
        g.setColour (juce::Colour::fromRGB (80, 84, 100));
        g.setFont (juce::Font (13.0f));
        g.drawText ("No audio yet - press Generate",
                    r, juce::Justification::centred);
    }
}

void WaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    if (!thumbnail || thumbnail->getTotalLength() <= 0.0) return;

    wasPlayingAtDrag = transport.isPlaying();
    transport.stop();

    // Seek the waveform's own transport so the playhead jumps visually
    // and isPlaying() reports false (so the editor's processor-driven
    // playback is in sync with what the user sees).
    seekToFraction (e.position.x / static_cast<double> (getWidth()));

    if (onSeek)
        onSeek (transport.getCurrentPosition(), wasPlayingAtDrag);
}

void WaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (!thumbnail) return;
    seekToFraction (e.position.x / static_cast<double> (getWidth()));
    if (onSeek)
        onSeek (transport.getCurrentPosition(), wasPlayingAtDrag);
}

void WaveformComponent::mouseUp (const juce::MouseEvent& e)
{
    // If playback was active when the user grabbed the waveform,
    // resume from the new position.  If it wasn't, leave the audio
    // paused — the user can press the Play button to listen from
    // the new spot.
    if (wasPlayingAtDrag)
    {
        transport.start();
        if (onSeek)
            onSeek (transport.getCurrentPosition(), true);
    }
}

void WaveformComponent::seekToFraction (double f)
{
    f = juce::jlimit (0.0, 1.0, f);
    double total = thumbnail->getTotalLength();
    transport.setPosition (f * total);
    repaint();
}

void WaveformComponent::timerCallback()
{
    if (loop && transport.isPlaying() &&
        transport.getCurrentPosition() >= lengthSec - 0.05)
    {
        transport.setPosition (0.0);
    }
    repaint();
}

void WaveformComponent::startDrag()
{
    if (!currentFile.existsAsFile()) return;

    juce::StringArray files;
    files.add (currentFile.getFullPathName());

    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, true, this);
}

} // namespace dawalka
