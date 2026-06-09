#include "PluginProcessor.h"
#include "PluginEditor.h"

// JUCE entry point — called by every plugin format (AU, VST, etc.) to
// instantiate the AudioProcessor. Must live in the global namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dawalka::PluginProcessor();
}

namespace dawalka {

PluginProcessor::BusesProperties PluginProcessor::makeBusLayout()
{
    auto buses = BusesProperties();

#if JucePlugin_Build_VST3
    // Ableton rejects VST3 effects that declare no audio input bus. DAWalka
    // still behaves as a generator, but the VST3 wrapper needs a valid input.
    buses = buses.withInput ("Input", juce::AudioChannelSet::stereo(), true);
#endif

    return buses.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

PluginProcessor::PluginProcessor()
    : AudioProcessor (makeBusLayout()),
      backend (modelManager)
{
    // Sandbox-safe: use explicit ~/Library/Application Support path.
    // (See ModelManager::getModelsDirectory() for the full explanation.)
    auto appData = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile ("Library/Application Support")
                       .getChildFile ("DAWalka");
    appData.createDirectory();

    juce::PropertiesFile::Options opt;
    opt.applicationName     = "DAWalka";
    opt.filenameSuffix      = "settings";
    opt.osxLibrarySubFolder = "Application Support";
    opt.storageFormat       = juce::PropertiesFile::StorageFormat::storeAsXML;
    settings = std::make_unique<juce::PropertiesFile> (opt);

    modelManager.addListener (this);
    backend.addListener (this);
}

PluginProcessor::~PluginProcessor()
{
    backend.stop();
}

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources() {}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    updateHostInfoFromPlayHead();

    if (!renderActive.load() || !renderBuffer)
    {
        buffer.clear();
        return;
    }

    // Hold the lock for the whole render so setRenderBuffer() can't swap
    // the buffer out from under us mid-block.
    const juce::CriticalSection::ScopedLockType l (renderLock);
    if (!renderBuffer) { buffer.clear(); return; }

    const int outSamples   = buffer.getNumSamples();
    const int outChannels  = buffer.getNumChannels();
    const int bufChannels  = renderBuffer->getNumChannels();
    const int bufSamples   = renderBuffer->getNumSamples();
    if (bufSamples < 2 || bufChannels < 1)
    {
        renderActive.store (false);
        buffer.clear();
        return;
    }

    const double outSr = getSampleRate();
    const double ratio = (renderSrcSr > 0.0 && outSr > 0.0)
                       ? renderSrcSr / outSr : 1.0;

    int pos = renderPos.load();
    if (pos < 0) pos = 0;

    // Linear-interp read into output.  We use whole-sample stepping for
    // simplicity (small pitch shift is fine for preview).
    for (int i = 0; i < outSamples; ++i)
    {
        const double sF = pos + i * ratio;
        int          s0 = (int) sF;
        if (s0 < 0) s0 = 0;
        if (s0 >= bufSamples - 1)
        {
            // Past the end — stop (or wrap if looping)
            for (int ch = 0; ch < outChannels; ++ch)
                buffer.setSample (ch, i, 0.0f);
            if (renderLoop.load())
            {
                pos = -i;   // start over from the beginning
                continue;
            }
            renderActive.store (false);
            // Zero the rest of the buffer so we don't leak stale audio
            for (int j = i + 1; j < outSamples; ++j)
                for (int ch = 0; ch < outChannels; ++ch)
                    buffer.setSample (ch, j, 0.0f);
            return;
        }
        const float  fr = (float) (sF - s0);
        for (int ch = 0; ch < outChannels; ++ch)
        {
            const int srcCh = juce::jmin (ch, bufChannels - 1);
            const float* p  = renderBuffer->getReadPointer (srcCh);
            const float a = p[s0];
            const float b = p[s0 + 1];
            buffer.setSample (ch, i, a + (b - a) * fr);
        }
    }

    pos += (int) (outSamples * ratio);
    if (pos >= bufSamples)
    {
        if (renderLoop.load()) pos = pos % bufSamples;
        else                  { pos = bufSamples - 1; renderActive.store (false); }
    }
    renderPos.store (pos);
}

void PluginProcessor::setRenderBuffer (std::shared_ptr<juce::AudioBuffer<float>> buf, double srcSampleRate)
{
    const juce::CriticalSection::ScopedLockType l (renderLock);
    renderBuffer     = std::move (buf);
    renderSrcSr      = srcSampleRate > 0.0 ? srcSampleRate : 44100.0;
    renderLengthSec  = (renderBuffer && renderSrcSr > 0.0)
        ? renderBuffer->getNumSamples() / renderSrcSr : 0.0;
    renderPos.store (0);
    renderActive.store (false);
}

void PluginProcessor::playRender()
{
    if (!renderBuffer || renderBuffer->getNumSamples() == 0) return;
    renderPos.store (0);
    renderActive.store (true);
    stateBroadcaster.sendChangeMessage();
}

void PluginProcessor::stopRender()
{
    renderActive.store (false);
    renderPos.store (0);
    stateBroadcaster.sendChangeMessage();
}

void PluginProcessor::setRenderPosition (double sec)
{
    if (! renderBuffer || renderSrcSr <= 0.0) return;
    int totalSamples = renderBuffer->getNumSamples();
    int newPos = static_cast<int> (sec * renderSrcSr);
    newPos = juce::jlimit (0, totalSamples - 1, newPos);
    renderPos.store (newPos);
}

double PluginProcessor::getRenderPositionSec() const
{
    if (renderSrcSr <= 0.0) return 0.0;
    return renderPos.load() / renderSrcSr;
}

void PluginProcessor::updateHostInfoFromPlayHead()
{
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            if (pos->getBpm().hasValue())          hostBpm     = *pos->getBpm();
            if (pos->getIsPlaying())               hostPlaying = pos->getIsPlaying();
            if (pos->getTimeInSeconds().hasValue()) hostTimeSec = *pos->getTimeInSeconds();
        }
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (settings) settings->setValue ("hostBpm", hostBpm.load());
    if (settings) settings->save();
    if (settings)
    {
        juce::MemoryBlock fileData;
        if (settings->getFile().loadFileAsData (fileData))
            destData.insert (fileData.getData(), 0, fileData.getSize());
    }
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
    if (settings) settings->reload();
}

void PluginProcessor::backendStateChanged (BackendState)
{
    stateBroadcaster.sendChangeMessage();
}

void PluginProcessor::jobProgress (const Job&)
{
    stateBroadcaster.sendChangeMessage();
}

void PluginProcessor::jobFinished (const Job& job)
{
    if (job.result.ok && job.result.audioFile.existsAsFile())
    {
        HistoryEntry e;
        e.id          = job.id;
        e.timestamp   = job.finishedAt;
        e.modelId     = job.result.modelId;
        e.prompt      = job.request.prompt;
        e.bpm         = job.request.bpm;
        e.key         = job.request.key;
        e.durationSec = job.result.durationSec;
        e.audioFile   = job.result.audioFile;
        e.seed        = job.result.seed;
        // A2A jobs carry their metadata on a2aRequest instead of request.
        if (job.kind == "a2a")
        {
            e.kind           = "a2a";
            e.sourceFile     = job.a2aRequest.initAudio.getFullPathName();
            e.initNoiseLevel = job.a2aRequest.initNoiseLevel;
        }
        history.add (e);
    }
    stateBroadcaster.sendChangeMessage();
}

void PluginProcessor::modelStateChanged (const ModelState&)
{
    stateBroadcaster.sendChangeMessage();
}

} // namespace dawalka
