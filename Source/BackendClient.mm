#include "BackendClient.h"
#include "ModelManager.h"

#if JUCE_MAC
#  include <Cocoa/Cocoa.h>
#  include <signal.h>
#  include <sys/types.h>
#endif

namespace dawalka {

juce::String backendStateToString (BackendState s)
{
    switch (s)
    {
        case BackendState::Stopped:  return "Stopped";
        case BackendState::Starting: return "Starting";
        case BackendState::Running:  return "Running";
        case BackendState::Busy:     return "Busy";
        case BackendState::Error:    return "Error";
        case BackendState::Crashed:  return "Crashed";
    }
    return "Unknown";
}

BackendClient::BackendClient (ModelManager& mm)
    : juce::Thread ("DAWalka-BackendClient"),
      modelManager (mm)
{
    backendDir = findBackendDirectory();
    // See ModelManager::getModelsDirectory() for why we don't use
    // userApplicationDataDirectory directly: in a sandboxed AU it resolves
    // to ~/Library/ rather than ~/Library/Application Support/, so log/pid
    // files would land in the wrong place.
    auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                    .getChildFile ("Library/Application Support")
                    .getChildFile ("DAWalka");
    home.createDirectory();
    logFile           = home.getChildFile (kBackendLogFileName);
    pidFile           = home.getChildFile (kBackendPidFileName);
    launcherArgsFile  = home.getChildFile ("launcher_args.json");
    // Default to ~/Documents/DAWalka/T2A.  The editor can change this
    // at runtime via setOutputDir(); the new value gets written to
    // launcher_args.json so the next launcher process picks it up.
    outputDir         = defaultOutputDir();
    outputDir.createDirectory();
    modelManager.addListener (this);
    // The plugin itself CANNOT fork() inside Logic Pro 11's sandboxed AU
    // host — posix_spawn is blocked and the plugin is killed.  We rely on
    // a small helper .app ("DAWalka Launcher.app") that lives next to the
    // Mach-O in Contents/MacOS/.  It runs OUTSIDE the plugin sandbox and
    // can fork the python server normally.  start() launches it via
    // NSWorkspace.launchApplicationAtURL (which IS allowed from sandboxed
    // processes) and then pings /health until the server is ready.
    setState (BackendState::Stopped, "Click GENERATE to start the backend");
    startThread (juce::Thread::Priority::low);   // <-- the run() loop MUST be alive

    // DIAG: log ctor completion to backend.log so we can confirm thread
    // is alive and the plugin is actually being loaded.
    logFile.appendText ("[ctor] BackendClient ctor done, thread started, backendDir="
                        + backendDir.getFullPathName() + "\n");
}

BackendClient::~BackendClient()
{
    stop();
    modelManager.removeListener (this);
}

juce::File BackendClient::findBackendDirectory()
{
    // 1. Inside the .component bundle (production install)
    auto bundle = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    auto inside = bundle.getChildFile ("Contents/Resources/python_backend");
    if (inside.isDirectory()) return inside;

    // 2. Dev tree: alongside the binary
    auto sibling = bundle.getParentDirectory().getChildFile ("python_backend");
    if (sibling.isDirectory()) return sibling;

    // 3. Project root
    auto root = bundle.getParentDirectory().getParentDirectory().getParentDirectory()
                    .getChildFile ("python_backend");
    if (root.isDirectory()) return root;

    // 4. HOME
    auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                    .getChildFile ("Library/Application Support/DAWalka/python_backend");
    if (home.isDirectory()) return home;

    return sibling;   // best-effort, may not exist
}

void BackendClient::setOutputDir (const juce::File& dir)
{
    auto d = dir;
    if (d.existsAsFile()) d = d.getParentDirectory();
    if (! d.exists()) d.createDirectory();
    outputDir = d;
    logFile.appendText ("[setOutputDir] " + d.getFullPathName() + "\n");
}

juce::File BackendClient::getOutputDir() const
{
    return outputDir;
}

void BackendClient::start()
{
    if (state.get() == BackendState::Running || state.get() == BackendState::Busy)
        return;
    logFile.appendText ("[start] " + juce::Time::getCurrentTime().toString(true,true) + "\n");
    setState (BackendState::Starting);
    consecutiveFailures = 0;
    notify(); // wake thread
}

void BackendClient::stop()
{
    killProcess();
    setState (BackendState::Stopped);
}

void BackendClient::run()
{
    logFile.appendText ("[run] thread started\n");
    while (!threadShouldExit())
    {
        const auto s = state.get();
        if (s == BackendState::Starting || s == BackendState::Crashed || s == BackendState::Error)
        {
            if (launchProcess())
            {
                // wait until it responds to /health
                const auto deadline = juce::Time::getCurrentTime() +
                                      juce::RelativeTime::milliseconds (kBackendStartupMs);
                bool alive = false;
                while (!threadShouldExit() && juce::Time::getCurrentTime() < deadline)
                {
                    if (ping())
                    {
                        alive = true;
                        break;
                    }
                    juce::Thread::sleep (kBackendPollMs);
                }
                if (alive)
                {
                    consecutiveFailures = 0;
                    logFile.appendText ("[run] alive! setState Running\n");
                    setState (BackendState::Running);

                    // Drain queued job that was waiting for the backend to
                    // come up (user clicked GENERATE while state was
                    // Stopped — submit() saved it as activeJob).
                    std::shared_ptr<Job> aj;
                    {
                        const juce::CriticalSection::ScopedLockType l (jobsMutex);
                        aj = activeJob;
                    }
                    if (aj && !aj->requestSubmitted.load())
                    {
                        if (aj->kind == "a2a")
                            sendA2aRequest (*aj);
                        else
                            sendGenerateRequest (*aj);
                    }
                }
                else
                {
                    logFile.appendText ("[run] NOT alive after deadline\n");
                    killProcess();
                    consecutiveFailures++;
                    if (consecutiveFailures >= 3)
                    {
                        setState (BackendState::Error, "Backend failed to start");
                    }
                    else
                    {
                        setState (BackendState::Crashed, "Startup timeout");
                        juce::Thread::sleep (2000);
                    }
                }
            }
            else
            {
                setState (BackendState::Error, lastError);
                juce::Thread::sleep (3000);
            }
        }
            else if (s == BackendState::Running || s == BackendState::Busy)
            {
                // Same handling for both states: we need to keep
                // pinging (so we can detect a dead backend) and we
                // need to keep polling the active job (so we can
                // detect completion).  The original code only
                // handled Running, which meant a job that failed
                // mid-process could leave the run thread dormant
                // forever (UI stuck on 0%).
                if (!ping())
                {
                    consecutiveFailures++;
                    if (consecutiveFailures >= 2)
                    {
                        killProcess();
                        setState (BackendState::Crashed, "Lost connection");
                    }
                }
            else
            {
                consecutiveFailures = 0;

                // Poll active job
                std::shared_ptr<Job> aj;
                {
                    const juce::CriticalSection::ScopedLockType l (jobsMutex);
                    aj = activeJob;
                }
                if (aj)
                {
                    if (!queryJob (*aj))
                    {
                        // backend doesn't know about it (e.g. restart) — drop
                        aj->result.ok    = false;
                        aj->result.error = "Backend lost the job";
                        aj->done         = true;
                        aj->finishedAt   = juce::Time::getCurrentTime();
                        notifyFinished (*aj);

                        const juce::CriticalSection::ScopedLockType l (jobsMutex);
                        activeJob.reset();
                    }
                    else if (aj->done)
                    {
                        const juce::CriticalSection::ScopedLockType l (jobsMutex);
                        activeJob.reset();
                        setState (BackendState::Running);
                    }
                    else
                    {
                        setState (BackendState::Busy);
                        notifyProgress (*aj);
                    }
                }
                else
                {
                    setState (BackendState::Running);
                }
            }
        }
        else if (s == BackendState::Busy)
        {
            // Just wait — the running block above handles poll
            juce::Thread::sleep (kBackendPollMs);
        }

        juce::Thread::sleep (kBackendPollMs);
    }
}

bool BackendClient::launchProcess()
{
#if JUCE_MAC
    logFile.appendText ("[launchProcess] enter, backendDir=" + backendDir.getFullPathName() + "\n");

    if (!backendDir.isDirectory())
    {
        lastError = "Backend directory missing: " + backendDir.getFullPathName();
        logFile.appendText ("[launchProcess] FAIL: backendDir missing\n");
        return false;
    }

    // Locate the helper .app — it sits next to the Mach-O in Contents/MacOS/
    auto bundle = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::File launcherApp = bundle.getChildFile ("Contents/MacOS/DAWalka Launcher.app");
    logFile.appendText ("[launchProcess] bundle=" + bundle.getFullPathName()
        + " launcher=" + launcherApp.getFullPathName() + "\n");
    if (!launcherApp.isDirectory())
    {
        // Fallback for dev tree: launcher next to the executable
        launcherApp = bundle.getChildFile ("DAWalka Launcher.app");
        logFile.appendText ("[launchProcess] fallback launcher=" + launcherApp.getFullPathName() + "\n");
    }
    if (!launcherApp.isDirectory())
    {
        lastError = "DAWalka Launcher.app not found in bundle";
        logFile.appendText ("[launchProcess] FAIL: launcher .app not found\n");
        return false;
    }

    // Pass the .component bundle path as argv[1] so the launcher can find
    // python_backend relative to it.  Use NSWorkspace.openURL: with a
    // file:// URL — that's the modern, sandbox-friendly API (the older
    // launchApplicationAtURL: hangs in Logic Pro 11's AU sandbox).
    // openURL goes through LaunchServices, which is always allowed
    // from sandboxed processes.
    //
    // We CAN'T pass extra argv (sandboxed apps launched via NSWorkspace
    // don't get custom argv).  Instead we write the current output dir
    // to launcher_args.json; the launcher reads it on startup and
    // forwards it to server.py via --outputs.
    {
        auto homeDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                          .getChildFile ("Library/Application Support")
                          .getChildFile ("DAWalka");
        juce::DynamicObject::Ptr args = new juce::DynamicObject();
        args->setProperty ("output_dir", outputDir.getFullPathName());
        args->setProperty ("models_dir",
            homeDir.getChildFile ("models").getFullPathName());
        args->setProperty ("log_file",  logFile.getFullPathName());
        args->setProperty ("pid_file",  pidFile.getFullPathName());
        args->setProperty ("port",      47823);
        juce::var v (args);
        auto json = juce::JSON::toString (v);
        launcherArgsFile.replaceWithText (json);
        logFile.appendText ("[launchProcess] wrote launcher_args.json: " + json + "\n");
    }

    NSURL* appUrl = [NSURL fileURLWithPath:
        [NSString stringWithUTF8String: launcherApp.getFullPathName().toRawUTF8()]];

    logFile.appendText ("[launchProcess] calling NSWorkspace.openURL\n");
    BOOL ok = [[NSWorkspace sharedWorkspace] openURL: appUrl];
    logFile.appendText (juce::String ("[launchProcess] NSWorkspace.openURL returned: ")
        + (ok ? "YES" : "NO") + " at " + juce::Time::getCurrentTime().toString(true,true) + "\n");

    if (!ok)
    {
        lastError = "NSWorkspace.openURL returned NO";
        return false;
    }
    return true;
#else
    lastError = "Backend launch not supported on this platform";
    return false;
#endif
}

void BackendClient::killProcess()
{
    // Best-effort: read pid file and SIGTERM the python server, if any.
    if (pidFile.existsAsFile())
    {
        auto pidText = pidFile.loadFileAsString().trim();
        if (pidText.isNotEmpty())
        {
            long pid = pidText.getLargeIntValue();
            if (pid > 0)
            {
                ::kill ((pid_t) pid, SIGTERM);
                juce::Thread::sleep (500);
                if (::kill ((pid_t) pid, 0) == 0)
                    ::kill ((pid_t) pid, SIGKILL);
            }
        }
        pidFile.deleteFile();
    }
}

bool BackendClient::ping()
{
    auto v = httpGetJson ("/health");
    return v.isObject() && v.getProperty ("ok", false);
}

juce::String BackendClient::submit (GenerationRequest req)
{
    logFile.appendText ("[submit] modelId=" + req.modelId + " state="
        + backendStateToString (state.get()) + "\n");

    auto j = std::make_shared<Job>();
    j->request  = std::move (req);
    j->kind     = "t2a";
    j->startedAt = juce::Time::getCurrentTime();

    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        jobs[j->id] = j;
        activeJob = j;
    }

