#include "HistoryComponent.h"

namespace dawalka {

// ── Single history row: background, text, and per-row buttons ────────────
class HistoryRow : public juce::Component
{
public:
    HistoryRow (int rowIdx, const HistoryEntry& e,
                std::function<void()> onSelect,
                std::function<void()> onPlay,
                std::function<void()> onDelete,
                std::function<void()> onPrompt)
        : index (rowIdx), entry (e),
          cbSelect (std::move (onSelect)),
          cbPlay   (std::move (onPlay)),
          cbDelete (std::move (onDelete)),
          cbPrompt (std::move (onPrompt))
    {
        addAndMakeVisible (playButton);
        playButton.setButtonText ("Play");
        playButton.setTooltip ("Play this clip in the plugin");
        playButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (40, 44, 56));
        playButton.setColour (juce::TextButton::textColourOffId, juce::Colour::fromRGB (220, 224, 240));
        playButton.onClick = [this] { if (cbPlay) cbPlay(); };

        // Prompt-copy button — only meaningful for T2A entries (A2A
        // entries have a meaningful prompt too, but the user's prompt
        // editor is shared across modes, so we still expose it for
        // both kinds — the prompt text is what gets copied).
        addAndMakeVisible (promptButton);
        promptButton.setButtonText ("Prompt");
        promptButton.setTooltip ("Copy this prompt into the prompt field");
        promptButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (40, 44, 56));
        promptButton.setColour (juce::TextButton::textColourOffId, juce::Colour::fromRGB (220, 224, 240));
        promptButton.onClick = [this] { if (cbPrompt) cbPrompt(); };

