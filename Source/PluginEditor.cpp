#include "PluginEditor.h"
#include "AppleSiliconDetector.h"
#include "AudioExporter.h"
#include "DragnDropSource.h"
#include "PromptBuilder.h"

namespace dawalka {

namespace {

const char* kKeyNames[12] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

// ─── Solo instrument list ──────────────────────────────────────────────
//
// When the user enables the "SOLO INSTRUMENT" toggle in T2A mode and
// picks one of these, the plugin appends "solo <name>" to the prompt
// (e.g. "cinematic, 90 BPM, F minor, solo violin").  Stable Audio 3
// recognises the "solo <instrument>" pattern and biases the output
// toward a single-instrument performance — useful for sketching lead
// lines, layering, or making a melodic element stand out in a mix.
//
// Disabled for the sa3-sm-sfx model (no concept of "solo" in foley /
// SFX generation).  A2A mode hides the whole row since A2A's prompt
// is sent verbatim with no suffix.
//
// 60 instruments, organized by family for the dropdown.  JUCE 8's
// ComboBox::addSectionalItem would let us group them visually, but a
// flat alphabetical-ish list keeps the code dead-simple and renders
// identically across JUCE versions.
const char* kSoloInstruments[] = {
    // Strings (15)
    "acoustic guitar",
    "alto saxophone",
    "bagpipes",
    "banjo",
    "bass guitar",
    "cello",
    "clarinet",
    "classical guitar",
    "double bass",
    "electric guitar",
    "flute",
    "french horn",
    "harmonica",
    "harp",
    "lute",
    "mandolin",
    "oboe",
    "piccolo",
    "recorder",
    "saxophone",
    "sitar",
    "soprano saxophone",
    "tenor saxophone",
    "trombone",
    "trumpet",
    "tuba",
    "ukulele",
    "upright bass",
    "viola",
    "violin",
    // Keys (10)
    "accordion",
    "celesta",
    "clavinet",
    "electric piano",
    "harmonium",
    "harpsichord",
    "mellotron",
    "organ",
    "piano",
    "synth",
    // Percussion (8)
    "bongos",
    "cajon",
    "congas",
    "drum kit",
    "marimba",
    "tabla",
    "timpani",
    "vibraphone",
    // Vocals (5)
    "alto vocal",
    "choir",
    "female vocal",
    "male vocal",
    "soprano vocal",
    // Other (7)
    "didgeridoo",
    "glockenspiel",
    "kalimba",
    "koto",
    "oud",
    "theremin",
    "xylophone"
};
constexpr int kNumSoloInstruments = sizeof (kSoloInstruments) / sizeof (kSoloInstruments[0]);

// Stable Audio expects "solo <instrument>" — return that, or empty
// if no instrument is picked.  Lower-case to match how the model was
// trained to see these phrases in its corpus.
inline juce::String soloTag (int comboId)
{
    if (comboId < 1 || comboId > kNumSoloInstruments) return {};
    return juce::String ("solo ") + kSoloInstruments[comboId - 1];
}

// Section label helper: small uppercase label above a group of controls.
juce::Label* makeSectionLabel (juce::Component& parent, const juce::String& text,
                                juce::Colour colour, float size = 10.5f)
{
    auto* l = new juce::Label();
    l->setText (text, juce::dontSendNotification);
    l->setFont (juce::Font (size, juce::Font::bold));
    l->setColour (juce::Label::textColourId, colour);
    l->setJustificationType (juce::Justification::topLeft);
    parent.addAndMakeVisible (l);
    return l;
}

} // namespace

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p)
{
    setLookAndFeel (&lnf);
    setResizable (true, true);
    setResizeLimits (kUiWidth, kUiHeight, 1600, 1200);

    // CRITICAL ORDERING: buildInterface() creates modelSelector / history /
    // progressOverlay (all std::unique_ptr — they're nullptr until then).
    // resized() dereferences them, so it MUST NOT run before they exist.
    // setSize() is therefore called LAST, after all children are in place.
    buildInterface();

    processor.stateBroadcaster.addChangeListener (this);
    processor.getBackend().addListener (this);
    processor.getModelManager().addListener (this);

    // Apply the editor size twice: once synchronously (in case the host
    // peer is already up and won't override it) and once asynchronously
    // (in case the host sets a 0x0 peer first and only commits a real
    // size after a message-loop tick).  Without the async pass, the user
    // would see a blank editor on first open and would have to nudge the
    // window corner to force a resize.
    setSize (kUiWidth, kUiHeight);
    juce::MessageManager::callAsync ([this] {
        setSize (kUiWidth, kUiHeight);
    });

    // Restore the user's last session: prompt, key, BPM, duration, model.
    // The PropertiesFile was reloaded by setStateInformation() before
    // the editor was constructed, so getValue() reflects the saved
    // session (or the defaults we just used in buildInterface()).
    loadFromSettings();

    // Apply the saved mode LAST — setMode() shows/hides per-mode widgets
    // and would otherwise be a no-op because the children aren't created
    // until after the constructor.
    const int savedMode = processor.getSettings().getIntValue ("mode", 0);
    setMode (savedMode == 1 ? Mode::A2A : Mode::T2A);

    // Auto-load the most recent history entry into the waveform (and
    // the processor's render buffer) so the user sees their last clip
    // — not an empty "press Generate" placeholder — when they reopen
    // the plugin.  We do this on a small delay because the history
    // rescan that runs at startup might not have populated yet.
    juce::MessageManager::callAsync ([this] {
        auto entries = processor.getHistory().getAll();
        if (entries.size() > 0)
        {
            const auto& latest = entries.getReference (0);
            if (latest.audioFile.existsAsFile())
            {
                waveform.loadFile (latest.audioFile);
                ensureProcessorRenderBuffer();
            }
        }
    });

    updateFromHost();
    startTimerHz (4);
}

void PluginEditor::loadFromSettings()
{
    auto& s = processor.getSettings();

    // ─── Migration: old folder layout ────────────────────────────────
    // Earlier versions used ~/Documents/DAWalka/outputs/ for T2A
    // outputs and ~/Documents/DAWalka/A2A/{input,output}/ for A2A.
    // The current layout is the simpler two-folder design:
    //   T2A: ~/Documents/DAWalka/T2A
    //   A2A: ~/Documents/DAWalka/A2A
    // Detect any saved value that points at an old path and rewrite
    // it to the matching new path so we don't keep recreating the
    // stale "outputs" / "A2A/output" / "A2A/input" subfolders.
    auto migrateOldPath = [&] (const juce::String& key) -> juce::String
    {
        auto v = s.getValue (key, juce::String());
        if (v.isEmpty()) return v;
        // Old paths contain "outputs", "A2A/output", or "A2A/input"
        if (v.contains ("/outputs")  ||
            v.contains ("/A2A/output") ||
            v.contains ("/A2A/input"))
        {
            juce::String migrated;
            if (v.contains ("/A2A/"))   migrated = defaultA2aDir().getFullPathName();
            else                        migrated = defaultT2aDir().getFullPathName();
            s.setValue (key, migrated);
            return migrated;
        }
        return v;
    };
    migrateOldPath ("outputDir");
    migrateOldPath ("a2aInputDir");
    migrateOldPath ("a2aOutputDir");
    s.saveIfNeeded();

    // Prompt
    promptEditor.setText (s.getValue ("prompt", kDefaultPrompt), juce::dontSendNotification);

    // Key
    keyRoot.setSelectedId (s.getIntValue ("keyRoot", kDefaultKeyRoot), juce::dontSendNotification);
    keyMode.setSelectedId (s.getIntValue ("keyMode", kDefaultKeyMode), juce::dontSendNotification);

    // BPM
    bool useProject = s.getBoolValue ("useProjectBpm", kDefaultUseProjectBpm);
    useProjectBpm.setToggleState (useProject, juce::dontSendNotification);
    bpmSlider.setValue (s.getDoubleValue ("bpm", kDefaultBpm), juce::dontSendNotification);
    bpmSlider.setEnabled (! useProject);

    // Duration
    bool customEnabled = s.getBoolValue ("useCustom", kDefaultUseCustom);
    useSelection.setToggleState (! customEnabled, juce::dontSendNotification);
    useCustom.setToggleState (customEnabled, juce::dontSendNotification);
    customSlider.setEnabled (customEnabled);
    unitSecButton.setEnabled (customEnabled);
    unitBarButton.setEnabled (customEnabled);
    durationUnit = s.getIntValue ("durationUnit", kDefaultDurationUnit) == 1
        ? DurationUnit::Bars : DurationUnit::Seconds;
    double defaultVal = (durationUnit == DurationUnit::Bars)
        ? static_cast<double> (kDefaultDurationBars)
        : static_cast<double> (kDefaultDurationSeconds);
    customSlider.setValue (s.getDoubleValue ("duration", defaultVal), juce::dontSendNotification);
    updateDurationSliderForUnit();
    updateDurationValueLabel();

    // Model — saved as the model id string (e.g. "sa3-sm-music"),
    // not as a combo-box index, because the index can change as the
    // ModelManager state evolves.  The selector remembers the request
    // and applies it as soon as the matching id appears in the list.
    juce::String savedModel = s.getValue ("modelId", juce::String());
    if (savedModel.isNotEmpty())
        modelSelector->setSelectedId (savedModel);

    // Solo instrument — independent of the model.  Disabled state
    // (FX model, A2A mode) is recomputed in updateSoloEnabled() after
    // the model is applied, so a stale "solo ON" pick survives a
    // model switch but won't be appended to a generation on FX.
    soloToggle.setToggleState (s.getBoolValue ("soloEnabled", false),
                                juce::dontSendNotification);
    soloInstrument.setSelectedId (s.getIntValue ("soloInstrument", 0),
                                  juce::dontSendNotification);
    updateSoloEnabled();

    // Prompt-tag toggles — both default ON to preserve the historical
    // behaviour of always appending KEY and BPM.  Persisted so a user
    // who turns them off for sound-effect generation on a music model
    // doesn't have to re-toggle every session.
    includeKeyInPrompt.setToggleState (
        s.getBoolValue ("includeKeyInPrompt", true), juce::dontSendNotification);
    includeBpmInPrompt.setToggleState (
        s.getBoolValue ("includeBpmInPrompt", true), juce::dontSendNotification);
    // Apply the disabled state to the KEY / BPM control rows.
    // This is a no-op in A2A (currentMode is still T2A at this
    // point — constructor-time setMode call at the end of this
    // function will re-run it for the actual mode).
    updateTagsEnabled();

    // Sample rate — default to whatever the host is running at.  If
    // the host is at a non-supported rate (88.2 / 96 / …), pick the
    // closest of the two we support.
    int savedSr = s.getIntValue ("sampleRate", 0);
    if (savedSr != 44100 && savedSr != 48000)
    {
        double hostSr = processor.getSampleRate();
        savedSr = (hostSr > 0.0 && hostSr > 46000.0) ? 48000 : 44100;
    }
    sampleRateBox.setSelectedId (savedSr == 48000 ? 2 : 1, juce::dontSendNotification);

    // Output folder.  Default lives in ~/Documents/DAWalka/T2A, but
    // a previous session may have changed it.  Apply the saved path to
    // both the history (where generated WAVs are tracked) and the
    // backend client (which passes it to the python server).
    juce::String savedOutDir = s.getValue ("outputDir", juce::String());
    if (savedOutDir.isNotEmpty())
    {
        auto f = juce::File (savedOutDir);
        t2aDir = f;
        if (f != processor.getHistory().getOutputDir())
        {
            processor.getHistory().setOutputDir (f);
            processor.getBackend().setOutputDir (f);
        }
    }

    // A2A folder (where transformed audio lands).  Default is
    // ~/Documents/DAWalka/A2A.
    juce::String savedA2aOut = s.getValue ("a2aOutputDir", juce::String());
    a2aOutputDir = savedA2aOut.isNotEmpty() ? juce::File (savedA2aOut) : defaultA2aDir();
    if (! a2aOutputDir.isDirectory())
        a2aOutputDir.createDirectory();
    // A2A's input source.  By default follows the T2A folder (so a
    // freshly generated T2A file can be transformed immediately),
    // but the user can override it via the A2A input menu.
    juce::String savedA2aIn = s.getValue ("a2aInputDir", juce::String());
    a2aInputDir = savedA2aIn.isNotEmpty() ? juce::File (savedA2aIn) : juce::File();
    if (a2aInputDir != juce::File() && ! a2aInputDir.isDirectory())
        a2aInputDir.createDirectory();
    if (a2aInputList)
        a2aInputList->setDirectory (getEffectiveA2aInputDir());

    // Restore the previously-selected A2A input file, if it still exists
    // in the (now-populated) input list.  This makes reopening the plugin
    // feel "I just paused and came back" rather than "everything was lost".
    juce::String savedSel = s.getValue ("a2aSelectedInput", juce::String());
    if (savedSel.isNotEmpty())
    {
        auto f = juce::File (savedSel);
        if (f.existsAsFile() && a2aInputList
            && f.getParentDirectory() == getEffectiveA2aInputDir())
        {
            a2aSelectedInput = f;
            a2aInputList->setSelectedFile (f);
        }
    }
    updateA2aInputFileLabel();

    // Init-noise level (A2A only).  0.7 is the typical A2A value —
    // preserves ~30% of the input character, ~70% noise.
    initNoiseSlider.setValue (
        s.getDoubleValue ("initNoiseLevel", 0.7), juce::dontSendNotification);
    updateInitNoiseValueLabel();
}

