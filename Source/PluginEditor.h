#pragma once

#include "Common.h"
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "ModelSelectorComponent.h"
#include "HistoryComponent.h"
#include "FileListComponent.h"
#include "WaveformComponent.h"
#include "ProgressOverlay.h"
#include "PromptLibrary.h"

namespace dawalka {

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::ChangeListener,
                     public juce::Timer,
                     private BackendClient::Listener,
                     private ModelManager::Listener
{
public:
    explicit PluginEditor (PluginProcessor& p);
    ~PluginEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    // BackendClient::Listener
    void backendStateChanged (BackendState s) override;
    void jobProgress   (const Job& job) override;
    void jobFinished   (const Job& job) override;

    // ModelManager::Listener
    void modelStateChanged (const ModelState& s) override;
    void modelListChanged() override { repaint(); }

private:
    void buildInterface();
    void buildHeader();
    void buildMain();
    void buildFooter();

    enum class DurationUnit { Seconds, Bars };

    // Mode switching (T2A / A2A)
    void setMode (Mode newMode);
    void onModeTabClicked (Mode m);

    // T2A history panel
    void buildT2aHistoryPanel();
    void onT2aHistorySelect  (const HistoryEntry& e);
    void onT2aHistoryPlay    (const HistoryEntry& e);
    void onT2aHistoryDelete  (const HistoryEntry& e);
    void onT2aHistoryClear();
    void showT2aHistoryMenu();
    void changeT2aOutputFolder();
    void applyT2aOutputFolderChange (const juce::File& newDir);

    // A2A panels
    void buildA2aPanels();
    void onA2aInputSelect  (const juce::File& f);
    void onA2aInputPlay    (const juce::File& f);
    void onA2aInputDelete  (const juce::File& f);
    void onA2aInputMenu();
    void onA2aInputFilesDropped (const juce::Array<juce::File>& files);
    void onA2aOutputSelect  (const HistoryEntry& e);
    void onA2aOutputPlay    (const HistoryEntry& e);
    void onA2aOutputDelete  (const HistoryEntry& e);
    void onA2aOutputClear();
    void showA2aOutputMenu();
    void changeA2aInputFolder();
    void applyA2aInputFolderChange (const juce::File& newDir);
    void changeA2aOutputFolder();
    void applyA2aOutputFolderChange (const juce::File& newDir);
    juce::File getEffectiveA2aInputDir() const;
    void refreshA2aInputList();
    void updateA2aInputFileLabel();
    void updateInitNoiseValueLabel();
    double measureAudioSeconds (const juce::File& f) const;

    // Actions
    void generateClicked();
    void cancelClicked();
    void playClicked();
    void stopClicked();
    void loopToggled();
    void exportClicked();
    void saveWavClicked();
    void copyToClipboardClicked();
    void updateFromHost();
    void updateGenerateButton();
    void ensureProcessorRenderBuffer();
    // Play the file through the plugin's preview render path, or stop
    // it if it's the file that's currently playing.  Shared by the
    // Play button in T2A history, A2A input browser, and A2A output
    // history so they all behave the same way (one click = play,
    // second click = stop).
    void togglePlayFile (const juce::File& f);
    void showStatus  (const juce::String& msg, int ms = 3000);
    void copyFileToClipboard (const juce::File& f);

    // Duration unit (SEC / BAR) helpers
    void setDurationUnit (DurationUnit u);
    void updateDurationSliderForUnit();
    void updateDurationValueLabel();
    // Effective duration in seconds, given the current state of the
    // duration controls.  Uses BPM from the slider or from the host
    // depending on useProjectBpm.
    double getEffectiveDurationSec() const;

    // Persistence — read controls from the shared PropertiesFile and
    // write them back on every change so the next project-open / next
    // plugin reload restores the user's last session.
    void loadFromSettings();
    void saveToSettings() const;

    PluginProcessor&                  processor;
    DAWalkaLookAndFeel                lnf;

    // Header
    juce::Label                       headerTitle;
    juce::Label                       headerSubtitle;
    juce::Label                       backendStatusLabel;
    juce::Label                       chipLabel;

    // Mode tabs (T2A | A2A) — placed just below the header
    juce::TextButton                  modeTabT2a { "T2A   |   TEXT TO AUDIO" };
    juce::TextButton                  modeTabA2a { "A2A   |   AUDIO TO AUDIO" };

    // Model
    juce::Label                       modelLabel;
    std::unique_ptr<ModelSelectorComponent> modelSelector;

    // Prompt
    juce::Label                       promptLabel;
    juce::TextEditor                  promptEditor;
    // "RANDOM MUSIC" button — picks a random instrumental prompt from
    // PromptLibrary and writes it into promptEditor.  Lives in the
    // top-right of the prompt block, on the same row as the PROMPT
    // label.
    juce::TextButton                  randomMusicPromptButton { "RANDOM MUSIC" };
    void                               onRandomMusicClicked();
    // "RANDOM SFX" button — picks a random sound-effect prompt from
    // PromptLibrary and writes it into promptEditor.  Hidden in A2A
    // mode (A2A only has one "RANDOM MUSIC" button — sfx category
    // could be added later if needed).
    juce::TextButton                  randomSfxPromptButton { "RANDOM SFX" };
    void                               onRandomSfxClicked();

    // Init-noise slider (A2A only — hidden in T2A)
    juce::Label                       initNoiseLabel;
    juce::Slider                      initNoiseSlider;
    juce::Label                       initNoiseValueLabel;
    // Quick-pick noise presets above the slider (A2A only)
    juce::TextButton                  initNoisePresetSubtle   { "Subtle" };   // 0.30
    juce::TextButton                  initNoisePresetBalanced { "Balanced" }; // 0.60
    juce::TextButton                  initNoisePresetCreative { "Creative" }; // 0.90
    void onInitNoisePresetClicked (float value);

    // ── Solo instrument (T2A only) ───────────────────────────────────
    // Sits under the prompt field.  When the toggle is on and an
    // instrument is picked, the plugin appends "solo <instrument>" to
    // the prompt before sending it to the model — Stable Audio 3
    // recognises that pattern and biases output toward a single
    // instrument performance.  Disabled for the FX model (no
    // meaningful "solo" concept in foley / SFX).  Available in BOTH
    // T2A and A2A modes — A2A solo appends "solo <instrument>" to
    // the transformation prompt so the model biases the regenerated
    // audio toward a single instrument.
    juce::ToggleButton                soloToggle       { "SOLO INSTRUMENT" };
    juce::ComboBox                    soloInstrument;
    void onSoloToggleChanged();
    void updateSoloEnabled();  // called when model changes — grays
                               // out the controls for sa3-sm-sfx
    // Appends "solo <instrument>" to `prompt` iff the SOLO toggle is
    // on, an instrument is picked, and we're not on the FX model.
    // Used by both T2A and A2A generation paths.  Pure function
    // (apart from reading soloToggle/soloInstrument state) — safe
    // to call from the message thread.
    juce::String appendSoloTag (const juce::String& prompt) const;

    // ── Prompt tags toggles (T2A only) ───────────────────────────────
    // KEY and BPM tags push the model toward tonal, rhythmic output.
    // When the user is using a music model (e.g. sa3-medium) to
    // generate sound effects, untextured ambiences, or anything where
    // the musical key/BPM cues are misleading, the corresponding tag
    // can be turned off.  This only affects the *prompt text* — the
    // BPM value still drives the duration slider and host-tempo sync,
    // the KEY dropdown still affects the model's tonal output where it
    // does apply.  Both default to ON to preserve the historical
    // behaviour; turning them off is purely opt-in.
    juce::Label                       tagsLabel        { {}, "TAGS" };
    juce::ToggleButton                includeKeyInPrompt { "KEY" };
    juce::ToggleButton                includeBpmInPrompt { "BPM" };
    // Reconciles the enabled state of the KEY and BPM control rows
    // with the corresponding tag toggle.  When a tag is off, the
    // related controls are grayed so the user doesn't think they're
    // setting a value that won't appear in the prompt.  T2A-only
    // (in A2A everything is hidden anyway).
    void updateTagsEnabled();

    // "Selected input file" indicator (A2A only)
    juce::Label                       a2aInputFileLabel { {}, "" };

    // Key
    juce::Label                       keyLabel;
    juce::ComboBox                    keyRoot;
    juce::ComboBox                    keyMode;

    // BPM
    juce::Label                       bpmLabel;
    juce::ToggleButton                useProjectBpm { "Use Project BPM" };
    juce::Slider                      bpmSlider;
    juce::Label                       bpmValueLabel;
    juce::TextButton                  autoBpmButton  { "AUTO FROM PROJECT" };

    // Sample rate
    juce::Label                       sampleRateLabel;
    juce::ComboBox                    sampleRateBox;

    // Duration
    juce::Label                       durationLabel;
    juce::ToggleButton                useSelection   { "Use Selection Range" };
    juce::ToggleButton                useCustom      { "Custom Duration" };
    juce::TextButton                  unitSecButton  { "S" };
    juce::TextButton                  unitBarButton  { "B" };
    juce::Label                       customLabel;
    juce::Slider                      customSlider;
    juce::Label                       customValueLabel;

    DurationUnit                      durationUnit   = DurationUnit::Seconds;

    // Generate
    juce::TextButton                  generateButton { "GENERATE" };

    // Waveform / transport
    WaveformComponent                 waveform;
    juce::TextButton                  playButton  { "Play" };
    juce::TextButton                  stopButton  { "Stop" };
    juce::ToggleButton                loopButton  { "Loop" };
    juce::TextButton                  saveWavButton { "Save WAV" };
    juce::TextButton                  copyToClipboardButton { "Copy to Clipboard" };
    juce::Label                       statusLabel  { {}, "" };

    // Progress overlay
    std::unique_ptr<ProgressOverlay>  progressOverlay;

    // T2A history panel (single column, full right-side height)
    juce::Label                       t2aHistoryLabel;
    std::unique_ptr<HistoryComponent> t2aHistory;

    // A2A panels (right column, split: INPUT on top, OUTPUT on bottom)
    std::unique_ptr<FileListComponent> a2aInputList;
    std::unique_ptr<HistoryComponent>  a2aOutputList;
    juce::Label                        a2aInputPanelLabel;
    juce::Label                        a2aOutputPanelLabel;

    // State
    Mode        currentMode = Mode::T2A;
    // Cached T2A folder.  We track this here (not via
    // processor.getHistory().getOutputDir()) because the history's
    // output dir is swapped to the A2A folder when the user enters
    // A2A mode — that would make the INPUT panel (labelled
    // "T2A folder") actually point at the A2A folder.  Keeping a
    // dedicated cache ensures the INPUT panel always reflects the
    // real T2A folder regardless of which mode we're in.
    juce::File  t2aDir;                  // T2A folder — where T2A writes its outputs
    // A2A's input browser defaults to the T2A folder (where T2A
    // writes its outputs) so a freshly generated T2A file can be
    // transformed immediately.  The user may override it via the
    // A2A input menu ("Set T2A folder...").  An empty a2aInputDir
    // means "follow the T2A folder" — when the user changes the T2A
    // folder, A2A's input list follows.  Once the user sets an
    // explicit A2A input folder, that folder stays put even if the
    // T2A folder is later changed.
    juce::File  a2aInputDir;             // empty = follow t2aDir
    juce::File  a2aOutputDir;            // A2A folder — where regenerated audio lands
    juce::File  a2aSelectedInput;        // currently chosen input file
    juce::File  playingFile;             // file currently playing through the plugin (for toggle-play in browsers)
    bool   selectionDurationAvailable = false;
    double selectionDurationSec       = 0.0;
    juce::String activeJobId;
    juce::Time lastClickTime;        // when the user last clicked GENERATE
    float  historyScanAccum = 0.0f;   // 1-second gate on output-folder rescan

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace dawalka
