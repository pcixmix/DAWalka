#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <functional>
#include <optional>
#include <variant>

namespace dawalka {

// Place a file on the macOS pasteboard as a file-URL (not plain text),
// so apps like Logic Pro can paste it as an audio region.  Implemented
// in ClipboardHelper.mm (Objective-C++).
void copyFileToPasteboard (const juce::String& path);

// Bring a running macOS app to the front by its bundle id
// (e.g. "com.apple.logic10").  Returns true if found and activated.
bool activateAppByBundleId (const juce::String& bundleId);

// ─── Plugin-wide constants ─────────────────────────────────────────────────

inline constexpr const char* kPluginName           = "DAWalka";
inline constexpr const char* kPluginDisplayName   = "DAWalka - AI Audio Generator";
inline constexpr const char* kPluginVersion        = "1.0.0";

inline constexpr int  kDefaultSampleRate         = 44100;
inline constexpr int  kDefaultBlockSize          = 512;
inline constexpr int  kMinDurationSeconds        = 1;
inline constexpr int  kMaxDurationSeconds        = 180;
inline constexpr int  kDefaultDurationSeconds    = 8;

// Defaults for persisted UI settings — used the very first time the
// user opens the plugin (when there's no saved value yet).
inline constexpr const char* kDefaultPrompt         = "dark cinematic cello ostinato";
inline constexpr int  kDefaultKeyRoot               = 1;       // C
inline constexpr int  kDefaultKeyMode               = 1;       // Major
inline constexpr bool kDefaultUseProjectBpm         = true;
inline constexpr int  kDefaultBpm                   = 120;
inline constexpr bool kDefaultUseCustom             = false;
inline constexpr int  kDefaultDurationUnit          = 1;       // 0 = seconds, 1 = bars
inline constexpr int  kDefaultDurationBars          = 4;       // 4 bars @ 120bpm = 8 sec

// Default T2A folder.  This is the SHARED pool of audio files
// for both T2A generation (where the backend writes new WAVs) and
// A2A input (where the user picks existing audio to transform).
// A workflow the new layout enables: generate something in T2A,
// switch to A2A, pick that T2A file as the input, and the
// transformed file lands in the A2A folder.
//
// Lives under the user's Documents folder so the user can find
// the files in Finder without digging through ~/Library; can be
// changed at runtime from the history panel.
inline juce::File defaultT2aDir()
{
    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return docs.getChildFile ("DAWalka").getChildFile ("T2A");
}

// Legacy alias — used to be the T2A output folder, now points at
// the shared T2A folder.  Kept for callers that still use the old
// name; new code should prefer defaultT2aDir().
inline juce::File defaultOutputDir() { return defaultT2aDir(); }

// Audio file extensions accepted by the A2A INPUT panel.
//
// The C++ side uses juce::AudioFormatManager::registerBasicFormats()
// (WAV / AIFF / FLAC / OGG) to *decode* any picked file, and the
// python side uses soundfile (libsndfile) to read it for inference —
// both handle the same set.  MP3 / AAC are not in this list because
// they'd require an extra codec to be linked into the C++ binary; the
// user can convert externally (ffmpeg) if needed.
inline const juce::StringArray& audioFileExtensions()
{
    static const juce::StringArray exts {
        "wav", "wave",
        "aif", "aiff", "aifc",
        "flac",
        "ogg", "oga",
    };
    return exts;
}

inline bool isAudioFileExtension (const juce::String& ext)
{
    auto lower = ext.toLowerCase().trimCharactersAtStart (".");
    for (const auto& known : audioFileExtensions())
        if (lower == known) return true;
    return false;
}

inline bool isAudioFile (const juce::File& f)
{
    return isAudioFileExtension (f.getFileExtension());
}

// Default A2A folder — this is where TRANSFORMED audio lands in
// A2A mode (the OUTPUT of the transform).  Inputs come from the
// T2A folder (shared with the T2A workflow) — there is no
// separate A2A input folder.
inline juce::File defaultA2aDir()
{
    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return docs.getChildFile ("DAWalka").getChildFile ("A2A");
}

// Legacy alias — used to be ~/Documents/DAWalka/A2A/output, now
// points at the unified ~/Documents/DAWalka/A2A.  Kept for
// callers that still use the old name; new code should prefer
// defaultA2aDir().
inline juce::File defaultA2aOutputDir() { return defaultA2aDir(); }

inline constexpr int  kUiWidth                   = 960;
inline constexpr int  kUiHeight                  = 720;
inline constexpr int  kUiMinWidth                = 860;
inline constexpr int  kUiMinHeight               = 760;
inline constexpr int  kOuterPad                  = 16;
inline constexpr int  kHeaderHeight              = 60;
inline constexpr int  kModeTabHeight             = 36;

inline constexpr const char* kModelsDirectory     = "DAWalka/models";
inline constexpr const char* kHistoryFileName     = "history.json";
inline constexpr const char* kSettingsFileName    = "settings.json";
inline constexpr const char* kBackendLogFileName  = "backend.log";
inline constexpr const char* kBackendPidFileName  = "backend.pid";

// IPC — plugin ↔ Python backend
inline constexpr const char* kBackendHost         = "127.0.0.1";
inline constexpr int         kBackendPort         = 47823;   // "DWLK" on phone keypad
inline constexpr int         kBackendStartupMs    = 45000;
inline constexpr int         kBackendPollMs       = 250;
inline constexpr int         kBackendRequestMs    = 60000;

// Apple Silicon detection
enum class AppleSilicon
{
    Unknown,
    Intel,
    M1, M1Pro, M1Max, M1Ultra,
    M2, M2Pro, M2Max, M2Ultra,
    M3, M3Pro, M3Max,
    M4, M4Pro, M4Max
};

// Musical key — 12 semitones × 2 modes
enum class ScaleMode { Major, Minor };

struct MusicalKey
{
    int  root      = 0;            // 0 = C, 1 = C#, … 11 = B
    ScaleMode mode = ScaleMode::Major;