void PluginEditor::setDurationUnit (DurationUnit u)
{
    if (durationUnit == u) return;
    durationUnit = u;
    updateDurationSliderForUnit();
    updateDurationValueLabel();
    saveToSettings();
}

void PluginEditor::updateDurationSliderForUnit()
{
    if (durationUnit == DurationUnit::Bars)
    {
        // Integer step 1..32.  The actual seconds value is computed
        // by getEffectiveDurationSec() from the current BPM.
        customSlider.setRange (1.0, 32.0, 1.0);
        if (customSlider.getValue() < 1.0)  customSlider.setValue (kDefaultDurationBars);
        if (customSlider.getValue() > 32.0) customSlider.setValue (32.0);
    }
    else
    {
        customSlider.setRange (1.0, 60.0, 1.0);
    }
    // Recolour the unit buttons
    auto activeCol   = lnf.accent;
    auto inactiveCol = juce::Colour::fromRGB (50, 56, 78);
    unitSecButton.setColour (juce::TextButton::buttonColourId,
        durationUnit == DurationUnit::Seconds ? activeCol : inactiveCol);
    unitBarButton.setColour (juce::TextButton::buttonColourId,
        durationUnit == DurationUnit::Bars ? activeCol : inactiveCol);
}

void PluginEditor::updateDurationValueLabel()
{
    if (durationUnit == DurationUnit::Bars)
    {
        int bars = static_cast<int> (customSlider.getValue());
        customValueLabel.setText (juce::String (bars) + (bars == 1 ? " bar" : " bars"),
                                  juce::dontSendNotification);
    }
    else
    {
        int sec = static_cast<int> (customSlider.getValue());
        customValueLabel.setText (juce::String (sec) + " sec",
                                  juce::dontSendNotification);
    }
}

double PluginEditor::getEffectiveDurationSec() const
{
    if (! useCustom.getToggleState())
        return 0.0;   // use project selection — caller fills this in

    if (durationUnit == DurationUnit::Bars)
    {
        int bars = static_cast<int> (customSlider.getValue());
        double bpm = useProjectBpm.getToggleState()
            ? processor.getHostBpm()
            : bpmSlider.getValue();
        return barsToSeconds (bars, bpm);
    }
    return customSlider.getValue();
}

void PluginEditor::saveToSettings() const
{
    auto& s = processor.getSettings();

    s.setValue ("prompt",        promptEditor.getText());
    s.setValue ("keyRoot",       keyRoot.getSelectedId());
    s.setValue ("keyMode",       keyMode.getSelectedId());
    s.setValue ("useProjectBpm", useProjectBpm.getToggleState());
    s.setValue ("bpm",           bpmSlider.getValue());
    s.setValue ("useCustom",     useCustom.getToggleState());
    s.setValue ("duration",      customSlider.getValue());
    s.setValue ("durationUnit",  static_cast<int> (durationUnit));
    s.setValue ("modelId",       modelSelector->getSelectedId());
    s.setValue ("sampleRate",    sampleRateBox.getSelectedId() == 2 ? 48000 : 44100);
    s.setValue ("mode",          static_cast<int> (currentMode));
    s.setValue ("soloEnabled",   soloToggle.getToggleState());
    s.setValue ("soloInstrument",soloInstrument.getSelectedId());
    s.setValue ("includeKeyInPrompt", includeKeyInPrompt.getToggleState());
    s.setValue ("includeBpmInPrompt", includeBpmInPrompt.getToggleState());
    // a2aInputDir is the A2A input source.  Empty string means
    // "follow the T2A folder" (the default).  An explicit value
    // means the user has chosen a custom A2A input folder via
    // the A2A input menu.
    s.setValue ("a2aInputDir",   a2aInputDir == juce::File() ? juce::String() : a2aInputDir.getFullPathName());
    s.setValue ("a2aOutputDir",  a2aOutputDir.getFullPathName());
    s.setValue ("a2aSelectedInput", a2aSelectedInput.getFullPathName());
    s.setValue ("initNoiseLevel", initNoiseSlider.getValue());
    s.saveIfNeeded();
}

PluginEditor::~PluginEditor()
{
    processor.getBackend().removeListener (this);
    processor.getModelManager().removeListener (this);
    processor.stateBroadcaster.removeChangeListener (this);
}

void PluginEditor::buildInterface()
{
    buildHeader();
    buildMain();
    buildT2aHistoryPanel();
    buildA2aPanels();
    buildFooter();
    progressOverlay = std::make_unique<ProgressOverlay>();
    addAndMakeVisible (*progressOverlay);
    progressOverlay->setOnCancel ([this] { cancelClicked(); });
    progressOverlay->setVisible (false);
}

void PluginEditor::buildHeader()
{
    addAndMakeVisible (headerTitle);
    headerTitle.setText ("DAWalka", juce::dontSendNotification);
    headerTitle.setFont (juce::Font (22.0f, juce::Font::bold));
    headerTitle.setColour (juce::Label::textColourId, lnf.text);
    headerTitle.setJustificationType (juce::Justification::topLeft);

    addAndMakeVisible (headerSubtitle);
    headerSubtitle.setText ("AI Audio Generator | Stable Audio 3 | Local",
                             juce::dontSendNotification);
    headerSubtitle.setFont (juce::Font (11.5f));
    headerSubtitle.setColour (juce::Label::textColourId, lnf.textMuted);
    headerSubtitle.setJustificationType (juce::Justification::topLeft);

    addAndMakeVisible (chipLabel);
    chipLabel.setFont (juce::Font (10.5f));
    chipLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    chipLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (backendStatusLabel);
    backendStatusLabel.setFont (juce::Font (10.5f));
    backendStatusLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    backendStatusLabel.setJustificationType (juce::Justification::centredRight);

    auto rep = AppleSiliconDetector::detectBasic();
    if (rep.isAppleSilicon)
        chipLabel.setText (rep.chipString + "  " +
                           juce::String (rep.memBytes / 1'073'741'824) + " GB",
                           juce::dontSendNotification);
    else
        chipLabel.setText ("Intel (no MLX)", juce::dontSendNotification);

    // ── Mode tabs (T2A | A2A) — placed in a row just below the header.
    // Two equal-width full-height buttons side by side.  Active one gets
    // the accent colour; inactive one is dark.
    for (auto* b : { &modeTabT2a, &modeTabA2a })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId,   juce::Colour::fromRGB (32, 35, 46));
        b->setColour (juce::TextButton::buttonOnColourId, lnf.accent);
        b->setColour (juce::TextButton::textColourOffId,  lnf.textMuted);
        b->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    }
    modeTabT2a.onClick = [this] { onModeTabClicked (Mode::T2A); };
    modeTabA2a.onClick = [this] { onModeTabClicked (Mode::A2A); };
}

void PluginEditor::onModeTabClicked (Mode m)
{
    if (currentMode == m) return;
    setMode (m);
    saveToSettings();
}

void PluginEditor::setMode (Mode newMode)
{
    currentMode = newMode;

    // Reflect in the tab buttons
    {
        auto active = juce::Colour::fromRGB (255, 122, 89);  // accent
        auto inactive = juce::Colour::fromRGB (32, 35, 46);
        modeTabT2a.setColour (juce::TextButton::buttonColourId,
            currentMode == Mode::T2A ? active : inactive);
        modeTabA2a.setColour (juce::TextButton::buttonColourId,
            currentMode == Mode::A2A ? active : inactive);
        modeTabT2a.setColour (juce::TextButton::textColourOffId,
            currentMode == Mode::T2A ? juce::Colours::white : lnf.textMuted);
        modeTabA2a.setColour (juce::TextButton::textColourOffId,
            currentMode == Mode::A2A ? juce::Colours::white : lnf.textMuted);
    }

    // T2A-only widgets
    const bool t2a = (currentMode == Mode::T2A);
    t2aHistoryLabel.setVisible (t2a);
    t2aHistory->setVisible (t2a);

    // These parameters are T2A-only.  A2A ignores KEY / BPM (the prompt
    // is sent verbatim with no suffix), and the output duration is
    // always equal to the input file's length — there's nothing to
    // configure.  The sample rate dropdown is also T2A-only because
    // the model runs at 44.1 kHz internally and A2A resamples to
    // whatever the project expects at the host.
    keyLabel          .setVisible (t2a);
    keyRoot           .setVisible (t2a);
    keyMode           .setVisible (t2a);
    bpmLabel          .setVisible (t2a);
    useProjectBpm     .setVisible (t2a);
    bpmSlider         .setVisible (t2a);
    bpmValueLabel     .setVisible (t2a);
    autoBpmButton     .setVisible (t2a);
    sampleRateLabel   .setVisible (t2a);
    sampleRateBox     .setVisible (t2a);
    durationLabel     .setVisible (t2a);
    useSelection      .setVisible (t2a);
    useCustom         .setVisible (t2a);
    unitSecButton     .setVisible (t2a);
    unitBarButton     .setVisible (t2a);
    customLabel       .setVisible (t2a);
    customSlider      .setVisible (t2a);
    customValueLabel  .setVisible (t2a);

    // A2A-only widgets
    const bool a2a = (currentMode == Mode::A2A);
    a2aInputList->setVisible (a2a);
    a2aInputPanelLabel.setVisible (a2a);
    a2aOutputPanelLabel.setVisible (a2a);
    a2aOutputList->setVisible (a2a);
    initNoiseLabel.setVisible (a2a);
    initNoiseSlider.setVisible (a2a);
    initNoiseValueLabel.setVisible (a2a);
    initNoisePresetSubtle.setVisible (a2a);
    initNoisePresetBalanced.setVisible (a2a);
    initNoisePresetCreative.setVisible (a2a);
    // Solo-instrument row is SHARED between T2A and A2A.  In both
    // modes the user can append "solo <instrument>" to the prompt;
    // updateSoloEnabled() and the toggle's onClick handler gate
    // the dropdown on the FX model.
    soloToggle.setVisible (true);
    soloInstrument.setVisible (true);
    // KEY/BPM tag toggles are T2A-only — A2A's prompt is verbatim,
    // no metadata tags to gate.
    tagsLabel           .setVisible (t2a);
    includeKeyInPrompt  .setVisible (t2a);
    includeBpmInPrompt  .setVisible (t2a);
    // RANDOM SFX button — T2A-only.  A2A only exposes RANDOM MUSIC
    // (its current workflow doesn't support an SFX category).
    // The layout slot is reserved either way in resized() so the
    // PROMPT row doesn't shift when the button disappears.
    randomSfxPromptButton.setVisible (t2a);
    // SOLO is shared between T2A and A2A (the user can solo an
    // instrument when transforming input audio as well as when
    // generating from scratch).  Re-apply the FX-model disable,
    // since the controls' visibility doesn't re-evaluate their
    // enabled state on its own.
    updateSoloEnabled();
    // KEY / BPM controls follow the corresponding tag toggle.
    // A2A hides them entirely; in T2A they gray out when the tag
    // is off.
    updateTagsEnabled();
    // "INPUT: foo.aiff (7.96 s)" is redundant with the INPUT pane on
    // the right (which already shows the same filename with size,
    // duration, and mtime).  Hide the duplicate.
    a2aInputFileLabel.setVisible (false);

    // When entering A2A, swap the History's output dir to a2aOutputDir
    // so the OUTPUT list scans the right folder.  When leaving, swap
    // back to the T2A output dir.  We don't persist this — it's a
    // runtime-only pointer swap; the next session starts in T2A mode
    // with the right T2A dir.
    //
    // A2A's INPUT browser is re-pointed at the EFFECTIVE A2A input
    // dir (= T2A folder by default, or a custom folder if the user
    // picked one via the menu) so it always shows the right contents
    // even if the user changed the T2A folder while in T2A mode
    // (applyT2aOutputFolderChange keeps it in sync when a2aInputDir
    // is empty).
    auto& history = processor.getHistory();
    if (a2a)
    {
        // ORDER MATTERS: setDirectory() must be called BEFORE
        // setOutputDir(), because getEffectiveA2aInputDir() falls back
        // to history.getOutputDir() when a2aInputDir is empty (= "follow
        // T2A").  If we swapped the order, the input list would briefly
        // point at the A2A folder (the new history output) instead of
        // the T2A folder (the one we just left) and the user would see
        // an empty INPUT list when switching to A2A right after
        // generating in T2A.
        if (a2aInputList)
            a2aInputList->setDirectory (getEffectiveA2aInputDir());
        if (a2aOutputDir != juce::File())
            history.setOutputDir (a2aOutputDir);
    }
    else
    {
        // Restore T2A's output dir from settings (or default)
        juce::String saved = processor.getSettings().getValue ("outputDir", juce::String());
        auto f = saved.isNotEmpty() ? juce::File (saved) : defaultT2aDir();
        t2aDir = f;
        history.setOutputDir (f);
        // The A2A INPUT list, when following T2A, is implicitly bound
        // to history.getOutputDir() — so it just follows.  Force a
        // refresh so it picks up the new T2A folder contents.
        if (a2aInputList)
            a2aInputList->setDirectory (getEffectiveA2aInputDir());
    }

    updateA2aInputFileLabel();
    updateGenerateButton();
    // The directory swap above triggers a rebuild of the browser
    // rows, so the newly-created row buttons are back to "Play"
    // by default.  Re-apply the playing state so the row for the
    // currently-playing file flips to "Stop" in the new view.
    if (t2aHistory)    t2aHistory->setPlayingFile    (playingFile);
    if (a2aOutputList) a2aOutputList->setPlayingFile (playingFile);
    if (a2aInputList)  a2aInputList->setPlayingFile  (playingFile);
    resized();
    repaint();
}

