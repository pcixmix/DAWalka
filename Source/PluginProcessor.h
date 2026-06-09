#pragma once

#include "Common.h"
#include "ModelManager.h"
#include "BackendClient.h"
#include "GenerationHistory.h"

namespace dawalka {

class PluginEditor;

class PluginProcessor : public juce::AudioProcessor,
                        public ModelManager::Listener,
                        public BackendClient::Listener
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override  { return kPluginDisplayName; }
    bool   acceptsMidi()  const override         { return false; }
    bool   producesMidi() const override         { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int    getNumPrograms() override             { return 1; }
    int    getCurrentProgram() override          { return 0; }
    void   setCurrentProgram (int) override      {}
    const juce::String getProgramName (int) override { return {}; }
    void   changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Public accessors used by the editor ─────────────────────────────
    ModelManager&       getModelManager()    { return modelManager; }
    BackendClient&      getBackend()         { return backend; }
    GenerationHistory&  getHistory()         { return history; }
    juce::PropertiesFile& getSettings()      { return *settings; }

    // Last BPM seen from the host
    double getHostBpm() const { return hostBpm.load(); }
    bool   isHostPlaying() const { return hostPlaying.load(); }
    double getHostTimeSec() const { return hostTimeSec.load(); }

    // ── Generator-style audio rendering ───────────────────────────────
    // DAWalka is a Generator AU: when the user clicks Play, the loaded
    // audio is rendered through processBlock so the host's transport
    // can play it.  Call setRenderBuffer() with the generated WAV
    // (decoded as AudioBuffer<float>), then playRender()/stopRender().
    void setRenderBuffer (std::shared_ptr<juce::AudioBuffer<float>> buf, double srcSampleRate);
    void playRender();
    void stopRender();
    // Move the playhead to a new position (seconds) without stopping
    // playback.  If we're currently rendering, the next processBlock
    // call will start reading samples from the new position.
    void setRenderPosition (double sec);
    bool isRendering() const { return renderActive.load(); }
    void setRenderLoop (bool l) { renderLoop = l; }
    bool getRenderLoop() const { return renderLoop; }
    double getRenderPositionSec() const;
    double getRenderLengthSec() const    { return renderLengthSec; }

    // State changes broadcast to the editor
    juce::ChangeBroadcaster stateBroadcaster;

    // BackendClient::Listener
    void backendStateChanged (BackendState s) override;
    void jobProgress   (const Job& job) override;
    void jobFinished   (const Job& job) override;

    // ModelManager::Listener
    void modelStateChanged (const ModelState& s) override;
    void modelListChanged() override { stateBroadcaster.sendChangeMessage(); }

private:
    static BusesProperties makeBusLayout();
    void updateHostInfoFromPlayHead();

    std::unique_ptr<juce::PropertiesFile> settings;
    ModelManager                          modelManager;
    BackendClient                         backend;
    GenerationHistory                     history;

    std::atomic<double> hostBpm        { 120.0 };
    std::atomic<bool>   hostPlaying    { false };
    std::atomic<double> hostTimeSec    { 0.0 };

    // Render buffer (decoded WAV) for generator-style playback
    std::shared_ptr<juce::AudioBuffer<float>> renderBuffer;
    std::atomic<int>     renderPos     { 0 };     // sample index into renderBuffer
    std::atomic<bool>    renderActive  { false };
    std::atomic<bool>    renderLoop    { true };
    double               renderSrcSr   = 44100.0;
    double               renderLengthSec = 0.0;
    juce::CriticalSection renderLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace dawalka