    juce::String toString() const
    {
        static const char* names[12] = {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        return juce::String(names[root]) + " " +
               (mode == ScaleMode::Major ? "Major" : "Minor");
    }

    static std::optional<MusicalKey> parse(const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        static const char* names[12] = {
            "c", "c#", "d", "d#", "e", "f",
            "f#", "g", "g#", "a", "a#", "b"
        };
        for (int i = 0; i < 12; ++i)
        {
            if (t.startsWith(names[i]))
            {
                ScaleMode m = ScaleMode::Major;
                if (t.contains("minor") || t.contains("min") || t.endsWith("m"))
                    m = ScaleMode::Minor;
                return MusicalKey{ i, m };
            }
        }
        return std::nullopt;
    }
};

struct GenerationRequest
{
    juce::String  prompt;
    MusicalKey    key           { 0, ScaleMode::Major };
    int           bpm           = 120;
    double        durationSec   = 8.0;
    int           sampleRate    = 44100;   // 44100 or 48000 — written into the WAV
    juce::String  modelId;
    int           seed          = -1;
    int           variations    = 1;
    bool          seamlessLoop  = true;
    // Whether to include the KEY / BPM tags in the prompt sent to the
    // model.  Both default true to preserve historical behaviour.
    // Turn them off when using a music model to generate sound effects
    // or untextured sounds (e.g. sa3-medium for foley) — the tags
    // push the model toward tonal output the user doesn't want.
    // The bpm / key values themselves are still sent (so duration /
    // host-bpm sync still work) — only the prompt-side annotation is
    // suppressed.
    bool          includeKey    = true;
    bool          includeBpm    = true;

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("prompt",         prompt);
        obj->setProperty("key_root",       key.root);
        obj->setProperty("key_mode",       key.mode == ScaleMode::Major ? "major" : "minor");
        obj->setProperty("bpm",            bpm);
        obj->setProperty("duration",       durationSec);
        obj->setProperty("sample_rate",    sampleRate);
        obj->setProperty("model",          modelId);
        obj->setProperty("seed",           seed);
        obj->setProperty("variations",     variations);
        obj->setProperty("seamless_loop",  seamlessLoop);
        obj->setProperty("include_key",    includeKey);
        obj->setProperty("include_bpm",    includeBpm);
        return juce::var(obj);
    }
};

struct GenerationResult
{
    bool                  ok              = false;
    juce::String          error;
    juce::File            audioFile;
    double                durationSec     = 0.0;
    int                   sampleRate      = 44100;
    int                   numChannels     = 2;
    juce::String          modelId;
    int                   seed            = 0;
    double                generationTimeMs = 0.0;
};

// Top-level UI mode.  T2A is the text-to-audio flow we shipped first;
// A2A is the audio-to-audio editor (init audio + prompt → regenerated
// audio of the same length).
enum class Mode { T2A, A2A };

// A2A request payload — what the C++ side sends to the python /a2a
// endpoint.  The python side reads the input file from disk directly
// (it runs as an unsandboxed helper process, so any user-readable path
// is fine — no upload needed).
struct A2aRequest
{
    juce::String  prompt;
    juce::File    initAudio;          // file the user picked from the input browser
    float         initNoiseLevel = 0.7f;  // σmax — 0.0=preserve, 1.0=full regen
    double        durationSec   = 0.0;    // length of initAudio, measured in C++
    juce::String  modelId;
    int           sampleRate    = 44100;
    int           seed          = -1;
    juce::File    outputDir;           // A2A output folder (separate from T2A)
    juce::String  suggestedFilename;    // e.g. "loop_bass__a2a_15-30-45.wav" (python uses if set)

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("prompt",           prompt);
        obj->setProperty ("init_audio",       initAudio.getFullPathName());
        obj->setProperty ("init_noise_level", initNoiseLevel);
        obj->setProperty ("duration",         durationSec);
        obj->setProperty ("model",            modelId);
        obj->setProperty ("sample_rate",      sampleRate);
        obj->setProperty ("seed",             seed);
        if (outputDir != juce::File())
            obj->setProperty ("output_dir",   outputDir.getFullPathName());
        if (suggestedFilename.isNotEmpty())
            obj->setProperty ("output_filename", suggestedFilename);
        return juce::var (obj);
    }
};