void PluginEditor::updateA2aInputFileLabel()
{
    const bool followingT2a = (a2aInputDir == juce::File());
    const juce::String folderTag = followingT2a ? "T2A folder" : "A2A input folder";
    if (a2aSelectedInput.existsAsFile())
    {
        a2aInputFileLabel.setText (
            folderTag + " file: " + a2aSelectedInput.getFileName()
            + "  (" + juce::String (AudioExporter::getAudioDuration (a2aSelectedInput), 2) + " s)",
            juce::dontSendNotification);
    }
    else
    {
        a2aInputFileLabel.setText (folderTag + " file: — pick a file from the list above",
                                    juce::dontSendNotification);
    }
}

void PluginEditor::updateInitNoiseValueLabel()
{
    float v = static_cast<float> (initNoiseSlider.getValue());
    juce::String desc = (v < 0.2f) ? "preserve" :
                        (v < 0.4f) ? "subtle" :
                        (v < 0.6f) ? "moderate" :
                        (v < 0.85f) ? "creative" : "regenerate";
    initNoiseValueLabel.setText (juce::String (v, 2) + "  (" + desc + ")",
                                  juce::dontSendNotification);

    // Highlight the quick-pick chip whose value is closest to the current
    // slider value (within 0.05).  Multiple chips may light up if the
    // value lands near a boundary, but in practice the slider's 0.01 step
    // means at most one will be within range.
    const float eps = 0.05f;
    auto highlight = [v, eps] (juce::TextButton& b, float presetValue)
    {
        bool active = std::abs (v - presetValue) < eps;
        b.setColour (juce::TextButton::buttonColourId,
            active ? juce::Colour::fromRGB (255, 122, 89)        // accent when active
                   : juce::Colour::fromRGB (50, 56, 78));
        b.setColour (juce::TextButton::textColourOffId,
            active ? juce::Colours::white
                   : juce::Colour::fromRGB (200, 204, 220));
    };
    highlight (initNoisePresetSubtle,   0.30f);
    highlight (initNoisePresetBalanced, 0.60f);
    highlight (initNoisePresetCreative, 0.90f);
}

void PluginEditor::onInitNoisePresetClicked (float value)
{
    initNoiseSlider.setValue (value, juce::sendNotification);
    // setValue with sendNotification triggers onValueChange → updates
    // the value label and saves settings.  No need to call those again.
}

double PluginEditor::measureAudioSeconds (const juce::File& f) const
{
    // Thin wrapper preserved for the public API.  Delegates to the safe
    // header-only probe so we never read the full file into memory just
    // to count samples — that crashes on large or malformed AIFF.
    return AudioExporter::getAudioDuration (f);
}

void PluginEditor::buildMain()
{
    // Section labels
    modelLabel.setText ("MODEL", juce::dontSendNotification);
    modelLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    modelLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    addAndMakeVisible (modelLabel);

    // Model selector
    modelSelector = std::make_unique<ModelSelectorComponent> (processor.getModelManager());
    addAndMakeVisible (*modelSelector);
    modelSelector->setOnSelectionChanged ([this] (const juce::String&) {
        updateGenerateButton();
        updateSoloEnabled();
        saveToSettings();
    });

    // Prompt
    promptLabel.setText ("PROMPT", juce::dontSendNotification);
    promptLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    promptLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    addAndMakeVisible (promptLabel);

    addAndMakeVisible (promptEditor);
    promptEditor.setText (kDefaultPrompt);
    promptEditor.setFont (juce::Font (13.5f));
    promptEditor.setMultiLine (true);
    promptEditor.setReturnKeyStartsNewLine (false);
    promptEditor.onTextChange = [this] {
        saveToSettings();
        // prompt emptiness gates the Generate button (with SOLO
        // providing an alternative source of prompt content).
        updateGenerateButton();
    };

    // "RANDOM MUSIC" prompt button — sits in the top-right of the
    // prompt block (next to the PROMPT label).  On click, replaces
    // the current prompt with a random music composition from
    // PromptLibrary tuned to the current mode.  Visible in both
    // T2A and A2A.
    addAndMakeVisible (randomMusicPromptButton);
    randomMusicPromptButton.setColour (juce::TextButton::buttonColourId,
        juce::Colour::fromRGB (54, 60, 82));
    randomMusicPromptButton.setColour (juce::TextButton::textColourOffId,
        juce::Colour::fromRGB (225, 229, 245));
    randomMusicPromptButton.setTooltip (
        "Generate a random MUSIC prompt for the current mode");
    randomMusicPromptButton.onClick = [this] { onRandomMusicClicked(); };

    // "RANDOM SFX" prompt button — sits just to the left of RANDOM
    // MUSIC.  On click, replaces the current prompt with a random
    // sound-effect composition from PromptLibrary.  Hidden in A2A
    // mode (the A2A workflow currently only exposes MUSIC; sfx
    // category can be added later if needed).
    addAndMakeVisible (randomSfxPromptButton);
    randomSfxPromptButton.setColour (juce::TextButton::buttonColourId,
        juce::Colour::fromRGB (54, 60, 82));
    randomSfxPromptButton.setColour (juce::TextButton::textColourOffId,
        juce::Colour::fromRGB (225, 229, 245));
    randomSfxPromptButton.setTooltip (
        "Generate a random SFX prompt (T2A only).  "
        "Best paired with the SFX model.");
    randomSfxPromptButton.onClick = [this] { onRandomSfxClicked(); };

    // ── Solo instrument row (T2A only) ─────────────────────────────
    // Sits under the prompt field, in the vertical slot that
    // A2A reserves for the INIT-NOISE row.  Hidden in A2A mode
    // by setMode().  Disabled for the FX model by updateSoloEnabled().
    addAndMakeVisible (soloToggle);
    soloToggle.setColour (juce::ToggleButton::textColourId, lnf.textMuted);
    soloToggle.setColour (juce::ToggleButton::tickColourId,
                          juce::Colour::fromRGB (255, 122, 89));
    soloToggle.setColour (juce::ToggleButton::tickDisabledColourId,
                          juce::Colour::fromRGB (90, 95, 115));
    soloToggle.setTooltip (
        "Force the model to perform a single instrument.\n"
        "Appends 'solo <instrument>' to the prompt before generation.\n"
        "Disabled for the FX model (no 'solo' concept in SFX).");
    soloToggle.onClick = [this] {
        onSoloToggleChanged();
        saveToSettings();
    };

    addAndMakeVisible (soloInstrument);
    for (int i = 0; i < kNumSoloInstruments; ++i)
        soloInstrument.addItem (kSoloInstruments[i], i + 1);
    soloInstrument.setTextWhenNothingSelected ("Pick an instrument...");
    soloInstrument.setSelectedId (0, juce::dontSendNotification);
    soloInstrument.setEnabled (false);  // off until toggle is on
    soloInstrument.setTooltip (
        "Instrument to feature as a solo in the generated audio");
    soloInstrument.onChange = [this] {
        saveToSettings();
        updateGenerateButton();
    };

    // ── Prompt tags (T2A only) ──────────────────────────────────────
    // "Include KEY / BPM in prompt" — see PluginEditor.h for the full
    // rationale.  Both default ON.  Hidden in A2A by setMode().
    addAndMakeVisible (tagsLabel);
    tagsLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    tagsLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    tagsLabel.setJustificationType (juce::Justification::centredRight);
    tagsLabel.setTooltip (
        "Tags appended to the prompt that bias the model toward tonal, "
        "rhythmic output.  Untick one or both when using a music model "
        "for sound effects or untextured sounds.\n\n"
        "Does NOT affect the duration slider or host-tempo sync — those "
        "use the BPM value directly, not the prompt tag.");

    addAndMakeVisible (includeKeyInPrompt);
    includeKeyInPrompt.setColour (juce::ToggleButton::textColourId, lnf.textMuted);
    includeKeyInPrompt.setColour (juce::ToggleButton::tickColourId,
                                  juce::Colour::fromRGB (255, 122, 89));
    includeKeyInPrompt.setTooltip (
        "If ON, appends the selected KEY to the prompt (e.g. 'F minor').\n"
        "Turn OFF when generating sound effects on a music model.\n"
        "When OFF, the KEY dropdowns are grayed so you don't think\n"
        "you're setting a value that won't reach the prompt.");
    includeKeyInPrompt.onClick = [this]
    {
        saveToSettings();
        updateTagsEnabled();
    };

    addAndMakeVisible (includeBpmInPrompt);
    includeBpmInPrompt.setColour (juce::ToggleButton::textColourId, lnf.textMuted);
    includeBpmInPrompt.setColour (juce::ToggleButton::tickColourId,
                                  juce::Colour::fromRGB (255, 122, 89));
    includeBpmInPrompt.setTooltip (
        "If ON, appends the BPM value to the prompt (e.g. '120 BPM').\n"
        "Turn OFF when generating sound effects on a music model.\n"
        "The BPM value still drives the duration slider — only the "
        "prompt tag is suppressed.\n"
        "When OFF, the BPM controls are grayed so you don't think "
        "you're setting a value that won't reach the prompt.");
    includeBpmInPrompt.onClick = [this]
    {
        saveToSettings();
        updateTagsEnabled();
    };

    // Init-noise slider (A2A only — hidden in T2A by setMode())
    addAndMakeVisible (initNoiseLabel);
    initNoiseLabel.setText ("INIT NOISE", juce::dontSendNotification);
    initNoiseLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    initNoiseLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    addAndMakeVisible (initNoiseSlider);
    initNoiseSlider.setRange (0.0, 1.0, 0.01);
    initNoiseSlider.setValue (0.7);
    initNoiseSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    initNoiseSlider.setTextValueSuffix ("");
    initNoiseSlider.onValueChange = [this] { updateInitNoiseValueLabel(); saveToSettings(); };
    addAndMakeVisible (initNoiseValueLabel);
    initNoiseValueLabel.setFont (juce::Font (11.5f));
    initNoiseValueLabel.setColour (juce::Label::textColourId, lnf.text);

    // Quick-pick noise preset chips.  Hovering/clicking sets the slider
    // to a known good value; the active preset (whichever is closest to
    // the current value) is highlighted.
    auto presetStyle = [this] (juce::TextButton& b, const juce::String& tip)
    {
        addAndMakeVisible (b);
        b.setTooltip (tip);
        b.setColour (juce::TextButton::buttonColourId,
                     juce::Colour::fromRGB (50, 56, 78));
        b.setColour (juce::TextButton::textColourOffId,
                     juce::Colour::fromRGB (200, 204, 220));
    };
    presetStyle (initNoisePresetSubtle,
                 "Subtle transformation — preserves most of the input character (σmax = 0.30)");
    presetStyle (initNoisePresetBalanced,
                 "Balanced — roughly half/half input and regeneration (σmax = 0.60)");
    presetStyle (initNoisePresetCreative,
                 "Creative — strong regeneration, only the rough shape of the input remains (σmax = 0.90)");
    initNoisePresetSubtle  .onClick = [this] { onInitNoisePresetClicked (0.30f); };
    initNoisePresetBalanced.onClick = [this] { onInitNoisePresetClicked (0.60f); };
    initNoisePresetCreative.onClick = [this] { onInitNoisePresetClicked (0.90f); };
    updateInitNoiseValueLabel();

    // "Currently selected input file" indicator (A2A only)
    addAndMakeVisible (a2aInputFileLabel);
    a2aInputFileLabel.setFont (juce::Font (11.0f, juce::Font::italic));
    a2aInputFileLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    a2aInputFileLabel.setJustificationType (juce::Justification::centredLeft);
    updateA2aInputFileLabel();

    // Key
    keyLabel.setText ("KEY", juce::dontSendNotification);
    keyLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    keyLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    addAndMakeVisible (keyLabel);

    for (int i = 0; i < 12; ++i)
        keyRoot.addItem (kKeyNames[i], i + 1);
    keyRoot.setSelectedId (kDefaultKeyRoot);
    keyRoot.onChange = [this] { saveToSettings(); };
    addAndMakeVisible (keyRoot);

    keyMode.addItem ("Major", 1);
    keyMode.addItem ("Minor", 2);
    keyMode.setSelectedId (kDefaultKeyMode);
    keyMode.onChange = [this] { saveToSettings(); };
    addAndMakeVisible (keyMode);

    // BPM
    bpmLabel.setText ("TEMPO", juce::dontSendNotification);
    bpmLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    addAndMakeVisible (bpmLabel);

    addAndMakeVisible (useProjectBpm);
    useProjectBpm.setToggleState (kDefaultUseProjectBpm, juce::dontSendNotification);
    useProjectBpm.onClick = [this] {
        bpmSlider.setEnabled (!useProjectBpm.getToggleState());
        if (useProjectBpm.getToggleState())
            updateFromHost();
        saveToSettings();
    };

    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (kDefaultBpm);
    bpmSlider.setEnabled (!kDefaultUseProjectBpm);
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.onValueChange = [this] { saveToSettings(); };
    addAndMakeVisible (bpmSlider);

    addAndMakeVisible (bpmValueLabel);
    bpmValueLabel.setFont (juce::Font (11.5f));
    bpmValueLabel.setColour (juce::Label::textColourId, lnf.text);

    // Sample-rate selector — affects the WAV file the Python backend
    // writes.  Default picks up the host's current project rate the
    // first time the plugin opens.
    sampleRateLabel.setText ("SR", juce::dontSendNotification);
    sampleRateLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    sampleRateLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    addAndMakeVisible (sampleRateLabel);
    sampleRateBox.addItem ("44.1 kHz", 1);
    sampleRateBox.addItem ("48 kHz",   2);
    sampleRateBox.onChange = [this] { saveToSettings(); };
    addAndMakeVisible (sampleRateBox);

    // Duration
    durationLabel.setText ("DURATION", juce::dontSendNotification);
    durationLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    durationLabel.setColour (juce::Label::textColourId, lnf.textMuted);
    addAndMakeVisible (durationLabel);

    addAndMakeVisible (useSelection);
    useSelection.setToggleState (!kDefaultUseCustom, juce::dontSendNotification);
    useSelection.onClick = [this] {
        useCustom.setToggleState (false, juce::dontSendNotification);
        unitSecButton.setEnabled (false);
        unitBarButton.setEnabled (false);
        customSlider.setEnabled (false);
        saveToSettings();
    };
    addAndMakeVisible (useCustom);
    useCustom.onClick = [this] {
        useSelection.setToggleState (false, juce::dontSendNotification);
        unitSecButton.setEnabled (true);
        unitBarButton.setEnabled (true);
        customSlider.setEnabled (true);
        updateDurationSliderForUnit();
        saveToSettings();
    };

    // SEC | BAR segmented control
    unitSecButton.setClickingTogglesState (false);
    unitBarButton.setClickingTogglesState (false);
    unitSecButton.onClick = [this] { setDurationUnit (DurationUnit::Seconds); };
    unitBarButton.onClick = [this] { setDurationUnit (DurationUnit::Bars);    };
    unitSecButton.setEnabled (kDefaultUseCustom);
    unitBarButton.setEnabled (kDefaultUseCustom);
    addAndMakeVisible (unitSecButton);
    addAndMakeVisible (unitBarButton);

    customSlider.setRange (1.0, 60.0, 1.0);
    customSlider.setValue (kDefaultDurationSeconds);
    customSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    customSlider.setEnabled (kDefaultUseCustom);
    customSlider.onValueChange = [this] { saveToSettings(); updateDurationValueLabel(); };
    addAndMakeVisible (customSlider);
    addAndMakeVisible (customValueLabel);
    customValueLabel.setFont (juce::Font (11.5f));
    customValueLabel.setColour (juce::Label::textColourId, lnf.text);
    updateDurationValueLabel();

    // Generate - prominent button
    addAndMakeVisible (generateButton);
    generateButton.setColour (juce::TextButton::buttonColourId, lnf.accent);
    generateButton.setColour (juce::TextButton::buttonOnColourId, lnf.accent);
    generateButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    generateButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    generateButton.onClick = [this] { generateClicked(); };

    // Waveform + transport
    addAndMakeVisible (waveform);
    // Mouse-down/drag on the waveform seeks the processor's render
    // position (and resumes playback on mouse-up if it was playing).
    // We intentionally do NOT start a JUCE drag-and-drop here — the
    // user can use the "Copy to Clipboard" button to push the file
    // onto the macOS pasteboard and Cmd+V it into Logic.
    waveform.onSeek = [this] (double sec, bool wasPlaying) {
        processor.setRenderPosition (sec);
        if (wasPlaying)
            processor.playRender();
    };

    addAndMakeVisible (playButton);
    playButton.onClick = [this] { playClicked(); };
    addAndMakeVisible (stopButton);
    stopButton.onClick = [this] { stopClicked(); };
    addAndMakeVisible (loopButton);
    loopButton.setToggleState (true, juce::dontSendNotification);
    loopButton.onClick = [this] { loopToggled(); };
    addAndMakeVisible (saveWavButton);
    saveWavButton.onClick = [this] { saveWavClicked(); };

    addAndMakeVisible (copyToClipboardButton);
    copyToClipboardButton.setColour (juce::TextButton::buttonColourId, lnf.accent);
    copyToClipboardButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    copyToClipboardButton.onClick = [this] { copyToClipboardClicked(); };

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (140, 220, 160));
    statusLabel.setFont (juce::Font (11.0f, juce::Font::italic));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
}