        addAndMakeVisible (deleteButton);
        deleteButton.setButtonText ("[x]");
        deleteButton.setTooltip ("Delete this clip (removes the WAV file too)");
        deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (40, 44, 56));
        deleteButton.setColour (juce::TextButton::textColourOffId, juce::Colour::fromRGB (180, 184, 200));
        deleteButton.onClick = [this] { if (cbDelete) cbDelete(); };
    }

    void setSelected (bool s) { selected = s; repaint(); }
    void setIndex (int i) { index = i; }

    // Flip the per-row Play button text between "Play" and "Stop"
    // for the click-Play-to-Stop affordance.  Called by
    // HistoryComponent::setPlayingFile().
    void setIsPlaying (bool playing)
    {
        if (playing)
        {
            playButton.setButtonText ("Stop");
            playButton.setTooltip   ("Stop playback");
        }
        else
        {
            playButton.setButtonText ("Play");
            playButton.setTooltip   ("Play this clip in the plugin");
        }
    }

    // Drag-source description: the absolute path of the generated
    // audio file.  Not a virtual Component method — just a
    // conventional name; the actual drag is started from
    // mouseDrag() below using performExternalDragDropOfFiles.
    juce::var getDragSourceDescription() const
    {
        return juce::var (entry.audioFile.getFullPathName());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // mouseDown already selected the row.  If the user moves the
        // cursor past the drag threshold, kick off a system-level
        // file drag.  JUCE's static
        // performExternalDragDropOfFiles() writes file URLs to the
        // macOS pasteboard and calls NSWindow's dragImage:, which
        // is exactly what Logic listens for on the timeline.  The
        // function blocks until the user drops or cancels, so no
        // further mouseDrag/mouseUp will fire for this gesture —
        // that's fine, the row is already selected.
        if (e.getDistanceFromDragStart() < 4) return;
        if (! entry.audioFile.existsAsFile()) return;
        juce::StringArray files;
        files.add (entry.audioFile.getFullPathName());
        // canMoveFiles = false: the receiver must copy, not move/delete.
        juce::DragAndDropContainer::performExternalDragDropOfFiles (
            files, /*canMoveFiles*/ false, this);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        const bool isA2A = (entry.kind == "a2a");
        const auto accent = isA2A
            ? juce::Colour::fromRGB (88, 198, 165)   // green for A2A
            : juce::Colour::fromRGB (255, 122, 89); // orange for T2A
        auto bg = selected
            ? accent.withAlpha (0.18f)               // tinted bg matches the accent
            : (index % 2 == 0
                ? juce::Colour::fromRGB (26, 29, 38)
                : juce::Colour::fromRGB (22, 25, 34));
        g.setColour (bg);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);

        // Subtle outer border on the selected row so it stands out
        // against the busy history list.
        if (selected)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
        }

        // Accent bar on the left — colour depends on the kind (T2A = orange,
        // A2A = green) so the user can tell generation types apart at a
        // glance in the OUTPUT list.  Wider when selected.
        const int accentW = selected ? 4 : 3;
        g.setColour (accent);
        // Draw accent as a clipped rect on the left edge of the rounded row
        {
            juce::Path accentPath;
            accentPath.addRectangle (r.getX(), r.getY(), accentW, r.getHeight());
            g.fillPath (accentPath);
        }

        // Title and meta in the area to the left of the buttons
        auto textR = r.withX (r.getX() + 12)
                       .withWidth (playButton.getX() - r.getX() - 16)
                       .withY (r.getY() + 5)
                       .withHeight (r.getHeight() - 10);

        // Kind badge (top-left of the row, before the timestamp)
        const int badgeW = 32;
        auto badgeR = textR.removeFromLeft (badgeW).withHeight (17);
        g.setColour (isA2A
            ? juce::Colour::fromRGB (88, 198, 165).withAlpha (0.18f)
            : juce::Colour::fromRGB (255, 122, 89).withAlpha (0.18f));
        g.fillRoundedRectangle (badgeR.toFloat(), 3.5f);
        g.setColour (isA2A
            ? juce::Colour::fromRGB (88, 198, 165)
            : juce::Colour::fromRGB (255, 122, 89));
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.drawText (isA2A ? "A2A" : "T2A", badgeR,
                    juce::Justification::centred);
        textR.removeFromLeft (6);  // gap between badge and text

        // Timestamp — aligned next to the badge
        g.setColour (selected
            ? juce::Colours::white
            : juce::Colour::fromRGB (235, 237, 245));
        g.setFont (juce::Font (11.5f, juce::Font::bold));
        g.drawText (entry.timestamp.toString (false, true),
                    textR.removeFromTop (17),
                    juce::Justification::centredLeft);

        // Prompt snippet
        g.setColour (selected
            ? juce::Colour::fromRGB (210, 214, 230)
            : juce::Colour::fromRGB (170, 174, 190));
        g.setFont (juce::Font (10.5f));
        auto promptLine = textR.removeFromTop (16);
        // Truncate with ellipsis if needed
        juce::String promptText = entry.prompt.substring (0, 42);
        if (entry.prompt.length() > 42)
            promptText += "...";
        g.drawText (promptText.isEmpty() || promptText == "..." ? "(no prompt info)" : promptText,
                    promptLine, juce::Justification::centredLeft);

        // Metadata line
        g.setColour (selected
            ? juce::Colour::fromRGB (190, 200, 220)
            : juce::Colour::fromRGB (110, 114, 130));
        g.setFont (juce::Font (9.5f));
        const int srKHz = (entry.sampleRate + 500) / 1000;   // 44100 -> "44", 48000 -> "48"
        juce::String meta;
        if (isA2A)
        {
            juce::String src = entry.sourceFile.isNotEmpty()
                ? juce::File (entry.sourceFile).getFileName()
                : juce::String ("?");
            meta = entry.modelId + "  \u2022  from " + src
                 + "  \u2022  " + juce::String (entry.durationSec, 1) + "s"
                 + "  \u2022  " + juce::String (srKHz) + "kHz"
                 + "  \u2022  noise " + juce::String (entry.initNoiseLevel, 2);
        }
        else
        {
            meta = entry.modelId + "  \u2022  " + juce::String (entry.bpm) + " BPM  \u2022  " +
                   juce::String (entry.durationSec, 1) + "s  \u2022  " +
                   juce::String (srKHz) + "kHz";
        }
        g.drawText (meta, textR, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        int bw = 30;     // [x] button width
        int prw = 60;    // Prompt button width
        int pw = 50;     // Play button width
        int h  = 24;
        int y  = (r.getHeight() - h) / 2 + r.getY();

        deleteButton.setBounds (r.getRight() - bw,                    y, bw, h);
        promptButton.setBounds (r.getRight() - bw - prw - 6,          y, prw, h);
        playButton.setBounds   (r.getRight() - bw - prw - pw - 12,    y, pw, h);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        // Click anywhere in the row (except on a button — JUCE
        // routes those clicks to the button) selects it AND asks the
        // editor to switch the waveform to this clip.
        //
        // Use the direct back-pointer (set in HistoryComponent::rebuild())
        // rather than dynamic_cast<>
        // (getParentComponent()->getParentComponent()): the parent
        // chain in JUCE is content→viewport→HistoryComponent (two
        // hops, not one), so the cast would land on the Viewport and
        // silently fail — which is exactly the bug we hit before.
        if (owner) owner->setSelectedIndex (index);
        if (cbSelect) cbSelect();
    }

    int                                       index;
    HistoryEntry                              entry;
    std::function<void()>                     cbSelect, cbPlay, cbDelete, cbPrompt;
    bool                                      selected = false;
    juce::TextButton                          playButton, promptButton, deleteButton;
    // Non-owning back-pointer to the HistoryComponent that created
    // this row.  Lifetime matches the row (owned by the component's
    // OwnedArray).  Set in HistoryComponent::rebuild().
    HistoryComponent*                         owner = nullptr;
};

