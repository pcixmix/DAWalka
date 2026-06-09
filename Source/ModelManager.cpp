#include "ModelManager.h"

namespace dawalka {

juce::String modelStatusToString (ModelStatus s)
{
    switch (s)
    {
        case ModelStatus::NotInstalled: return "NOT_INSTALLED";
        case ModelStatus::Downloading:  return "DOWNLOADING";
        case ModelStatus::Verifying:    return "VERIFYING";
        case ModelStatus::Ready:        return "READY";
        case ModelStatus::Loading:      return "LOADING";
        case ModelStatus::Error:        return "ERROR";
    }
    return "UNKNOWN";
}

ModelStatus modelStatusFromString (const juce::String& s)
{
    if (s == "DOWNLOADING") return ModelStatus::Downloading;
    if (s == "VERIFYING")   return ModelStatus::Verifying;
    if (s == "READY")       return ModelStatus::Ready;
    if (s == "LOADING")     return ModelStatus::Loading;
    if (s == "ERROR")       return ModelStatus::Error;
    return ModelStatus::NotInstalled;
}

juce::Array<ModelDescriptor> ModelManager::getCatalogue()
{
    juce::Array<ModelDescriptor> cat;

    // ── Stable Audio 3 Small Music (MLX, non-gated) ─────────────────────
    // Weights: stabilityai/stable-audio-3-optimized/MLX/ — pre-converted .npz
    // files (no PyTorch required at runtime).
    {
        ModelDescriptor m;
        m.id            = "sa3-sm-music";
        m.displayName   = "Stable Audio 3 Small Music";
        m.description   = "Compact music / loop generator. ~1.9 GB. ~10x realtime on M1 8 GB.";
        m.repoOwner     = "stabilityai";
        m.repoName      = "stable-audio-3-optimized";
        m.revision      = "main";
        m.totalBytes    = 1'919'000'000LL;
        m.minMemoryGB   = "8";
        m.recommendedFor = "M1 / M2 (8 GB)";
        m.fileList = {
            "dit_sm-music_f16.npz",
            "same_s_decoder_f32.npz",
            "same_s_encoder_f32.npz",
            "t5gemma_f16.npz"
        };
        cat.add (m);
    }

    // ── Stable Audio 3 Small SFX (MLX, non-gated) ───────────────────────
    {
        ModelDescriptor m;
        m.id            = "sa3-sm-sfx";
        m.displayName   = "Stable Audio 3 Small SFX";
        m.description   = "Sound effects & foley generator. ~1.9 GB. ~10x realtime on M1 8 GB.";
        m.repoOwner     = "stabilityai";
        m.repoName      = "stable-audio-3-optimized";
        m.revision      = "main";
        m.totalBytes    = 1'919'000'000LL;
        m.minMemoryGB   = "8";
        m.recommendedFor = "M1 / M2 (8 GB)";
        m.fileList = {
            "dit_sm-sfx_f16.npz",
            "same_s_decoder_f32.npz",
            "same_s_encoder_f32.npz",
            "t5gemma_f16.npz"
        };
        cat.add (m);
    }

    // ── Stable Audio 3 Medium (MLX, non-gated) ──────────────────────────
    {
        ModelDescriptor m;
        m.id            = "sa3-medium";
        m.displayName   = "Stable Audio 3 Medium";
        m.description   = "Higher-fidelity music + SFX. ~6.9 GB. ~2x realtime on M1 8 GB.";
        m.repoOwner     = "stabilityai";
        m.repoName      = "stable-audio-3-optimized";
        m.revision      = "main";
        m.totalBytes    = 6'882'000'000LL;
        m.minMemoryGB   = "16";
        m.recommendedFor = "M1 Pro / M2 Pro (16 GB)";
        m.fileList = {
            "dit_medium_f16.npz",
            "same_l_decoder_f32.npz",
            "same_l_encoder_f32.npz",
            "t5gemma_f16.npz"
        };
        cat.add (m);
    }

    return cat;
}

std::optional<ModelDescriptor> ModelManager::findById (const juce::String& id)
{
    for (auto& m : getCatalogue())
        if (m.id == id) return m;
    return std::nullopt;
}

ModelManager::ModelManager()
    : juce::Thread ("DAWalka-ModelManager")
{
    installedFile = getModelsDirectory().getChildFile ("installed.json");
    loadInstalledFromDisk();
    refreshStateFromDisk();
    // Run at low priority so we never starve the audio thread or delay
    // host UI work.
    startThread (juce::Thread::Priority::low);
}

ModelManager::~ModelManager()
{
    cancelRequested = true;
    stopThread (5000);
}

juce::File ModelManager::getModelsDirectory() const
{
    // IMPORTANT: do NOT use juce::File::userApplicationDataDirectory here.
    // In a sandboxed AU (Logic Pro 11's AUHostingServiceXPC_arrow) JUCE's
    // userApplicationDataDirectory resolves to ~/Library/ (the container root),
    // NOT to ~/Library/Application Support/ as one would expect on macOS.
    // This caused the plugin to look for models in
    // ~/Library/DAWalka/models/ while the installer puts them in
    // ~/Library/Application Support/DAWalka/models/.
    // Use the explicit, sandbox-stable path:
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Application Support")
                   .getChildFile (kModelsDirectory);
    if (!dir.exists()) dir.createDirectory();
    return dir;
}

juce::File ModelManager::getModelDirectory (const juce::String& id) const
{
    // All sa3_mlx models share a flat layout — weights live directly in
    // ~/Library/Application Support/DAWalka/models/<filename>.npz.
    // (The OLD per-model subdirectory layout was for stable-audio-tools,
    // which is no longer used.)
    return getModelsDirectory();
}

juce::File ModelManager::getModelPath (const juce::String& id) const
{
    auto dir = getModelDirectory (id);
    auto desc = findById (id);
    if (!desc) return {};
    auto primary = dir.getChildFile (desc->fileList[0]);
    return primary.existsAsFile() ? primary : juce::File();
}

bool ModelManager::isModelReady (const juce::String& id) const
{
    const juce::CriticalSection::ScopedLockType l (mutex);
    auto it = states.find (id);
    return it != states.end() && it->second.status == ModelStatus::Ready;
}

juce::Array<ModelState> ModelManager::getStates() const
{
    juce::Array<ModelState> out;
    const juce::CriticalSection::ScopedLockType l (mutex);
    for (auto& kv : states) out.add (kv.second);
    return out;
}

void ModelManager::touchModel (const juce::String& id)
{
    const juce::CriticalSection::ScopedLockType l (mutex);
    auto it = states.find (id);
    if (it != states.end())
    {
        it->second.lastUsedAt = juce::Time::getCurrentTime();
        saveInstalledToDisk();
    }
}

void ModelManager::deleteModel (const juce::String& id)
{
    auto dir = getModelDirectory (id);
    if (dir.exists()) dir.deleteRecursively();

    {
        const juce::CriticalSection::ScopedLockType l (mutex);
        auto it = states.find (id);
        if (it != states.end())
        {
            it->second.status           = ModelStatus::NotInstalled;
            it->second.progress         = 0.0;
            it->second.bytesDownloaded  = 0;
            it->second.errorMessage     = {};
        }
        saveInstalledToDisk();
    }
    notifyList();
    for (auto& s : getStates()) notifyState (s);
}

namespace {

// Synchronous HTTP download with progress callback. Returns true on success.
// On failure, deletes any partial output file.
bool downloadUrlToFile (const juce::URL& url,
                        const juce::File& outFile,
                        std::function<void (juce::int64, juce::int64)> onProgress,
                        std::function<bool()> shouldContinue)
{
    if (outFile.existsAsFile()) outFile.deleteFile();
    outFile.getParentDirectory().createDirectory();

    std::unique_ptr<juce::InputStream> in (url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (30000)
            .withNumRedirectsToFollow (5)));