void PluginEditor::buildT2aHistoryPanel()
{
    addAndMakeVisible (t2aHistoryLabel);
    t2aHistoryLabel.setText ("T2A folder", juce::dontSendNotification);
    t2aHistoryLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    t2aHistoryLabel.setColour (juce::Label::textColourId, lnf.textMuted);

    t2aHistory = std::make_unique<HistoryComponent> (processor.getHistory());
    addAndMakeVisible (*t2aHistory);
    t2aHistory->setOnSelectRequested  ([this] (const HistoryEntry& e) { onT2aHistorySelect  (e); });
    t2aHistory->setOnPlayRequested    ([this] (const HistoryEntry& e) { onT2aHistoryPlay    (e); });
    t2aHistory->setOnDeleteRequested  ([this] (const HistoryEntry& e) { onT2aHistoryDelete  (e); });
    t2aHistory->setOnClearRequested   ([this]                          { onT2aHistoryClear   (); });
    t2aHistory->setOnMenuRequested    ([this]                          { showT2aHistoryMenu  (); });
}

void PluginEditor::buildA2aPanels()
{
    // INPUT list (top half of the right column in A2A mode) — reads
    // from the SHARED T2A folder (where T2A writes its outputs and
    // where the user can drop audio files for transformation).
    a2aInputList = std::make_unique<FileListComponent>();
    addAndMakeVisible (*a2aInputList);
    a2aInputList->setOnSelect ([this] (const juce::File& f) { onA2aInputSelect (f); });
    a2aInputList->setOnPlay   ([this] (const juce::File& f) { onA2aInputPlay   (f); });
    a2aInputList->setOnDelete ([this] (const juce::File& f) { onA2aInputDelete (f); });
    a2aInputList->setOnMenu   ([this]                          { onA2aInputMenu   (); });
    a2aInputList->setOnFilesDropped ([this] (const juce::Array<juce::File>& files)
    {
        onA2aInputFilesDropped (files);
    });
    // Look up each file in the generation history by its filename
    // stem (= HistoryEntry::id) and pass the metadata to the row
    // painter, so the INPUT panel renders identically to the OUTPUT
    // panel for files we generated in DAWalka.  Files dropped in from
    // Finder with no matching history entry fall back to the simple
    // filename + size + duration + mtime view.
    a2aInputList->setMetadataProvider (
        [this] (const juce::File& f) -> std::optional<HistoryEntry>
        {
            return processor.getHistory().findById (f.getFileNameWithoutExtension());
        });

    // OUTPUT list (bottom half) — reuses HistoryComponent.  We point it
    // at the same GenerationHistory, but the editor will call setOutputDir
    // on the history with a2aOutputDir when entering A2A mode.
    addAndMakeVisible (a2aOutputPanelLabel);
    a2aOutputPanelLabel.setText ("A2A folder", juce::dontSendNotification);
    a2aOutputPanelLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    a2aOutputPanelLabel.setColour (juce::Label::textColourId, lnf.textMuted);

    // INPUT panel label (top of the A2A side column).  Reads from
    // the SHARED T2A folder — there is no separate A2A input folder
    // in the new layout.
    addAndMakeVisible (a2aInputPanelLabel);
    a2aInputPanelLabel.setText ("T2A folder", juce::dontSendNotification);
    a2aInputPanelLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    a2aInputPanelLabel.setColour (juce::Label::textColourId, lnf.textMuted);

    a2aOutputList = std::make_unique<HistoryComponent> (processor.getHistory());
    addAndMakeVisible (*a2aOutputList);
    a2aOutputList->setOnSelectRequested ([this] (const HistoryEntry& e) { onA2aOutputSelect (e); });
    a2aOutputList->setOnPlayRequested   ([this] (const HistoryEntry& e) { onA2aOutputPlay   (e); });
    a2aOutputList->setOnDeleteRequested ([this] (const HistoryEntry& e) { onA2aOutputDelete (e); });
    a2aOutputList->setOnClearRequested  ([this]                          { onA2aOutputClear  (); });
    a2aOutputList->setOnMenuRequested   ([this]                          { showA2aOutputMenu (); });
}

void PluginEditor::buildFooter() {}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.background);

    // Subtle separator below the header
    g.setColour (lnf.surfaceAlt);
    g.fillRect (0, kHeaderHeight, getWidth(), 1);
}

void PluginEditor::visibilityChanged()
{
    if (! isVisible())
        return;

    // Some AU hosts open the editor at a 0x0 (or remembered-last) size
    // and only commit a real peer size after the component becomes visible.
    // Re-applying the intended size here is what actually makes the UI
    // appear on first open without the user having to drag the corner.
    if (getWidth() != kUiWidth || getHeight() != kUiHeight)
        setSize (kUiWidth, kUiHeight);

    // The host may have laid the children out at the wrong (small) size
    // before our setSize took effect.  Force a fresh layout + repaint so
    // every child is correctly positioned on the very first show.
    resized();
    repaint();
}