    const auto s = state.get();
    if (s == BackendState::Stopped)
    {
        // First-time click: kick off the launcher .app, which runs the
        // python server in an unsandboxed process.  The run() thread
        // will pick up `activeJob` and POST /generate as soon as /health
        // responds (see the "alive" branch in run()).
        start();
        return j->id;
    }
    if (s == BackendState::Starting || s == BackendState::Crashed || s == BackendState::Error)
    {
        // Backend is in the middle of (re)starting.  Queue the job; the
        // run() thread will POST it once we transition to Running.
        return j->id;
    }

    // Backend is Running or Busy — POST immediately.
    sendGenerateRequest (*j);
    return j->id;
}

juce::String BackendClient::submitA2a (A2aRequest req)
{
    logFile.appendText ("[submitA2a] modelId=" + req.modelId + " init=" + req.initAudio.getFileName()
        + " σmax=" + juce::String (req.initNoiseLevel, 2) + " state="
        + backendStateToString (state.get()) + "\n");

    auto j = std::make_shared<Job>();
    j->a2aRequest = std::move (req);
    j->kind       = "a2a";
    j->startedAt  = juce::Time::getCurrentTime();

    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        jobs[j->id] = j;
        activeJob = j;
    }

    const auto s = state.get();
    if (s == BackendState::Stopped)
    {
        start();
        return j->id;
    }
    if (s == BackendState::Starting || s == BackendState::Crashed || s == BackendState::Error)
    {
        return j->id;
    }

    sendA2aRequest (*j);
    return j->id;
}