    if (!in) return false;

    std::unique_ptr<juce::OutputStream> out (outFile.createOutputStream());
    if (!out) return false;

    const juce::int64 totalLen = in->getTotalLength();
    juce::MemoryBlock chunk (64 * 1024);
    juce::int64 written = 0;

    while (!in->isExhausted())
    {
        if (shouldContinue && !shouldContinue()) { out.reset(); outFile.deleteFile(); return false; }
        const int got = in->read (chunk.getData(), (int) chunk.getSize());
        if (got <= 0) break;
        out->write (chunk.getData(), (size_t) got);
        written += got;
        if (onProgress) onProgress (written, totalLen);
    }
    out->flush();
    return written > 0;
}

} // namespace

void ModelManager::downloadModel (const juce::String& id)
{
    cancelRequested = false;
    currentJobId    = id;
    notify(); // wake up the worker thread
}

void ModelManager::run()
{
    while (!threadShouldExit())
    {
        if (currentJobId.isNotEmpty() && !cancelRequested)
        {
            const auto id = currentJobId;
            currentJobId = {};

            auto desc = findById (id);
            if (!desc)
            {
                ModelState s;
                s.status = ModelStatus::Error;
                s.errorMessage = "Unknown model id";
                {
                    const juce::CriticalSection::ScopedLockType l (mutex);
                    states[id] = s;
                }
                notifyState (s);
                continue;
            }

            const auto targetDir = getModelDirectory (id);
            targetDir.createDirectory();

            // Set status
            {
                const juce::CriticalSection::ScopedLockType l (mutex);
                if (states.find (id) == states.end())
                {
                    ModelState ms;
                    ms.desc = *desc;
                    states[id] = ms;
                }
                auto& s = states[id];
                s.status           = ModelStatus::Downloading;
                s.progress         = 0.0;
                s.bytesDownloaded  = 0;
                s.errorMessage     = {};
                notifyState (s);
            }

            bool allOk = true;
            int64_t totalBytes = 0;
            int  fileIdx = 0;

            for (const auto& fileName : desc->fileList)
            {
                if (threadShouldExit() || cancelRequested) { allOk = false; break; }

                const auto outFile = targetDir.getChildFile (fileName);
                if (outFile.existsAsFile() && outFile.getSize() > 0)
                {
                    // already downloaded
                    fileIdx++;
                    {
                        const juce::CriticalSection::ScopedLockType l (mutex);
                        auto& s = states[id];
                        s.progress = static_cast<double> (fileIdx) / desc->fileList.size();
                        s.bytesDownloaded += outFile.getSize();
                        totalBytes += outFile.getSize();
                        notifyState (s);
                    }
                    continue;
                }

                // ── Try HuggingFace direct URL ────────────────────────────
                juce::URL url (juce::String ("https://huggingface.co/")
                               + desc->repoOwner + "/"
                               + desc->repoName + "/resolve/"
                               + desc->revision + "/"
                               + fileName);

                const auto onProgress = [&] (juce::int64 bytesSoFar, juce::int64)
                {
                    const juce::CriticalSection::ScopedLockType l (mutex);
                    auto it = states.find (id);
                    if (it == states.end()) return;
                    auto& s = it->second;
                    s.bytesDownloaded = totalBytes + bytesSoFar;
                    s.progress       = (static_cast<double> (fileIdx) +
                                         (bytesSoFar > 0 ? static_cast<double> (bytesSoFar) / std::max<juce::int64> (1, desc->totalBytes / desc->fileList.size()) : 0))
                                                                / desc->fileList.size();
                    notifyState (s);
                };
                const auto shouldCont = [&] { return !cancelRequested && !threadShouldExit(); };

                bool ok = downloadUrlToFile (url, outFile, onProgress, shouldCont);

                if (cancelRequested) { allOk = false; outFile.deleteFile(); break; }
                if (!ok)
                {
                    // fall back to LFS-resolve redirect
                    juce::URL lfsUrl (juce::String ("https://huggingface.co/")
                                      + desc->repoOwner + "/"
                                      + desc->repoName + "/resolve/"
                                      + desc->revision + "/"
                                      + fileName + "?download=true");
                    const auto onProgress2 = [&] (juce::int64 bytesSoFar, juce::int64)
                    {
                        const juce::CriticalSection::ScopedLockType l (mutex);
                        auto it = states.find (id);
                        if (it == states.end()) return;
                        auto& s = it->second;
                        s.bytesDownloaded = totalBytes + bytesSoFar;
                        s.progress       = (static_cast<double> (fileIdx) + 0.5) / desc->fileList.size();
                        notifyState (s);
                    };
                    ok = downloadUrlToFile (lfsUrl, outFile, onProgress2, shouldCont);
                }

                if (!ok)
                {
                    allOk = false;
                    {
                        const juce::CriticalSection::ScopedLockType l (mutex);
                        auto& s = states[id];
                        s.status       = ModelStatus::Error;
                        s.errorMessage = "Failed to download " + fileName;
                        notifyState (s);
                    }
                    break;
                }
                else
                {
                    totalBytes += outFile.getSize();
                    fileIdx++;
                }
            }

            if (allOk)
            {
                // ── Verify (basic — files exist & size > 0; sha256 if set) ──
                {
                    const juce::CriticalSection::ScopedLockType l (mutex);
                    auto& s = states[id];
                    s.status   = ModelStatus::Verifying;
                    s.progress = 0.95;
                    notifyState (s);
                }

                bool verified = true;
                for (const auto& f : desc->fileList)
                {
                    auto ff = targetDir.getChildFile (f);
                    if (!ff.existsAsFile() || ff.getSize() < 16)
                    { verified = false; break; }
                }
                if (verified && desc->sha256.isNotEmpty())
                {
                    auto primary = targetDir.getChildFile (desc->fileList[0]);
                    auto actual = juce::SHA256 (primary).toHexString();
                    if (actual.toLowerCase() != desc->sha256.toLowerCase())
                        verified = false;
                }

                {
                    const juce::CriticalSection::ScopedLockType l (mutex);
                    auto& s = states[id];
                    s.status       = verified ? ModelStatus::Ready : ModelStatus::Error;
                    s.progress     = 1.0;
                    s.errorMessage = verified ? juce::String() : juce::String ("Integrity check failed");
                    s.lastUsedAt   = juce::Time::getCurrentTime();
                    saveInstalledToDisk();
                    notifyState (s);
                }
            }
        }

        wait (250);
    }
}