void PluginEditor::resized()
{
    // Defence in depth: unique_ptr children can be null during the first
    // paint/resized dispatch if the host is aggressive.
    if (!modelSelector || !t2aHistory || !a2aInputList || !a2aOutputList || !progressOverlay)
        return;

    const int W = getWidth();
    const int H = getHeight();

    // ── HEADER (y: 0..kHeaderHeight) ────────────────────────────────────
    {
        const int padX = kOuterPad;
        const int midY = 4;
        const int titleH = 24;
        const int subH   = 16;

        // Title (left)
        headerTitle.setBounds (padX, midY, 220, titleH);
        // Backend status (right of title row)
        backendStatusLabel.setBounds (W - padX - 200, midY + 4, 200, titleH - 4);

        // Subtitle (left, below title)
        headerSubtitle.setBounds (padX, midY + titleH + 2, 400, subH);
        // Chip (right, below status)
        chipLabel.setBounds (W - padX - 220, midY + titleH + 2, 220, subH);
    }

    // ── MODE TABS (y: kHeaderHeight..kHeaderHeight+kModeTabHeight) ──────
    {
        const int tabsY = kHeaderHeight;
        // The two buttons each take 50% of the available width minus a
        // small gap, with a vertical separator between them.
        const int tabW = (W - kOuterPad * 2 - 4) / 2;
        modeTabT2a.setBounds (kOuterPad,             tabsY + 4, tabW, kModeTabHeight - 8);
        modeTabA2a.setBounds (kOuterPad + tabW + 4,  tabsY + 4, tabW, kModeTabHeight - 8);
    }

    // Content area starts below header + tabs
    const int contentY = kHeaderHeight + kModeTabHeight + kOuterPad;
    const int contentH = H - contentY - kOuterPad;

    // ── TWO COLUMNS — main on the left, side panel(s) on the right.
    //    The right panel extends ALL THE WAY to the right edge
    //    of the editor so there is no gray strip on the right.
    const int sideW = static_cast<int> (W * 0.30);    // ~30% for side panel
    const int sideX = W - sideW;                       // touches the right edge
    const int mainX = kOuterPad;
    const int mainW = sideX - mainX - kOuterPad;       // gap only between columns

    // ── SIDE COLUMN: T2A folder (T2A) or T2A folder + A2A folder (A2A)
    if (currentMode == Mode::T2A)
    {
        int y = contentY;
        t2aHistoryLabel.setBounds (sideX, y, sideW, 16);
        y += 20;
        t2aHistory->setBounds (sideX, y, sideW, contentH - 20);
    }
    else   // A2A
    {
        // Top half: T2A folder (input source — shared with T2A mode),
        // Bottom half: A2A folder (transformed outputs).
        const int halfH = (contentH - 4) / 2;
        const int inputY = contentY;
        const int outputY = contentY + halfH + 4;

        a2aInputPanelLabel.setBounds (sideX, inputY, sideW, 16);
        a2aInputList       ->setBounds (sideX, inputY + 20, sideW, halfH - 20);
        a2aOutputPanelLabel.setBounds (sideX, outputY, sideW, 16);
        a2aOutputList      ->setBounds (sideX, outputY + 20, sideW, halfH - 20);
    }

    // ── MAIN COLUMN ─────────────────────────────────────────────────────
    int y = contentY;
    int x = mainX;
    int colW = mainW;
    const int gap = 8;

    // MODEL — single compact row (label + selector)
    modelLabel.setBounds (x, y, 70, 22);
    modelSelector->setBounds (x + 74, y, colW - 74, 22);
    y += 22 + gap;

    // PROMPT — single line, ~3 lines tall.  The two "RANDOM" buttons
    // sit in the top-right of this row, on the same line as the
    // PROMPT label, so they never steal height from the text field
    // below.  SFX is hidden in A2A but the layout slot is reserved.
    const int randBtnW = 88;
    const int randBtnGap = 4;
    const int randBtnH = 18;
    promptLabel.setBounds (x, y,
                           colW - 2 * randBtnW - randBtnGap - 6, 14);
    randomSfxPromptButton.setBounds (x + colW - 2 * randBtnW - randBtnGap,
                                     y - 2, randBtnW, randBtnH);
    randomMusicPromptButton.setBounds (x + colW - randBtnW, y - 2,
                                       randBtnW, randBtnH);
    y += 16;
    promptEditor.setBounds (x, y, colW, 64);
    y += 64 + gap;

    // SOLO-INSTRUMENT row (SHARED by T2A and A2A — the user can solo
    // an instrument in both modes).  Always positioned at the same y
    // so the layout doesn't shift between modes.  The toggle takes
    // 150 px on the left, the dropdown fills the rest.  Disabled
    // for the FX model by updateSoloEnabled().
    {
        const int soloH = 22;
        const int toggleW = 150;
        soloToggle      .setBounds (x, y, toggleW, soloH);
        soloInstrument  .setBounds (x + toggleW + 6, y, colW - toggleW - 6, soloH);
        y += soloH;
    }

    // TAGS row (T2A only) / INIT-NOISE row (A2A only) — they share
    // the same vertical slot so the layout doesn't shift between
    // modes.  TAGS is two checkboxes "KEY" and "BPM" that gate
    // whether the corresponding tag is appended to the prompt.
    // INIT-NOISE is the σmax slider that controls how much the A2A
    // model regenerates from the input audio.  Always advance y by
    // one row (22 px) regardless of mode.
    {
        // TAGS (T2A only — hidden in A2A by setMode()).
        const int tagH = 22;
        const int tagLabelW = 50;
        const int tagToggleW = 70;
        tagsLabel           .setBounds (x, y, tagLabelW, tagH);
        includeKeyInPrompt  .setBounds (x + tagLabelW + 4, y, tagToggleW, tagH);
        includeBpmInPrompt  .setBounds (x + tagLabelW + 4 + tagToggleW + 8, y,
                                        tagToggleW, tagH);

        // INIT NOISE (A2A only — hidden in T2A by setMode()).  Same
        // y as the TAGS row, same height.
        const int noiseH = 22;
        initNoiseLabel.setBounds (x, y, 90, noiseH);
        initNoiseSlider.setBounds (x + 94, y, colW - 94 - 110, noiseH);
        initNoiseValueLabel.setBounds (x + colW - 106, y, 106, noiseH);
        y += tagH;
    }

    // Quick-pick preset chips (A2A only).  Three small buttons in a
    // compact row under the slider; clicking sets the slider to the
    // preset value.  Positioned at the right edge of the column so
    // they don't crowd the rest of the layout.  In T2A the row is
    // empty (controls are hidden by setMode()).
    {
        const int chipH   = 20;
        const int chipGap = 6;
        const int chipW   = 78;
        const int chipsTotalW = 3 * chipW + 2 * chipGap;
        int cx = x + colW - chipsTotalW;
        initNoisePresetSubtle  .setBounds (cx, y, chipW, chipH);  cx += chipW + chipGap;
        initNoisePresetBalanced.setBounds (cx, y, chipW, chipH);  cx += chipW + chipGap;
        initNoisePresetCreative.setBounds (cx, y, chipW, chipH);
        y += chipH + gap;
    }

    // "Currently selected input" indicator (always hidden — the
    // dedicated INPUT pane on the right shows the same info plus
    // size, duration, and mtime).
    a2aInputFileLabel.setBounds (x, y, colW, 18);
    // y += 18 + gap;   // (intentionally not advanced — the label is
                        //  always hidden in both modes)

    // T2A-only control rows.  Skipped entirely in A2A — KEY / BPM
    // / DURATION / SR are not exposed in A2A (the A2A model takes
    // a verbatim prompt and a fixed-length input, and outputs at
    // the host's expected rate).  SOLO IS shared between T2A and
    // A2A — see the solo block above for the rationale.  No
    // widgets to position in A2A, so y stays put and the Generate
    // button sits directly under the prompt/init-noise.
    if (currentMode == Mode::T2A)
    {
        // KEY + TEMPO + SAMPLE RATE — all on one row
        const int kRowY = y;
        keyLabel.setBounds (x, kRowY, 30, 22);
        keyRoot.setBounds  (x + 32, kRowY, 70, 22);
        keyMode.setBounds  (x + 106, kRowY, 70, 22);
        useProjectBpm.setBounds (x + 184, kRowY, 120, 22);
        // BPM slider ends well before the SR dropdown so the latter has
        // room for "44.1 kHz" / "48 kHz" without ellipsising.
        bpmSlider.setBounds     (x + 308, kRowY, colW - 308 - 160, 22);
        bpmValueLabel.setBounds (x + colW - 156, kRowY, 44, 22);
        sampleRateLabel.setBounds (x + colW - 108, kRowY, 24, 22);
        sampleRateBox.setBounds   (x + colW - 80,  kRowY, 80, 22);
        y += 22 + gap;

        // Duration row
        useCustom.setBounds (x, y, 140, 22);
        // S | B segmented control, 36px each so they're easy to click and
        // the single-letter labels don't get clipped.
        unitSecButton.setBounds (x + 144, y, 36, 22);
        unitBarButton.setBounds (x + 144 + 36, y, 36, 22);
        customSlider.setBounds (x + 144 + 36 + 36 + 6, y,
                                colW - (144 + 36 + 36 + 6) - 84, 22);
        customValueLabel.setBounds (x + colW - 80, y, 80, 22);
        y += 22 + gap + 4;
    }

    // GENERATE (full width, prominent)
    generateButton.setBounds (x, y, colW, 38);
    y += 38 + gap;

    // Waveform (takes remaining space minus transport + status row)
    const int transportH = 32;
    const int statusH    = 18;
    const int waveH = contentH - (y - contentY) - transportH - statusH - gap;
    waveform.setBounds (x, y, colW, std::max (70, waveH));
    y += std::max (70, waveH) + gap;

    // Transport row
    int trX = x;
    playButton.setBounds   (trX, y, 64, transportH - 4); trX += 68;
    stopButton.setBounds   (trX, y, 64, transportH - 4); trX += 68;
    loopButton.setBounds   (trX, y, 64, transportH - 4); trX += 68;
    saveWavButton.setBounds (trX, y, 84, transportH - 4); trX += 88;
    copyToClipboardButton.setBounds (trX, y, colW - (trX - x), transportH - 4);
    y += transportH;

    // Status row (toast messages)
    statusLabel.setBounds (x, y, colW, statusH);

    // Progress overlay covers the whole window
    progressOverlay->setBounds (0, 0, W, H);
}

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &processor.stateBroadcaster)
    {
        repaint();
        // If the render stopped on its own (file finished playing
        // through), clear our playingFile tracker so the next click
        // on the same file is treated as a new playback, not as the
        // second click of a toggle that already ended.
        if (playingFile != juce::File() && !processor.isRendering())
            playingFile = juce::File();
        // Update the per-row Play/Stop button labels in all three
        // browser components so the click-Play-to-Stop affordance
        // is always visible inline.
        if (t2aHistory)    t2aHistory->setPlayingFile    (playingFile);
        if (a2aOutputList) a2aOutputList->setPlayingFile (playingFile);
        if (a2aInputList)  a2aInputList->setPlayingFile  (playingFile);
    }
}

void PluginEditor::timerCallback()
{
    updateFromHost();
    backendStatusLabel.setText ("Backend: " + backendStateToString (processor.getBackend().getState()),
                                juce::dontSendNotification);
    auto s = processor.getBackend().getState();
    backendStatusLabel.setColour (juce::Label::textColourId,
        s == BackendState::Running ? lnf.success :
        s == BackendState::Busy    ? lnf.warning :
        s == BackendState::Error || s == BackendState::Crashed ? lnf.danger : lnf.textMuted);
    bpmValueLabel.setText (juce::String (static_cast<int> (bpmSlider.getValue())) + " BPM",
                            juce::dontSendNotification);
    // The duration value label is unit-aware (sec / bars) and is kept
    // in sync by updateDurationValueLabel(); the timer must NOT just
    // hardcode " sec" — that would clobber the BARS suffix.
    updateDurationValueLabel();

    // If loadFromSettings() ran before the model selector's list was
    // populated, the saved model id may not have been applicable yet.
    // Re-apply it now (cheap, idempotent — setSelectedId is a no-op
    // if it's already that value).
    static juce::String lastAppliedModel;
    juce::String wanted = processor.getSettings().getValue ("modelId", juce::String());
    if (wanted.isNotEmpty() && wanted != lastAppliedModel
        && wanted != modelSelector->getSelectedId())
    {
        modelSelector->setSelectedId (wanted);
        lastAppliedModel = wanted;
    }
    // Sync the waveform playhead to the processor's render-buffer position
    // so the cursor moves during playback (playback is driven by the
    // audio thread, not by the waveform's own transport).
    waveform.setExternalPosition (processor.getRenderPositionSec());

    // Auto-discover new WAVs in the output folder (every 1 s) so the user
    // always sees everything that's been generated — even if the polling
    // failed or the plugin was reopened after the fact.
    historyScanAccum += 0.25f;   // timer runs at 4 Hz
    if (historyScanAccum >= 1.0f)
    {
        historyScanAccum = 0.0f;
        processor.getHistory().rescanOutputFolder();

        // A2A: also refresh the INPUT list (user may have dropped a new
        // file into the input folder from Finder).
        if (currentMode == Mode::A2A && a2aInputList)
            a2aInputList->refresh();

        // Safety net: if the progress overlay is still up but a new WAV
        // appeared in the output folder AFTER the user's last click,
        // the generation is effectively done — dismiss the overlay and
        // load the file.  This covers the case where the polling thread
        // failed to deliver jobFinished (e.g. backend was restarted,
        // activeJob was lost, etc.).
        if (progressOverlay && progressOverlay->isVisible() && !activeJobId.isEmpty())
        {
            auto outDir = processor.getHistory().getOutputDir();
            if (outDir.isDirectory())
            {
                juce::File newest;
                juce::Time newestTime;
                for (juce::DirectoryEntry e : juce::RangedDirectoryIterator (
                         outDir, false, "*.wav", juce::File::findFiles))
                {
                    auto f = e.getFile();
                    auto t = f.getLastModificationTime();
                    if (t > lastClickTime && t > newestTime)
                    {
                        newest     = f;
                        newestTime = t;
                    }
                }
                if (newest.existsAsFile())
                {
                    // Generation completed — acknowledge in backend (this
                    // resets state from Busy→Running, fires jobFinished),
                    // dismiss overlay, and load the file.
                    DBG ("[timer] output-file safety net: dismissing overlay, newest=" + newest.getFileName());
                    processor.getBackend().acknowledgeCompletion (activeJobId, newest);
                    progressOverlay->end();
                    activeJobId = {};
                    updateGenerateButton();
                    waveform.loadFile (newest);
                    double sr = 0.0;
                    if (auto buf = AudioExporter::readWav (newest, &sr))
                        processor.setRenderBuffer (std::make_shared<juce::AudioBuffer<float>> (*buf), sr);
                }
            }
        }
    }
}