void BackendClient::sendGenerateRequest (Job& j)
{
    j.requestSubmitted.store (true);

    juce::var response;
    if (!httpPostJson ("/generate", j.request.toVar(), &response))
    {
        j.result.ok    = false;
        j.result.error = "Failed to reach backend";
        j.done         = true;
        j.finishedAt   = juce::Time::getCurrentTime();
        notifyFinished (j);
        return;
    }

    // Capture the backend-assigned id — the run() thread polls /job/{remoteId}
    // using THIS id, not the local C++ UUID.  Without it the backend always
    // returns 404 and the UI stays stuck on "Generating...".
    j.remoteId = response.getProperty ("job_id", "").toString();
    if (j.remoteId.isEmpty())
    {
        j.result.ok    = false;
        j.result.error = "Backend did not return a job id";
        j.done         = true;
        j.finishedAt   = juce::Time::getCurrentTime();
        notifyFinished (j);
        return;
    }

    // POST /generate only confirms the job was QUEUED — it does NOT contain
    // the audio file (the job is still running on the backend).  We do NOT
    // call notifyFinished here; the run() thread will poll /job/{remoteId}
    // and call notifyFinished with the real result once the job actually
    // completes.  Calling notifyFinished now (with an empty audio path)
    // would make jobFinished see a "successful" job with no file and skip
    // adding it to the history.

    if (! response.getProperty ("ok", false))
    {
        j.result.ok    = false;
        j.result.error = response.getProperty ("error", "Unknown error").toString();
        j.done         = true;
        j.finishedAt   = juce::Time::getCurrentTime();
        notifyFinished (j);
    }
}