// ── HistoryComponent ────────────────────────────────────────────────────
HistoryComponent::HistoryComponent (GenerationHistory& h)
    : history (h)
{
    addAndMakeVisible (headerLabel);
    headerLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    headerLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 174, 190));
    headerLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (clearButton);
    clearButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (44, 36, 38));
    clearButton.setColour (juce::TextButton::textColourOffId, juce::Colour::fromRGB (255, 122, 89));
    clearButton.onClick = [this] { if (onClear) onClear(); };

    addAndMakeVisible (menuButton);
    menuButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (44, 48, 60));
    menuButton.setColour (juce::TextButton::textColourOffId, juce::Colour::fromRGB (200, 204, 220));
    menuButton.setTooltip ("Output folder options");
    menuButton.onClick = [this] { if (onMenu) onMenu(); };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content);
    viewport.setScrollBarsShown (true, false);
    content.setSize (100, 100);

    history.addChangeListener (this);
    rebuild();
}

HistoryComponent::~HistoryComponent()
{
    history.removeChangeListener (this);
    rows.clear();
}

void HistoryComponent::setOnSelectRequested (std::function<void (const HistoryEntry&)> cb) { onSelect = std::move (cb); }
void HistoryComponent::setOnPlayRequested   (std::function<void (const HistoryEntry&)> cb) { onPlay   = std::move (cb); }
void HistoryComponent::setOnDeleteRequested (std::function<void (const HistoryEntry&)> cb) { onDelete = std::move (cb); }
void HistoryComponent::setOnPromptRequested (std::function<void (const HistoryEntry&)> cb) { onPrompt = std::move (cb); }
void HistoryComponent::setOnClearRequested  (std::function<void()> cb)                     { onClear  = std::move (cb); }
void HistoryComponent::setOnMenuRequested   (std::function<void()> cb)                     { onMenu   = std::move (cb); }

void HistoryComponent::setPlayingFile (const juce::File& f)
{
    // Persist the playing state so future rebuild() calls (the
    // timer's 1-Hz output-folder rescan, etc.) re-apply it.  Without
    // this, every rebuild would clobber the "Stop" label back to
    // "Play" and the button would flicker.
    playingFile = f;
    for (auto& row : rows)
        row->setIsPlaying (row->entry.audioFile == f);
}