void PluginEditor::updateFromHost()
{
    if (useProjectBpm.getToggleState())
        bpmSlider.setValue (processor.getHostBpm(), juce::dontSendNotification);
}

void PluginEditor::updateGenerateButton()
{
    bool modelReady = processor.getModelManager().isModelReady (modelSelector->getSelectedId());
    auto  s         = processor.getBackend().getState();
    // SOLO can carry the prompt on its own ("solo <instrument>"), so
    // an empty prompt field is OK when the user has SOLO armed: the
    // backend gets a non-empty solo-only prompt from appendSoloTag().
    // The same three guards as appendSoloTag(): toggle on + instrument
    // picked + non-SFX model.
    bool  soloActive = soloToggle.getToggleState()
                     && soloInstrument.getSelectedId() > 0
                     && modelSelector
                     && ! modelSelector->getSelectedId().containsIgnoreCase ("sfx");
    bool  promptOk  = promptEditor.getText().trim().isNotEmpty() || soloActive;
    bool  a2aInputOk = (currentMode != Mode::A2A) || a2aSelectedInput.existsAsFile();

    // Generate is enabled whenever the model and prompt are usable, even
    // if the python backend is Stopped or still starting up.  submit()
    // in BackendClient handles all three cases: it kicks off the
    // launcher .app if the backend is Stopped, queues the job if it's
    // Starting, and POSTs immediately if it's already Running.
    bool backendOk = (s == BackendState::Stopped
                   || s == BackendState::Starting
                   || s == BackendState::Running);
    generateButton.setEnabled (modelReady && backendOk && promptOk && a2aInputOk);

    if (s == BackendState::Busy)
    {
        generateButton.setEnabled (false);
        generateButton.setButtonText (currentMode == Mode::A2A ? "REGENERATING..." : "GENERATING...");
    }
    else if (!modelReady)
    {
        generateButton.setButtonText ("MODEL NOT READY");
    }
    else if (currentMode == Mode::A2A && ! a2aInputOk)
    {
        generateButton.setButtonText ("PICK AN INPUT FILE");
    }
    else if (s == BackendState::Starting)
    {
        // Backend is starting up; clicking again will queue the job
        // and it will fire as soon as /health responds.
        generateButton.setButtonText ("BACKEND STARTING... (click to queue)");
    }
    else if (s == BackendState::Stopped)
    {
        generateButton.setButtonText (currentMode == Mode::A2A ? "REGENERATE" : "GENERATE");
    }
    else
    {
        generateButton.setButtonText (currentMode == Mode::A2A ? "REGENERATE" : "GENERATE");
    }
}

void PluginEditor::generateClicked()
{
    // DIAG: log every click + button state
    {
        auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library/Application Support")
                        .getChildFile ("DAWalka");
        auto logFile = home.getChildFile ("backend.log");
        logFile.appendText ("[click] generateClicked, mode="
            + juce::String (currentMode == Mode::A2A ? "A2A" : "T2A")
            + " enabled=" + juce::String (generateButton.isEnabled() ? "yes" : "no")
            + " state=" + backendStateToString (processor.getBackend().getState())
            + " modelReady=" + juce::String (
                processor.getModelManager().isModelReady (modelSelector->getSelectedId()) ? "yes" : "no")
            + " promptEmpty=" + juce::String (promptEditor.isEmpty() ? "yes" : "no")
            + (currentMode == Mode::A2A
                ? " a2aInput=" + a2aSelectedInput.getFullPathName()
                : juce::String())
            + "\n");
    }

    updateGenerateButton();
    if (!generateButton.isEnabled()) return;

    if (currentMode == Mode::A2A)
    {
        // ── A2A: regenerate the selected input file in the style of the prompt.
        if (! a2aSelectedInput.existsAsFile())
        {
            showStatus ("Pick an input file from the browser first.");
            return;
        }
        // Build the effective prompt first so SOLO can supply the
        // prompt on its own ("solo <instrument>") when the text field
        // is empty — same rule as the T2A branch and as the Generate
        // button's enable logic in updateGenerateButton().
        const auto effectivePrompt = appendSoloTag (promptEditor.getText().trim());
        if (effectivePrompt.isEmpty())
        {
            showStatus ("Type a prompt or enable SOLO with an instrument.");
            return;
        }

        A2aRequest req;
        req.prompt         = effectivePrompt;
        req.initAudio      = a2aSelectedInput;
        req.initNoiseLevel = static_cast<float> (initNoiseSlider.getValue());
        req.modelId        = modelSelector->getSelectedId();
        req.sampleRate     = sampleRateBox.getSelectedId() == 2 ? 48000 : 44100;
        req.outputDir      = a2aOutputDir;
        req.durationSec    = measureAudioSeconds (a2aSelectedInput);

        if (req.durationSec <= 0.0)
        {
            showStatus ("Input file is silent or unreadable.");
            return;
        }

        // Build a meaningful output filename: "<input-name>__a2a__<HH-MM-SS>.wav"
        // so the user can see at a glance which output came from which input.
        // Timestamp suffix avoids collisions when re-running on the same file.
        {
            auto inBase = a2aSelectedInput.getFileNameWithoutExtension();
            // Strip any non-alphanumeric chars from the base (keep spaces
            // replaced with underscores — safer filenames on case-sensitive
            // file systems and more readable in the Finder list).
            inBase = inBase.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
            if (inBase.isEmpty()) inBase = "a2a";
            auto t = juce::Time::getCurrentTime();
            req.suggestedFilename =
                inBase + "__a2a__" + t.formatted ("%H-%M-%S") + ".wav";
        }

        activeJobId = processor.getBackend().submitA2a (req);
        processor.getModelManager().touchModel (req.modelId);
        lastClickTime = juce::Time::getCurrentTime();

        const auto s = processor.getBackend().getState();
        if (s == BackendState::Starting)
        {
            progressOverlay->begin ("Starting backend...");
            progressOverlay->setProgress (0.0);
            progressOverlay->setEstimatedSecondsLeft (-1.0);
        }
        else if (s == BackendState::Running || s == BackendState::Busy)
        {
            progressOverlay->begin ("Regenerating (A2A)...");
            progressOverlay->setProgress (0.0);
            progressOverlay->setEstimatedSecondsLeft (-1.0);
        }
        return;
    }

    // ── T2A (original path) ───────────────────────────────────────────
    GenerationRequest req;
    // Build the visible prompt + optional "solo <instrument>" suffix.
    // The suffix is the ONLY thing we add here — BPM / key / "seamless
    // loop" / "professional mix" are all appended server-side by
    // PromptBuilder.build() so the visible prompt stays clean for the
    // user.  We add "solo" inline because it depends on UI state
    // (toggle + dropdown), not on the model.
    req.prompt        = appendSoloTag (promptEditor.getText());
    req.bpm           = static_cast<int> (bpmSlider.getValue());
    req.key.root      = keyRoot.getSelectedId() - 1;
    req.key.mode      = keyMode.getSelectedId() == 2 ? ScaleMode::Minor : ScaleMode::Major;
    req.modelId       = modelSelector->getSelectedId();
    req.durationSec   = useCustom.getToggleState() ? getEffectiveDurationSec() :
                         (selectionDurationAvailable ? selectionDurationSec : kDefaultDurationSeconds);
    req.sampleRate    = sampleRateBox.getSelectedId() == 2 ? 48000 : 44100;
    req.seamlessLoop  = true;
    // KEY/BPM in-prompt flags — sent to the Python PromptBuilder on
    // the backend.  Defaults are true (preserves historical behaviour).
    // Duration control is unaffected: the BPM value still flows
    // through to req.bpm regardless, so the duration slider and
    // host-tempo sync keep working when the prompt tag is suppressed.
    req.includeKey    = includeKeyInPrompt.getToggleState();
    req.includeBpm    = includeBpmInPrompt.getToggleState();

    auto effective = PromptBuilder::build (req.prompt, req.bpm, req.key, req.seamlessLoop, req.modelId,
                                           /*extraTags*/ {},
                                           req.includeKey, req.includeBpm);
    juce::ignoreUnused (effective);

    activeJobId = processor.getBackend().submit (req);
    processor.getModelManager().touchModel (req.modelId);
    lastClickTime = juce::Time::getCurrentTime();

    // submit() may have started the backend (Stopped -> Starting). Show
    // an overlay that reflects whichever phase we're in.  When the
    // backend finally gets to Running, the run() thread in BackendClient
    // will POST /generate, the server streams progress, and we'll get
    // jobProgress callbacks that update the overlay.
    const auto s = processor.getBackend().getState();
    if (s == BackendState::Starting)
    {
        progressOverlay->begin ("Starting backend...");
        progressOverlay->setProgress (0.0);
        progressOverlay->setEstimatedSecondsLeft (-1.0);
    }
    else if (s == BackendState::Running || s == BackendState::Busy)
    {
        progressOverlay->begin ("Generating...");
        progressOverlay->setProgress (0.0);
        progressOverlay->setEstimatedSecondsLeft (-1.0);
    }
    else
    {
        // Stopped but submit() didn't start anything (e.g. errored out).
        // The error state will be reflected via backendStateChanged().
    }
}

void PluginEditor::cancelClicked()
{
    if (activeJobId.isNotEmpty())
        processor.getBackend().cancel (activeJobId);
    progressOverlay->end();
}

void PluginEditor::playClicked()
{
    // First, make sure the processor has a render buffer loaded.
    // (jobFinished does this automatically, but the user might click
    // Play after loading a file from history, where jobFinished is
    // not called.  Safe to call repeatedly — it re-decodes the WAV.)
    ensureProcessorRenderBuffer();
    if (processor.getRenderLengthSec() > 0.0)
        processor.playRender();
    // (no waveform.play() — the transport has no audio device wired up,
    //  and starting it has been known to cause AU host crashes when the
    //  render ends.  All sound comes from processBlock.)
}

void PluginEditor::stopClicked()
{
    processor.stopRender();
    waveform.stop();
    waveform.setExternalPosition (0.0);
}

void PluginEditor::ensureProcessorRenderBuffer()
{
    auto f = waveform.getFile();
    if (!f.existsAsFile()) return;
    // readWav() reads the entire file into memory.  On a malformed or
    // huge AIFF that can throw / crash — guard it so the user sees
    // "failed to load" instead of taking down the host.
    try
    {
        double sr = 0.0;
        auto buf = AudioExporter::readWav (f, &sr);
        if (!buf) return;
        processor.setRenderBuffer (std::make_shared<juce::AudioBuffer<float>> (*buf), sr);
    }
    catch (...)
    {
        showStatus ("Failed to load " + f.getFileName() + " (unsupported or corrupt)");
    }
}

void PluginEditor::loopToggled()
{
    waveform.setLooping (loopButton.getToggleState());
    processor.setRenderLoop (loopButton.getToggleState());
}

void PluginEditor::copyToClipboardClicked()
{
    auto src = waveform.getFile();
    if (!src.existsAsFile())
    {
        showStatus ("No file to copy.");
        return;
    }
    copyFileToClipboard (src);
    showStatus ("Copied. Press \xE2\x8C\x98+V in Logic to insert at the playhead.");
}