void BackendClient::sendA2aRequest (Job& j)
{
    j.requestSubmitted.store (true);

    juce::var response;
    if (!httpPostJson ("/a2a", j.a2aRequest.toVar(), &response))
    {
        j.result.ok    = false;
        j.result.error = "Failed to reach backend";
        j.done         = true;
        j.finishedAt   = juce::Time::getCurrentTime();
        notifyFinished (j);
        return;
    }

    j.remoteId = response.getProperty ("job_id", "").toString();
    if (j.remoteId.isEmpty())
    {
        j.result.ok    = false;
        j.result.error = "Backend did not return a job id";
        j.done         = true;
        j.finishedAt   = juce::Time::getCurrentTime();
        notifyFinished (j);
        return;
    }

    // Same as sendGenerateRequest: POST only confirms QUEUED, the
    // run() thread polls /job/{remoteId} and notifies finished.
    if (! response.getProperty ("ok", false))
    {
        j.result.ok    = false;
        j.result.error = response.getProperty ("error", "Unknown error").toString();
        j.done         = true;
        j.finishedAt   = juce::Time::getCurrentTime();
        notifyFinished (j);
    }
}

void BackendClient::cancel (const juce::String& jobId)
{
    std::shared_ptr<Job> aj;
    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        if (activeJob && activeJob->id == jobId) aj = activeJob;
    }
    if (aj) aj->cancelFlag = true;
    // Send the backend-assigned id (if we have it) so the worker's
    // worker.cancel(remoteId) actually finds the job.
    const auto idToCancel = (aj && aj->remoteId.isNotEmpty()) ? aj->remoteId : jobId;
    httpPostJson ("/cancel", juce::var (idToCancel), nullptr);
}