// History record
struct HistoryEntry
{
    juce::Time            timestamp;
    juce::String          modelId;
    juce::String          prompt;
    int                   bpm          = 120;
    MusicalKey            key          { 0, ScaleMode::Major };
    double                durationSec  = 0.0;
    int                   sampleRate   = 44100;
    juce::File            audioFile;
    int                   seed         = 0;
    juce::String          id;          // uuid
    // A2A metadata (empty/zero for T2A entries; populated for A2A).
    juce::String          kind         = "t2a";   // "t2a" | "a2a"
    juce::String          sourceFile;            // absolute path of A2A input, empty for T2A
    float                 initNoiseLevel = 0.0f; // A2A σmax; 0.0 for T2A

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id",         id);
        obj->setProperty("timestamp",  timestamp.toISO8601(false));
        obj->setProperty("model",      modelId);
        obj->setProperty("prompt",     prompt);
        obj->setProperty("bpm",        bpm);
        obj->setProperty("key_root",   key.root);
        obj->setProperty("key_mode",   key.mode == ScaleMode::Major ? "major" : "minor");
        obj->setProperty("duration",   durationSec);
        obj->setProperty("sample_rate", sampleRate);
        obj->setProperty("audio",      audioFile.getFullPathName());
        obj->setProperty("seed",       seed);
        if (kind != "t2a")                obj->setProperty ("kind",            kind);
        if (sourceFile.isNotEmpty())      obj->setProperty ("source_file",     sourceFile);
        if (initNoiseLevel > 0.0f)        obj->setProperty ("init_noise_level", initNoiseLevel);
        return juce::var(obj);
    }

    static HistoryEntry fromVar(const juce::var& v)
    {
        HistoryEntry e;
        e.id        = v["id"].toString();
        e.timestamp = juce::Time::fromISO8601(v["timestamp"].toString());
        e.modelId   = v["model"].toString();
        e.prompt    = v["prompt"].toString();
        e.bpm       = v["bpm"];
        e.key.root  = v["key_root"];
        e.key.mode  = v["key_mode"].toString() == "minor" ? ScaleMode::Minor : ScaleMode::Major;
        e.durationSec = v["duration"];
        e.sampleRate   = v["sample_rate"];   // 44100 by default if missing in old entries
        e.audioFile = juce::File(v["audio"].toString());
        e.seed      = v["seed"];
        e.kind            = v.hasProperty ("kind")             ? v["kind"].toString()             : juce::String ("t2a");
        e.sourceFile      = v.hasProperty ("source_file")      ? v["source_file"].toString()      : juce::String();
        e.initNoiseLevel  = v.hasProperty ("init_noise_level") ? (float) v["init_noise_level"]     : 0.0f;
        return e;
    }
};

// Small RAII helper for thread-safe background-to-UI messaging
template <typename T>
class ThreadSafeValue
{
public:
    void set (T v)                { const juce::SpinLock::ScopedLockType l (lock); value = std::move (v); }
    T    get() const              { const juce::SpinLock::ScopedLockType l (lock); return value; }

private:
    mutable juce::SpinLock lock;
    T value;
};

// Convert seconds → bars/beats (4/4)
inline std::pair<int, int> secondsToBarsBeats (double seconds, double bpm)
{
    if (bpm <= 0.0) return { 0, 0 };
    const double beats = (seconds * bpm) / 60.0;
    return { static_cast<int> (beats) / 4,
             static_cast<int> (beats) % 4 };
}

// Convert bars → seconds (4/4).  Returns 0 if bpm is non-positive.
inline double barsToSeconds (int bars, double bpm)
{
    if (bpm <= 0.0) return 0.0;
    return bars * 4.0 * 60.0 / bpm;
}

// Make uuid
juce::String makeUuid();

} // namespace dawalka