void PluginEditor::saveWavClicked()
{
    auto src = waveform.getFile();
    if (!src.existsAsFile()) return;

    auto target = src.withFileExtension ("44k.wav");
    if (target.existsAsFile()) target.deleteFile();

    double sr = 0.0;
    auto buf = AudioExporter::readWav (src, &sr);
    if (!buf) return;

    // Also feed the buffer to the processor for in-plugin playback.
    processor.setRenderBuffer (std::make_shared<juce::AudioBuffer<float>> (*buf), sr);

    if (AudioExporter::writeWav (target, *buf, sr))
    {
        auto* fc = new juce::FileChooser ("Save WAV", target, "*.wav");
        fc->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting,
            [fc, target] (const juce::FileChooser& chooser) mutable
            {
                auto dest = chooser.getResult();
                if (dest != juce::File() && dest != target) target.copyFileTo (dest);
                delete fc;
            });
    }
}

void PluginEditor::exportClicked() {}

// ─── T2A history callbacks ─────────────────────────────────────────────
void PluginEditor::onT2aHistorySelect (const HistoryEntry& e)
{
    if (!e.audioFile.existsAsFile()) return;
    // Switching clips should stop any in-flight playback so the user
    // doesn't hear the tail of the old clip while looking at the new
    // waveform.
    processor.stopRender();
    waveform.stop();
    waveform.setExternalPosition (0.0);
    waveform.loadFile (e.audioFile);
    ensureProcessorRenderBuffer();
    showStatus ("Loaded " + e.audioFile.getFileName() + " — press Play to listen.");
}

void PluginEditor::onT2aHistoryPlay (const HistoryEntry& e)
{
    togglePlayFile (e.audioFile);
}

// ─── togglePlayFile ────────────────────────────────────────────────────
//
// Single Play/stop helper shared by T2A history, A2A input browser, and
// A2A output history.  A second click on the same file (i.e. the file
// that's currently playing through the plugin) stops playback instead
// of restarting from the beginning — the standard "click again to
// stop" affordance the user expects from a media player.
//
// State tracking: we keep `playingFile` updated whenever we start
// playback, and clear it from changeListenerCallback() once the
// render ends (either because we stopped it explicitly, or because
// the file finished on its own).  Comparing incoming files to
// `playingFile` lets us decide between "start" and "stop".
void PluginEditor::togglePlayFile (const juce::File& f)
{
    if (!f.existsAsFile()) return;

    if (playingFile == f && processor.isRendering())
    {
        // Same file, currently playing — second click stops.
        processor.stopRender();
        waveform.stop();
        return;
    }

    // New file (or a file that finished naturally on a previous
    // play).  Rewind, load, start.  We deliberately stop+load+start
    // even if the user clicked a different Play button while another
    // file was playing — the user expects Play to mean "play this
    // one now."
    processor.stopRender();
    waveform.stop();
    waveform.setExternalPosition (0.0);
    waveform.loadFile (f);
    ensureProcessorRenderBuffer();
    if (processor.getRenderLengthSec() > 0.0)
    {
        processor.playRender();
        playingFile = f;
    }
    else
    {
        // File failed to load as a render buffer — don't claim
        // ownership of the play state.
        playingFile = juce::File();
    }

    // playRender() above fired stateBroadcaster synchronously, but at
    // that point playingFile was still the OLD value.  Re-apply
    // labels now that the new playingFile is in place.
    if (t2aHistory)    t2aHistory->setPlayingFile    (playingFile);
    if (a2aOutputList) a2aOutputList->setPlayingFile (playingFile);
    if (a2aInputList)  a2aInputList->setPlayingFile  (playingFile);
}

void PluginEditor::onT2aHistoryDelete (const HistoryEntry& e)
{
    processor.getHistory().remove (e.id);
    showStatus ("Deleted " + e.audioFile.getFileName() + ".");
}

void PluginEditor::onT2aHistoryClear()
{
    // Only delete files in the CURRENT output folder.  The A2A
    // outputs (in their own folder) stay untouched — setOutputDir
    // no longer drops them from the cache, so we need this scoped
    // variant to avoid nuking the wrong folder.
    processor.getHistory().clearCurrentOutputDir();
    showStatus ("T2A history cleared (files removed from disk).");
}

void PluginEditor::showT2aHistoryMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Set T2A folder...");
    m.addItem (2, "Show in Finder");

    const bool busy = (processor.getBackend().getState() == BackendState::Busy)
                   || (processor.getBackend().getState() == BackendState::Starting);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&t2aHistory->getMenuButton()),
        [this, busy] (int result)
        {
            if (result == 1 && ! busy)
            {
                changeT2aOutputFolder();
            }
            else if (result == 2)
            {
                processor.getHistory().getOutputDir().revealToUser();
            }
        });
}

void PluginEditor::changeT2aOutputFolder()
{
    auto current = processor.getHistory().getOutputDir();
    auto chooser = std::make_shared<juce::FileChooser> (
        "Choose T2A folder", current, "", true);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File()) return;
            applyT2aOutputFolderChange (result);
        });
}

void PluginEditor::applyT2aOutputFolderChange (const juce::File& newDir)
{
    auto& backend = processor.getBackend();
    auto& history = processor.getHistory();
    auto oldDir = history.getOutputDir();

    if (newDir == oldDir) return;

    showStatus ("Switching T2A folder…");

    backend.stop();
    backend.setOutputDir (newDir);
    history.setOutputDir (newDir);
    t2aDir = newDir;

    // A2A's INPUT browser follows the T2A folder ONLY when the user
    // hasn't explicitly set a custom A2A input folder (a2aInputDir
    // empty = "follow T2A").  If the user set their own folder, leave
    // it alone.
    if (a2aInputList && a2aInputDir == juce::File())
        a2aInputList->setDirectory (newDir);

    processor.getSettings().setValue ("outputDir", newDir.getFullPathName());
    processor.getSettings().saveIfNeeded();

    showStatus ("T2A folder: " + newDir.getFileName()
                + " (next generation will use it; A2A input follows)", 4000);
}

// ─── Random prompt buttons ─────────────────────────────────────────────
//
// Two buttons: RANDOM MUSIC and RANDOM SFX.  Both share the same
// handler shape — pick from the appropriate PromptLibrary pool and
// write the result into promptEditor.  In A2A the body is wrapped
// in a "transform this into…" action phrase by the library.
//
// The model selector is no longer consulted here — the user picks
// the CATEGORY (Music / SFX) by clicking the corresponding button,
// which is more direct than a button that infers category from
// model id.  The model selector still governs which model runs the
// generation.
void PluginEditor::onRandomMusicClicked()
{
    const bool isA2A = (currentMode == Mode::A2A);
    const auto p = PromptLibrary::pickRandomMusic (isA2A);
    if (p.isEmpty())
    {
        showStatus ("Random library is empty — please reinstall.", 3000);
        return;
    }
    promptEditor.setText (p, juce::dontSendNotification);
    saveToSettings();
    updateGenerateButton();
    showStatus ("Random MUSIC prompt generated"
                + juce::String (isA2A ? " (A2A)" : " (T2A)"), 1800);
}

void PluginEditor::onRandomSfxClicked()
{
    const bool isA2A = (currentMode == Mode::A2A);
    const auto p = PromptLibrary::pickRandomSfx (isA2A);
    if (p.isEmpty())
    {
        showStatus ("Random library is empty — please reinstall.", 3000);
        return;
    }
    promptEditor.setText (p, juce::dontSendNotification);
    saveToSettings();
    updateGenerateButton();
    showStatus ("Random SFX prompt generated"
                + juce::String (isA2A ? " (A2A)" : " (T2A)"), 1800);
}

// ─── Solo instrument ────────────────────────────────────────────────────
//
// Two states to manage:
//   1. any mode + non-FX model → controls are live, dropdown is
//      gated by the toggle (off → dropdown grayed, on → dropdown live).
//   2. any mode + FX model     → controls are visible but disabled
//      (FX has no "solo" concept, the dropdown would be misleading).
//
// SOLO is shared between T2A and A2A — appendSoloTag() handles both
// generation paths, and the user can solo an instrument when
// transforming input audio as well as when generating from scratch.
//
// updateSoloEnabled() reconciles the enabled state with the current
// model — called whenever the model changes.  onSoloToggleChanged()
// only deals with the toggle/dropdown dependency on user clicks.
void PluginEditor::updateSoloEnabled()
{
    if (!modelSelector) return;

    const bool fxModel =
        modelSelector->getSelectedId().containsIgnoreCase ("sfx");

    // Toggle is live in BOTH T2A and A2A, as long as the model is
    // not FX.  The controls' vertical slot in the layout is shared
    // between modes, so they're always visible.
    const bool allowSolo = !fxModel;

    soloToggle.setEnabled (allowSolo);
    if (!allowSolo)
    {
        // If we just got disabled (FX model picked), force the
        // dropdown off too — otherwise the next time the user
        // switches back to a music model the dropdown would
        // re-enable with a stale selection.
        soloInstrument.setEnabled (false);
    }
    else
    {
        // The dropdown is gated by the toggle — only live when
        // the user has actually asked for solo mode.
        soloInstrument.setEnabled (soloToggle.getToggleState());
    }
}

void PluginEditor::onSoloToggleChanged()
{
    // Re-evaluate the dropdown's enabled state from the toggle.
    // SOLO is allowed in both T2A and A2A; only the FX model
    // disables it.
    const bool allowSolo = ! modelSelector->getSelectedId().containsIgnoreCase ("sfx");

    soloInstrument.setEnabled (allowSolo && soloToggle.getToggleState());

    // Friendly status hint so the user knows what just happened.
    if (soloToggle.getToggleState() && soloInstrument.getSelectedId() == 0)
        showStatus ("Solo ON — pick an instrument from the dropdown.", 2200);
    else if (soloToggle.getToggleState())
        showStatus ("Solo ON — appending 'solo " + soloTag (soloInstrument.getSelectedId())
                    + "' to the prompt.", 1800);
    else
        showStatus ("Solo OFF", 1200);

    // SOLO affects whether an empty prompt is allowed (see
    // updateGenerateButton) — re-evaluate Generate button state.
    updateGenerateButton();
}

juce::String PluginEditor::appendSoloTag (const juce::String& prompt) const
{
    // All three guards must pass for the solo suffix to be appended:
    //   1. toggle ON (user wants solo mode)
    //   2. an instrument is picked from the dropdown
    //   3. the model supports solo (not FX / SFX)
    // Used by both T2A and A2A — both modes honour the same rules.
    if (! soloToggle.getToggleState())             return prompt;
    if (soloInstrument.getSelectedId() <= 0)       return prompt;
    if (modelSelector
        && modelSelector->getSelectedId().containsIgnoreCase ("sfx"))
        return prompt;

    const auto solo = soloTag (soloInstrument.getSelectedId());
    if (solo.isEmpty()) return prompt;

    const auto trimmed = prompt.trim();
    if (trimmed.isEmpty())         return solo;
    if (trimmed.containsIgnoreCase (solo)) return prompt;  // already there
    return trimmed + ", " + solo;
}

// ─── Tags-row enable state ─────────────────────────────────────────────
//
// When the user turns OFF a tag in the TAGS row, the corresponding
// control row is grayed out — otherwise the user might keep tweaking
// the key or BPM without realising the value isn't reaching the
// prompt.  The disabled state is purely cosmetic; the controls'
// internal state (e.g. selected key, current BPM) is preserved so
// re-enabling the tag restores the previous setting.
//
// Only the visibility-flipping controls (key/bpm groups) are
// affected — the tag toggles themselves stay live so the user can
// re-enable them.  The TAGS row, the SAMPLERATE dropdown, the
// DURATION row, and the SOLO row are independent.
//
// T2A-only: in A2A the whole KEY/BPM cluster is hidden by setMode(),
// so this method is a no-op there.  We still early-out so a stale
// include*InPrompt value from the A2A side doesn't accidentally
// re-enable controls that aren't visible anyway.
void PluginEditor::updateTagsEnabled()
{
    if (currentMode != Mode::T2A)
    {
        // Defensive: in A2A we want the key/bpm controls left in
        // whatever enabled state — but the user can't see them
        // anyway.  Skip rather than setEnabled(false) so we don't
        // fight setMode() (which itself doesn't touch these).
        return;
    }

    const bool allowKey = includeKeyInPrompt.getToggleState();
    const bool allowBpm = includeBpmInPrompt.getToggleState();

    keyLabel      .setEnabled (allowKey);
    keyRoot       .setEnabled (allowKey);
    keyMode       .setEnabled (allowKey);

    bpmLabel      .setEnabled (allowBpm);
    useProjectBpm .setEnabled (allowBpm);
    bpmSlider     .setEnabled (allowBpm);
    bpmValueLabel .setEnabled (allowBpm);
    autoBpmButton .setEnabled (allowBpm);

    // The TAGS toggles themselves stay live — that's how the user
    // re-enables the controls.  Sample rate is independent (it's
    // a downstream audio-routing concern, not a prompt tag) and
    // stays live too.
}

