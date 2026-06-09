#pragma once

#include "Common.h"

namespace dawalka {

// One entry in the catalogue — metadata only, lives in JSON shipped with the
// plugin. The actual weights live in ~/Library/Application Support/DAWalka/models/.
struct ModelDescriptor
{
    juce::String id;              // stable identifier (used in IPC)
    juce::String displayName;     // user-visible name
    juce::String description;
    juce::String repoOwner;       // HuggingFace / Stability repo
    juce::String repoName;
    juce::String revision;        // git revision / branch
    juce::StringArray fileList;   // files to download
    int64_t      totalBytes = 0;  // approximate size, used for UI
    juce::String sha256;          // optional verification hash
    juce::String minMemoryGB;     // e.g. "8"
    juce::String recommendedFor;  // e.g. "M1/M2 (8GB)"
};

enum class ModelStatus
{
    NotInstalled,
    Downloading,
    Verifying,
    Ready,
    Loading,
    Error
};

juce::String modelStatusToString (ModelStatus s);
ModelStatus  modelStatusFromString (const juce::String& s);

struct ModelState
{
    ModelDescriptor desc;
    ModelStatus     status           = ModelStatus::NotInstalled;
    double          progress         = 0.0;     // 0…1
    int64_t         bytesDownloaded  = 0;
    juce::String    errorMessage;
    juce::Time      lastUsedAt;                 // for LRU unloading
};

// Thread-safe repository. The list of installed models lives in
// ~/Library/Application Support/DAWalka/models/installed.json. The catalogue
// itself is built-in (see ModelManager::getCatalogue()).
class ModelManager : public juce::Thread,
                     public juce::AsyncUpdater
{
public:
    ModelManager();
    ~ModelManager() override;

    // Catalogue of all known models
    static juce::Array<ModelDescriptor> getCatalogue();
    static std::optional<ModelDescriptor> findById (const juce::String& id);

    // Get a snapshot of the current per-model state
    juce::Array<ModelState> getStates() const;

    juce::File getModelsDirectory() const;
    juce::File getModelDirectory (const juce::String& id) const;

    bool isModelReady (const juce::String& id) const;
    juce::File getModelPath (const juce::String& id) const;

    // Begin downloading a model — runs on a background thread
    void downloadModel (const juce::String& id);

    // Drop a model from disk
    void deleteModel (const juce::String& id);

    // Mark a model as "in use" — updates LRU timestamp
    void touchModel (const juce::String& id);

    // Listeners
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void modelStateChanged (const ModelState& state) = 0;
        virtual void modelListChanged() = 0;
    };
    void addListener    (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // juce::Thread
    void run() override;

    // juce::AsyncUpdater
    void handleAsyncUpdate() override;

private:
    void loadInstalledFromDisk();
    void saveInstalledToDisk();
    void notifyState (const ModelState& s);
    void notifyList();
    void refreshStateFromDisk();

    juce::File  installedFile;
    mutable juce::CriticalSection mutex;
    std::unordered_map<juce::String, ModelState> states;
    juce::ListenerList<Listener> listeners;

    juce::String currentJobId;
    bool         cancelRequested = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelManager)
};

} // namespace dawalka