void BackendClient::acknowledgeCompletion (const juce::String& jobId, const juce::File& audioFile)
{
    std::shared_ptr<Job> aj;
    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        auto it = jobs.find (jobId);
        if (it != jobs.end()) aj = it->second;
    }
    if (!aj) return;

    if (aj->done) return;   // already handled

    aj->result.ok         = true;
    aj->result.audioFile  = audioFile;
    aj->result.sampleRate = aj->request.sampleRate;
    aj->done              = true;
    aj->finishedAt        = juce::Time::getCurrentTime();
    notifyFinished (*aj);

    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        if (activeJob && activeJob->id == jobId)
            activeJob.reset();
    }
    setState (BackendState::Running);
}

bool BackendClient::queryJob (const Job& j)
{
    // Use the backend-assigned id, not the local C++ UUID.  Without this
    // the backend's /job/{remoteId} handler would always 404 and the UI
    // would stay stuck on "Generating..." forever.
    const auto idToQuery = j.remoteId.isNotEmpty() ? j.remoteId : j.id;

    auto v = httpGetJson ("/job/" + idToQuery);
    if (!v.isObject()) return false;
    auto status = v.getProperty ("status", "").toString();

    if (status == "done")
    {
        const_cast<Job&> (j).result.audioFile = juce::File (v.getProperty ("audio", "").toString());
        const_cast<Job&> (j).result.durationSec = v.getProperty ("duration", 0.0);
        const_cast<Job&> (j).result.sampleRate  = v.getProperty ("sample_rate", 44100);
        const_cast<Job&> (j).result.numChannels = v.getProperty ("channels", 2);
        const_cast<Job&> (j).result.modelId     = v.getProperty ("model", j.request.modelId);
        const_cast<Job&> (j).result.seed        = v.getProperty ("seed", 0);
        const_cast<Job&> (j).result.generationTimeMs = v.getProperty ("generation_time_ms", 0.0);
        const_cast<Job&> (j).result.ok = true;
        const_cast<Job&> (j).progress = 1.0;
        const_cast<Job&> (j).done = true;
        const_cast<Job&> (j).finishedAt = juce::Time::getCurrentTime();
        notifyFinished (j);
        return true;
    }
    if (status == "error")
    {
        const_cast<Job&> (j).result.ok    = false;
        const_cast<Job&> (j).result.error = v.getProperty ("error", "Unknown error").toString();
        const_cast<Job&> (j).done         = true;
        const_cast<Job&> (j).finishedAt   = juce::Time::getCurrentTime();
        notifyFinished (j);
        return true;
    }
    if (status == "running" || status == "queued")
    {
        const double p = v.getProperty ("progress", 0.0);
        const_cast<Job&> (j).progress = p;
        notifyProgress (j);
        return true;
    }
    return true;
}

std::shared_ptr<Job> BackendClient::getJob (const juce::String& id) const
{
    const juce::CriticalSection::ScopedLockType l (jobsMutex);
    auto it = jobs.find (id);
    if (it != jobs.end()) return it->second;
    return nullptr;
}