// ─── A2A callbacks ─────────────────────────────────────────────────────
void PluginEditor::onA2aInputSelect (const juce::File& f)
{
    a2aSelectedInput = f;
    updateA2aInputFileLabel();
    updateGenerateButton();
    saveToSettings();

    // Auto-load the picked file into the preview waveform so the user
    // can hear it before regenerating.  We deliberately do this only
    // when the selection actually changes (not on every repaint), and
    // we don't auto-play — just stage the audio and let them press
    // Play (or REGENERATE).
    if (f.existsAsFile() && waveform.getFile() != f)
    {
        processor.stopRender();
        waveform.stop();
        waveform.setExternalPosition (0.0);
        waveform.loadFile (f);
        ensureProcessorRenderBuffer();
        showStatus ("Loaded " + f.getFileName()
                    + " (input preview) — press Play to listen.");
    }
}

void PluginEditor::onA2aInputPlay (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    togglePlayFile (f);
    if (playingFile == f && processor.isRendering())
        showStatus ("Playing " + f.getFileName() + " (input preview)");
}

void PluginEditor::onA2aInputDelete (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    if (a2aSelectedInput == f) a2aSelectedInput = juce::File();
    f.deleteFile();
    if (a2aInputList) a2aInputList->refresh();
    updateA2aInputFileLabel();
    updateGenerateButton();
}

void PluginEditor::onA2aInputMenu()
{
    // The top browser is labelled "T2A folder" — keep all menu text
    // consistent with that label so the user sees the same term in
    // the label, the menu, the file dialog, and the status bar.
    //   1 = Set T2A folder…  (override the T2A folder used for input)
    //   2 = Use T2A folder   (reset to follow the shared T2A folder)
    //   3 = Reveal in Finder
    //   4 = Refresh
    juce::PopupMenu m;
    m.addItem (1, "Set T2A folder...");
    const bool followingT2a = (a2aInputDir == juce::File());
    m.addItem (2, "Use T2A folder", false, followingT2a);
    m.addSeparator();
    m.addItem (3, "Reveal in Finder");
    m.addItem (4, "Refresh");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&a2aInputList->getMenuButton()),
        [this] (int result)
        {
            if      (result == 1) changeA2aInputFolder();
            else if (result == 2) applyA2aInputFolderChange (juce::File());
            else if (result == 3) getEffectiveA2aInputDir().revealToUser();
            else if (result == 4) { if (a2aInputList) a2aInputList->refresh(); }
        });
}

void PluginEditor::onA2aInputFilesDropped (const juce::Array<juce::File>& files)
{
    // User dragged .wav files from Finder into the INPUT panel.  Copy
    // them into the EFFECTIVE A2A input folder (the T2A folder by
    // default, or a custom folder if the user picked one) — don't move,
    // keep the user's originals safe.  Then refresh + auto-select
    // the first new file.
    if (files.isEmpty()) return;
    auto targetDir = getEffectiveA2aInputDir();
    if (! targetDir.isDirectory())
        targetDir.createDirectory();

    int copied = 0;
    int skipped = 0;
    juce::File firstNew;
    for (const auto& src : files)
    {
        if (! src.existsAsFile()) { ++skipped; continue; }
        if (! isAudioFile (src)) { ++skipped; continue; }

        // If the file is already in the target folder, just use it as-is.
        juce::File dst = targetDir.getChildFile (src.getFileName());
        if (src == dst)
        {
            if (firstNew == juce::File()) firstNew = dst;
            ++copied;
            continue;
        }

        // Avoid clobbering an existing file — append a counter until we
        // find a free name.
        if (dst.exists())
        {
            auto stem = src.getFileNameWithoutExtension();
            auto ext  = src.getFileExtension();
            int i = 2;
            while (dst.exists())
            {
                dst = targetDir.getChildFile (stem + "_" + juce::String (i) + ext);
                ++i;
                if (i > 999) break;
            }
        }

        if (src.copyFileTo (dst))
        {
            // Force the destination's mtime to "now" — JUCE's
            // copyFileTo() uses copyfile() which preserves the
            // source's mtime, so a file from 2015 dropped today
            // would otherwise sort to the bottom of the list (the
            // INPUT panel sorts newest-first by mtime).
            dst.setLastModificationTime (juce::Time::getCurrentTime());
            if (firstNew == juce::File()) firstNew = dst;
            ++copied;
        }
        else
        {
            ++skipped;
        }
    }

    if (a2aInputList) a2aInputList->refresh();

    if (firstNew != juce::File())
        onA2aInputSelect (firstNew);

    juce::String msg = "Added " + juce::String (copied) + " file"
                     + (copied == 1 ? "" : "s") + " to "
                     + (a2aInputDir == juce::File() ? "T2A folder" : "A2A input folder");
    if (skipped > 0)
        msg += " (" + juce::String (skipped) + " skipped)";
    showStatus (msg, 4000);
}

// (changeA2aInputFolder / applyA2aInputFolderChange were removed in
//  the folder-restructuring refactor — A2A's input is the SHARED T2A
//  folder, so the user changes it via the T2A menu, not a separate
//  A2A input menu item.)

void PluginEditor::onA2aOutputSelect (const HistoryEntry& e)
{
    if (!e.audioFile.existsAsFile()) return;
    processor.stopRender();
    waveform.stop();
    waveform.setExternalPosition (0.0);
    waveform.loadFile (e.audioFile);
    ensureProcessorRenderBuffer();
    showStatus ("Loaded " + e.audioFile.getFileName() + " — press Play to listen.");
}

void PluginEditor::onA2aOutputPlay (const HistoryEntry& e)
{
    togglePlayFile (e.audioFile);
}

void PluginEditor::onA2aOutputDelete (const HistoryEntry& e)
{
    processor.getHistory().remove (e.id);
    showStatus ("Deleted " + e.audioFile.getFileName() + ".");
}

void PluginEditor::onA2aOutputClear()
{
    // Same scoped clear as T2A — only delete files in the CURRENT
    // output folder (A2A's at this point), leave the T2A folder alone.
    processor.getHistory().clearCurrentOutputDir();
    showStatus ("A2A output cleared (files removed from disk).");
}

void PluginEditor::showA2aOutputMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Set A2A folder...");
    m.addItem (2, "Reveal A2A folder in Finder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&a2aOutputList->getMenuButton()),
        [this] (int result)
        {
            if (result == 1)      changeA2aOutputFolder();
            else if (result == 2) a2aOutputDir.revealToUser();
        });
}

void PluginEditor::changeA2aOutputFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Choose A2A folder", a2aOutputDir, "", true);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File()) return;
            applyA2aOutputFolderChange (result);
        });
}

void PluginEditor::applyA2aOutputFolderChange (const juce::File& newDir)
{
    a2aOutputDir = newDir;
    if (! a2aOutputDir.isDirectory())
        a2aOutputDir.createDirectory();
    // Swap the HistoryComponent's output dir to the new folder.
    processor.getHistory().setOutputDir (a2aOutputDir);
    saveToSettings();
    showStatus ("A2A output folder: " + a2aOutputDir.getFileName(), 4000);
}

juce::File PluginEditor::getEffectiveA2aInputDir() const
{
    // Empty a2aInputDir means "follow the T2A folder" — handy because
    // a freshly generated T2A file is then immediately available as
    // an A2A input.  An explicit value is the user's custom choice
    // and stays put even if the T2A folder is later changed.
    //
    // We use the cached `t2aDir` instead of
    // processor.getHistory().getOutputDir() because the history
    // swaps its output dir to the A2A folder when entering A2A
    // mode — that would make this function (and the file-drop
    // handler that uses it) copy dropped files into the A2A
    // folder even though the INPUT panel is labelled "T2A folder".
    return a2aInputDir != juce::File()
         ? a2aInputDir
         : t2aDir;
}

void PluginEditor::refreshA2aInputList()
{
    if (a2aInputList)
        a2aInputList->setDirectory (getEffectiveA2aInputDir());
    updateA2aInputFileLabel();
}

void PluginEditor::changeA2aInputFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Choose T2A folder", getEffectiveA2aInputDir(), "", true);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File()) return;
            applyA2aInputFolderChange (result);
        });
}

void PluginEditor::applyA2aInputFolderChange (const juce::File& newDir)
{
    if (newDir == juce::File())
    {
        // Reset to "follow the T2A folder".
        a2aInputDir = juce::File();
        saveToSettings();
        refreshA2aInputList();
        showStatus ("A2A input folder: follows T2A folder", 4000);
        return;
    }

    a2aInputDir = newDir;
    if (! a2aInputDir.isDirectory())
        a2aInputDir.createDirectory();
    saveToSettings();
    refreshA2aInputList();
    showStatus ("A2A input folder: " + a2aInputDir.getFileName(), 4000);
}

void PluginEditor::showStatus (const juce::String& msg, int ms)
{
    statusLabel.setText (msg, juce::dontSendNotification);
    juce::Component::SafePointer<PluginEditor> safe (this);
    juce::Timer::callAfterDelay (ms, [safe]
    {
        if (safe) safe->statusLabel.setText ({}, juce::dontSendNotification);
    });
}

void PluginEditor::copyFileToClipboard (const juce::File& f)
{
    if (!f.existsAsFile()) return;
    // Put the file on the pasteboard as a *file URL* (NSPasteboardTypeFileURL),
    // not as plain text — Logic Pro recognizes this and pastes it as a
    // new audio region at the playhead.
    dawalka::copyFileToPasteboard (f.getFullPathName());
}

void PluginEditor::backendStateChanged (BackendState s)
{
    updateGenerateButton();
    repaint();

    // If we were showing "Starting backend..." and the backend is now
    // Running, the run() thread in BackendClient will POST /generate
    // for our queued job.  Flip the overlay to the right label for the
    // current mode (A2A says "Regenerating", T2A says "Generating").
    if (activeJobId.isNotEmpty()
        && (s == BackendState::Running || s == BackendState::Busy))
    {
        progressOverlay->begin (currentMode == Mode::A2A
                                ? "Regenerating (A2A)..."
                                : "Generating...");
        progressOverlay->setProgress (0.0);
        progressOverlay->setEstimatedSecondsLeft (-1.0);
    }
}

void PluginEditor::jobProgress (const Job& job)
{
    if (job.id == activeJobId)
    {
        progressOverlay->setProgress (job.progress.load());
    }
}

void PluginEditor::jobFinished (const Job& job)
{
    progressOverlay->end();
    if (job.result.ok)
    {
        if (job.result.audioFile.existsAsFile())
        {
            waveform.loadFile (job.result.audioFile);
            // Decode the WAV once and hand the buffer to the processor
            // so the in-plugin Play button can render it through the
            // host (Generator-AU style).
            double sr = 0.0;
            if (auto buf = AudioExporter::readWav (job.result.audioFile, &sr))
                processor.setRenderBuffer (std::make_shared<juce::AudioBuffer<float>> (*buf), sr);

            // Append to history so the user can replay/insert from the
            // panel.  We add EVERY successful job, even if it isn't the
            // "current" one — the user may have queued several.
            //
            // The canonical id of an entry is the WAV file's stem (the
            // backend-assigned UUID), NOT the local C++ job id — the
            // rescan path uses the file stem for dedup.  If we used
            // job.id here, the safety net / rescan would later see a
            // different id for the same file and add a duplicate entry.
            HistoryEntry e;
            e.id          = job.result.audioFile.getFileNameWithoutExtension();
            e.timestamp   = juce::Time::getCurrentTime();
            e.modelId     = job.request.modelId;
            e.prompt      = job.request.prompt;
            e.bpm         = job.request.bpm;
            e.key         = job.request.key;
            e.durationSec = job.result.durationSec > 0.0 ? job.result.durationSec
                                                         : job.request.durationSec;
            e.sampleRate  = job.request.sampleRate;
            e.audioFile   = job.result.audioFile;
            e.seed        = job.result.seed;
            // A2A jobs carry their metadata on a2aRequest.
            if (job.kind == "a2a")
            {
                e.kind           = "a2a";
                e.sourceFile     = job.a2aRequest.initAudio.getFullPathName();
                e.initNoiseLevel = job.a2aRequest.initNoiseLevel;
            }
            processor.getHistory().add (e);
        }
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
            "DAWalka - Generation Error",
            job.result.error.isNotEmpty() ? job.result.error : "Unknown error");
    }
    if (job.id == activeJobId)
        activeJobId = {};
    updateGenerateButton();
}

void PluginEditor::modelStateChanged (const ModelState&)
{
    updateGenerateButton();
}

} // namespace dawalka