void HistoryComponent::setSelectedIndex (int i)
{
    if (i == selectedIndex) return;
    if (i >= entries.size()) i = -1;
    selectedIndex = i;
    for (int k = 0; k < rows.size(); ++k)
        rows[k]->setSelected (k == selectedIndex);
}

void HistoryComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
    if (selectedIndex >= entries.size())
        selectedIndex = entries.isEmpty() ? -1 : 0;
}

void HistoryComponent::rebuild()
{
    // Only show entries that live in the current output folder.
    // Entries from other folders (e.g. A2A when in T2A mode) are
    // kept in `history.entries` and reappear with their full
    // metadata when the user switches back.  See
    // GenerationHistory::getEntriesInOutputDir() and the comment
    // on GenerationHistory::setOutputDir() for why we filter here
    // instead of dropping entries on folder switch.
    entries = history.getEntriesInOutputDir();
    rows.clear();

    int y = 4;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries.getReference (i);
        auto row = std::make_unique<HistoryRow> (i, e,
            [this, i] { if (onSelect  && i < entries.size()) onSelect  (entries.getReference (i)); },
            [this, i] { if (onPlay    && i < entries.size()) onPlay    (entries.getReference (i)); },
            [this, i] { if (onDelete  && i < entries.size()) onDelete  (entries.getReference (i)); },
            [this, i] { if (onPrompt  && i < entries.size()) onPrompt  (entries.getReference (i)); });
        row->setIndex (i);
        row->setSelected (i == selectedIndex);
        row->owner = this;   // back-pointer for mouseDown — see HistoryRow::mouseDown
        content.addAndMakeVisible (row.get());
        row->setBounds (0, y, content.getWidth(), kRowH);
        rows.add (std::move (row));
        y += kRowH;
    }
    int totalH = juce::jmax (kRowH, entries.size() * kRowH + 8);
    content.setSize (viewport.getWidth() - 4, totalH);

    // Re-apply the persisted playing state to the freshly-created
    // rows.  Each HistoryRow's constructor sets its playButton to
    // "Play", so without this the timer's periodic rescan would
    // flicker the currently-playing row's label between "Stop" and
    // "Play".
    for (auto& row : rows)
        row->setIsPlaying (row->entry.audioFile == playingFile);
}

void HistoryComponent::resized()
{
    auto r = getLocalBounds();
    auto headerR = r.removeFromTop (kHeaderH);
    // header: [HISTORY ............] [CLEAR] [⋮]
    int buttonW    = 24;
    int clearW     = 56;
    int rightEdge  = headerR.getRight() - 6;
    menuButton.setBounds  ({ rightEdge - buttonW, headerR.getY() + 6, buttonW, kButtonH });
    clearButton.setBounds ({ menuButton.getX() - clearW - 4, headerR.getY() + 6, clearW, kButtonH });
    headerLabel.setBounds (headerR.removeFromLeft (clearButton.getX() - headerR.getX() - 6).reduced (0, 8));
    viewport.setBounds (r);
    // Resize all rows to match new width
    for (auto& row : rows)
        row->setSize (viewport.getWidth() - 4, row->getHeight());
    content.setSize (viewport.getWidth() - 4, content.getHeight());
}

void HistoryComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (24, 27, 35));

    // Orange accent bar on the left edge of the viewport area
    auto vpBounds = viewport.getBounds();
    g.setColour (juce::Colour::fromRGB (255, 122, 89));
    g.fillRect (vpBounds.getX(), vpBounds.getY(), 3, vpBounds.getHeight());

    if (entries.isEmpty())
    {
        g.setColour (juce::Colour::fromRGB (120, 124, 140));
        g.setFont (juce::Font (12.0f));
        g.drawText ("History is empty\nGenerate audio to fill it",
                    getLocalBounds().reduced (8, 20), juce::Justification::centred);
    }
}

} // namespace dawalka