void BackendClient::modelStateChanged (const ModelState&)
{
    // If active model becomes ready while busy, no-op
    // If active model was removed, abort
    std::shared_ptr<Job> aj;
    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        aj = activeJob;
    }
    if (aj && !modelManager.isModelReady (aj->request.modelId))
    {
        // model missing — cancel
        cancel (aj->id);
    }
}

void BackendClient::setState (BackendState s, const juce::String& err)
{
    state.set (s);
    lastError = err;
    notifyState();
}

void BackendClient::notifyState()
{
    auto s = state.get();
    juce::MessageManager::callAsync ([this, s] {
        listeners.call ([s] (Listener& l) { l.backendStateChanged (s); });
    });
}

void BackendClient::notifyProgress (const Job& j)
{
    std::shared_ptr<Job> copy;
    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        auto it = jobs.find (j.id);
        if (it != jobs.end()) copy = it->second;
    }
    if (!copy) return;
    juce::MessageManager::callAsync ([this, copy] {
        listeners.call ([&copy] (Listener& l) { l.jobProgress (*copy); });
    });
}

void BackendClient::notifyFinished (const Job& j)
{
    std::shared_ptr<Job> copy;
    {
        const juce::CriticalSection::ScopedLockType l (jobsMutex);
        auto it = jobs.find (j.id);
        if (it != jobs.end()) copy = it->second;
    }
    if (!copy) return;
    juce::MessageManager::callAsync ([this, copy] {
        listeners.call ([&copy] (Listener& l) { l.jobFinished (*copy); });
    });
}

// ─── Minimal HTTP client (no external deps) ───────────────────────────────

namespace {

// Single HTTP request/response using libcurl-style raw sockets would be huge;
// use JUCE's URL::createInputStream with proper timeouts.
juce::var parseJsonOrEmpty (const juce::String& text)
{
    auto v = juce::JSON::parse (text);
    if (v.isVoid()) return {};
    return v;
}

} // namespace

juce::var BackendClient::httpGetJson (const juce::String& path)
{
    juce::URL url (juce::String ("http://") + kBackendHost + ":" + juce::String (kBackendPort) + path);
    std::unique_ptr<juce::InputStream> in (url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (2000)
            .withNumRedirectsToFollow (3)));
    if (!in) return {};
    return parseJsonOrEmpty (in->readEntireStreamAsString());
}

bool BackendClient::httpPostJson (const juce::String& path, const juce::var& payload, juce::var* responseOut)
{
    juce::URL url (juce::String ("http://") + kBackendHost + ":" + juce::String (kBackendPort) + path);
    auto body = juce::JSON::toString (payload);
    auto extra = juce::StringArray();
    extra.add ("Content-Type: application/json");
    extra.add ("Accept: application/json");

    std::unique_ptr<juce::InputStream> in (url.withPOSTData (body).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
            .withConnectionTimeoutMs (2000)
            .withExtraHeaders (extra.joinIntoString ("\n"))));
    if (!in)
    {
        if (responseOut) *responseOut = juce::var();
        return false;
    }
    auto text = in->readEntireStreamAsString();
    if (responseOut) *responseOut = parseJsonOrEmpty (text);
    return true;
}

bool BackendClient::httpPostFile (const juce::String& path, const juce::var& payload, const juce::File& bodyFile,
                                  const juce::String& contentType, juce::var* responseOut)
{
    juce::URL url (juce::String ("http://") + kBackendHost + ":" + juce::String (kBackendPort) + path);
    auto body = juce::JSON::toString (payload);
    auto extra = juce::StringArray();
    extra.add ("Content-Type: " + contentType);
    extra.add ("Accept: application/json");

    juce::ignoreUnused (bodyFile);
    std::unique_ptr<juce::InputStream> in (url.withPOSTData (body).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
            .withConnectionTimeoutMs (2000)
            .withExtraHeaders (extra.joinIntoString ("\n"))));
    if (!in) { if (responseOut) *responseOut = juce::var(); return false; }
    if (responseOut) *responseOut = parseJsonOrEmpty (in->readEntireStreamAsString());
    return true;
}

} // namespace dawalka
