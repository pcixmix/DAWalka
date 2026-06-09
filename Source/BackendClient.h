#pragma once

#include "Common.h"
#include "ModelManager.h"

namespace dawalka {

// Status of the local Python backend
enum class BackendState
{
    Stopped,
    Starting,
    Running,
    Busy,
    Error,
    Crashed
};

juce::String backendStateToString (BackendState s);

// One generation job
struct Job
{
    juce::String          id          = dawalka::makeUuid();   // local C++ id
    juce::String          remoteId;                           // backend-assigned id (used for /job/{remoteId})
    GenerationRequest     request;                            // T2A payload
    A2aRequest            a2aRequest;                         // A2A payload (used when kind=="a2a")
    juce::String          kind        = "t2a";                // "t2a" | "a2a"
    GenerationResult      result;
    std::atomic<double>   progress    { 0.0 };
    std::atomic<bool>     cancelFlag  { false };
    std::atomic<bool>     done        { false };
    std::atomic<bool>     requestSubmitted { false };   // true after POST
    juce::Time            startedAt;
    juce::Time            finishedAt;
};

// Talks to the Python daemon over HTTP (uvicorn on 127.0.0.1:47823). Manages
// process lifecycle, queues jobs, and exposes Listener notifications.
class BackendClient : public juce::Thread,
                      private ModelManager::Listener
{
public:
    BackendClient (ModelManager& mm);
    ~BackendClient() override;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void start();                       // spawn the python process
    void stop();
    BackendState getState() const { return state.get(); }
    juce::String getLastError() const { return lastError; }

    // ── Job control ─────────────────────────────────────────────────────
    // Posts a job and returns its id. Listener will receive progress + done.
    juce::String submit (GenerationRequest req);

    // Audio-to-audio: regenerates `req.initAudio` in the style of `req.prompt`,
    // same length.  Goes to the python /a2a endpoint.
    juce::String submitA2a (A2aRequest req);

    // Cancel a job (in-flight or queued)
    void cancel (const juce::String& jobId);

    // Called by the output-folder safety net when a WAV appears in the
    // output directory that corresponds to a job we never got a "done"
    // notification for (e.g. polling failed, plugin reloaded, backend
    // restarted).  Marks the job complete with the given file as result
    // and returns the backend to Running so the Generate button re-enables.
    void acknowledgeCompletion (const juce::String& jobId, const juce::File& audioFile);

    // Change the folder where generated WAVs land.  The new path is
    // stashed in launcher_args.json so the next time the launcher
    // starts the python server, it passes --outputs <dir>.  Does NOT
    // restart the running backend by itself — the caller is expected
    // to call stop() + start() (or just submit a new job) afterwards.
    void setOutputDir (const juce::File& dir);
    juce::File getOutputDir() const;

    // Latest snapshot of a job
    std::shared_ptr<Job> getJob (const juce::String& id) const;

    // ── Listeners ───────────────────────────────────────────────────────
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void backendStateChanged (BackendState s) = 0;
        virtual void jobProgress (const Job& job) = 0;
        virtual void jobFinished (const Job& job) = 0;
    };
    void addListener    (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // ModelManager::Listener
    void modelStateChanged (const ModelState& s) override;
    void modelListChanged() override {}

    // juce::Thread — used to keep-alive pings & crash detection
    void run() override;

private:
    bool launchProcess();
    void killProcess();
    bool ping();
    void sendGenerateRequest (Job& j);
    void sendA2aRequest (Job& j);
    void setState (BackendState s, const juce::String& err = {});

    void notifyState();
    void notifyProgress (const Job& j);
    void notifyFinished (const Job& j);

    // Returns true if the backend reports it's busy
    bool queryJob (const Job& j);

    // Get the directory where the embedded python backend lives. Falls back
    // to the development tree.
    static juce::File findBackendDirectory();

    // HTTP helpers — minimal, no external deps
    juce::var httpGetJson (const juce::String& path);
    bool        httpPostJson (const juce::String& path, const juce::var& payload, juce::var* responseOut = nullptr);
    bool        httpPostFile (const juce::String& path, const juce::var& payload, const juce::File& bodyFile,
                              const juce::String& contentType, juce::var* responseOut);

    ModelManager&                 modelManager;
    juce::File                    backendDir;
    juce::ChildProcess            process;
    juce::File                    logFile;
    juce::File                    pidFile;
    juce::File                    launcherArgsFile;
    juce::File                    outputDir;
    ThreadSafeValue<BackendState> state;
    juce::String                  lastError;
    juce::ListenerList<Listener>  listeners;

    mutable juce::CriticalSection jobsMutex;
    std::unordered_map<juce::String, std::shared_ptr<Job>> jobs;
    std::shared_ptr<Job>         activeJob;     // currently running on backend

    int                          consecutiveFailures = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackendClient)
};

} // namespace dawalka