void ModelManager::refreshStateFromDisk()
{
    const juce::CriticalSection::ScopedLockType l (mutex);
    for (auto& desc : getCatalogue())
    {
        auto it = states.find (desc.id);
        if (it == states.end())
        {
            ModelState ms;
            ms.desc = desc;
            states[desc.id] = ms;
        }
        else
            it->second.desc = desc;

        auto dir = getModelDirectory (desc.id);
        bool anyMissing = false;
        for (const auto& f : desc.fileList)
            if (!dir.getChildFile (f).existsAsFile()) { anyMissing = true; break; }

        auto& s = states[desc.id];
        if (s.status == ModelStatus::Downloading || s.status == ModelStatus::Verifying)
            continue;            // don't overwrite an active job
        s.status = anyMissing ? ModelStatus::NotInstalled : ModelStatus::Ready;
    }
    saveInstalledToDisk();
}

void ModelManager::loadInstalledFromDisk()
{
    if (!installedFile.existsAsFile()) return;

    auto json = juce::JSON::parse (installedFile);
    if (!json.isObject()) return;

    auto* obj = json.getDynamicObject();
    if (!obj) return;

    for (auto& desc : getCatalogue())
    {
        ModelState s;
        s.desc = desc;
        auto entry = obj->getProperty (desc.id);
        if (entry.isObject())
        {
            s.status          = modelStatusFromString (entry.getProperty ("status", "NOT_INSTALLED").toString());
            s.progress        = entry.getProperty ("progress", 0.0);
            s.bytesDownloaded = entry.getProperty ("bytes", 0);
            s.errorMessage    = entry.getProperty ("error", "");
            s.lastUsedAt      = juce::Time::fromISO8601 (entry.getProperty ("lastUsed", "").toString());
        }
        else
        {
            s.status = ModelStatus::NotInstalled;
        }
        states[desc.id] = s;
    }
}

void ModelManager::saveInstalledToDisk()
{
    auto* root = new juce::DynamicObject();
    for (auto& kv : states)
    {
        auto& s = kv.second;
        auto* e = new juce::DynamicObject();
        e->setProperty ("status",   modelStatusToString (s.status));
        e->setProperty ("progress", s.progress);
        e->setProperty ("bytes",    static_cast<juce::int64> (s.bytesDownloaded));
        e->setProperty ("error",    s.errorMessage);
        e->setProperty ("lastUsed", s.lastUsedAt.toISO8601 (false));
        root->setProperty (kv.first, juce::var (e));
    }
    installedFile.replaceWithText (juce::JSON::toString (juce::var (root), true));
}

void ModelManager::notifyState (const ModelState& s)
{
    juce::MessageManager::callAsync ([this, s] {
        listeners.call ([&] (Listener& l) { l.modelStateChanged (s); });
    });
}

void ModelManager::notifyList()
{
    juce::MessageManager::callAsync ([this] {
        listeners.call ([] (Listener& l) { l.modelListChanged(); });
    });
}

void ModelManager::handleAsyncUpdate() {}

} // namespace dawalka
