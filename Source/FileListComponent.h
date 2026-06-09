#pragma once

#include "Common.h"
#include "AudioExporter.h"

namespace dawalka {

// A simple folder-based file browser.
//
// Used for the A2A "INPUT" pane — shows every .wav in a folder, lets
// the user click to select a file (the "current input" — i.e. what
// /a2a will use as the init audio), and offers Play / Delete per row.
//
// Unlike HistoryComponent this is NOT a ChangeBroadcaster and doesn't
// keep persistent state — it just reflects a directory on disk that
// can change at any time (e.g. user drops a new file into the folder
// from Finder).
class FileListComponent : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    FileListComponent();
    ~FileListComponent() override;

    // Point at a new directory and rebuild the row list.  If the
    // directory doesn't exist yet, it gets created and shown empty.
    void setDirectory (const juce::File& dir);

    // Currently selected (highlighted) file, or invalid File if none.
    juce::File getSelectedFile() const { return selectedFile; }
    void       setSelectedFile (const juce::File& f);

    // Callbacks
    void setOnSelect  (std::function<void (const juce::File&)> cb);
    void setOnPlay    (std::function<void (const juce::File&)> cb);
    void setOnDelete  (std::function<void (const juce::File&)> cb);
    void setOnMenu    (std::function<void()> cb);   // popup menu (change folder, etc.)

    // Optional metadata provider — if set, each row is rendered with
    // the rich HistoryEntry view (badge, timestamp, prompt, model +
    // BPM/duration/etc.) when a matching entry exists in the
    // generation history.  Files with no history entry (e.g. WAVs
    // dropped in from Finder) still render, just in the simpler
    // filename + size + duration view.  This lets the A2A INPUT
    // panel look identical to the T2A OUTPUT panel for files the
    // user generated in DAWalka, while still accepting arbitrary
    // audio files.
    using MetadataProvider = std::function<std::optional<HistoryEntry> (const juce::File&)>;
    void setMetadataProvider (MetadataProvider p) { metadataProvider = std::move (p); }

    juce::TextButton& getMenuButton() { return menuButton; }   // popup anchor

    // Refresh the row list (call after files appear/disappear externally).
    void refresh();

    // Mark the row whose path matches `f` as the currently playing
    // one — flips its per-row Play button text to "Stop" and resets
    // every other row's button to "Play".  Pass an invalid File to
    // clear all rows back to "Play".  Called from PluginEditor on
    // every playingFile change so the click-Play-to-Stop affordance
    // is visible across the panel.
    void setPlayingFile (const juce::File& f);

    // True while the user is dragging files over this component.
    bool isDragHovered() const { return dragHovered; }

    // Callback fired when the user drops one or more files into the
    // empty area of the panel.  The editor handles copying them into
    // the input directory and refreshing the list.
    void setOnFilesDropped (std::function<void (const juce::Array<juce::File>&)> cb);
    std::function<void (const juce::Array<juce::File>&)> onFilesDropped;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Inner content component that hosts the file rows.  Inherits
    // DragAndDropContainer so a FileRow can drag the underlying audio
    // file out of the plugin and onto the host's timeline — JUCE
    // walks the parent chain looking for a DragAndDropContainer when
    // a row reports a description in getDragSourceDescription().
    struct ContentComp : public juce::Component,
                          public juce::DragAndDropContainer
    {
        // DragAndDropContainer is interface-only; no overrides needed.
        // All drag bookkeeping is handled by the base class.
    };

private:
    // Single file row.  Defined inline (not forward-declared) because it's
    // held in a juce::OwnedArray — the compiler needs the complete type at
    // the point where the destructor / OwnedArray is instantiated.
    class FileRow : public juce::Component
    {
    public:
        FileRow (int rowIdx, const juce::File& f,
                 std::function<void()> onSelect,
                 std::function<void()> onPlay,
                 std::function<void()> onDelete,
                 std::optional<HistoryEntry> meta = std::nullopt)
            : file (f), index (rowIdx), meta (std::move (meta)),
              cbSelect (std::move (onSelect)),
              cbPlay   (std::move (onPlay)),
              cbDelete (std::move (onDelete))
        {
            addAndMakeVisible (playButton);
            playButton.setButtonText ("Play");
            playButton.setTooltip ("Play this file in the plugin");
            playButton.onClick = [this] { if (cbPlay) cbPlay(); };

            addAndMakeVisible (deleteButton);
            deleteButton.setButtonText ("[x]");
            deleteButton.setTooltip ("Delete this file");
            deleteButton.onClick = [this] { if (cbDelete) cbDelete(); };

            // If we have history metadata, use the rich info; otherwise
            // fall back to file-derived info (size, duration, mtime).
            if (this->meta)
            {
                richTimestamp = this->meta->timestamp.toString (false, true);
                richPrompt    = this->meta->prompt.substring (0, 38);
                richMeta      = buildRichMeta (*this->meta);
                durationText  = formatDuration (this->meta->durationSec);
            }
            else
            {
                durationText  = formatDuration (AudioExporter::getAudioDuration (f));
            }
        }

        // Format "model  -  BPM  -  duration  -  sample rate kHz"  (T2A)
        // or "model  -  from <source>  -  duration  -  noise σ"  (A2A)
        // to match the HistoryComponent's row meta line exactly.
        static juce::String buildRichMeta (const HistoryEntry& e)
        {
            const int srKHz = (e.sampleRate + 500) / 1000;
            if (e.kind == "a2a")
            {
                juce::String src = e.sourceFile.isNotEmpty()
                    ? juce::File (e.sourceFile).getFileName()
                    : juce::String ("?");
                return e.modelId + "  -  from " + src
                     + "  -  " + juce::String (e.durationSec, 1) + "s"
                     + "  -  " + juce::String (srKHz) + "kHz"
                     + "  -  noise " + juce::String (e.initNoiseLevel, 2);
            }
            return e.modelId + "  -  " + juce::String (e.bpm) + " BPM  -  " +
                   juce::String (e.durationSec, 1) + "s  -  " +
                   juce::String (srKHz) + "kHz";
        }

        // Drag-source description: the absolute path of the file.  Not
        // a virtual Component method — we just keep the same name as
        // a hint — the actual drag is started manually from
        // mouseDrag() below.
        juce::var getDragSourceDescription() const
        {
            return juce::var (file.getFullPathName());
        }

        // Flip the per-row Play button text between "Play" and
        // "Stop" so the click-Play-to-Stop affordance is visible
        // inline.  Called by FileListComponent::setPlayingFile().
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
                playButton.setTooltip   ("Play this file in the plugin");
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // mouseDown already selected the row.  If the user moves
            // the cursor past the drag threshold, kick off a
            // system-level file drag.  JUCE's static
            // performExternalDragDropOfFiles() writes file URLs to
            // the macOS pasteboard and calls NSWindow's dragImage:,
            // which is exactly what Logic listens for on the
            // timeline.  The function blocks until the user drops or
            // cancels, so no further mouseDrag/mouseUp will fire for
            // this gesture — that's fine, the row is already selected.
            if (e.getDistanceFromDragStart() < 4) return;
            if (! file.existsAsFile()) return;
            juce::StringArray files;
            files.add (file.getFullPathName());
            // canMoveFiles = false: the file is the user's input —
            // they probably don't want it moved/deleted by the
            // receiver.  Logic always copies anyway.
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                files, /*canMoveFiles*/ false, this);
        }

        void setSelected (bool s) { selected = s; repaint(); }
        void setIndex (int i) { index = i; }

        juce::File file;
        int                                       index;
        std::optional<HistoryEntry>               meta;   // present iff this file is in the generation history
        juce::String                              richTimestamp;  // history row's first text line
        juce::String                              richPrompt;     // history row's second text line
        juce::String                              richMeta;       // history row's third text line (model + BPM + ...)
        juce::String                              durationText;   // e.g. "3:42.1"; empty on error

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            auto bg = selected
                ? juce::Colour::fromRGB (62, 72, 102)               // brighter blue
                : (index % 2 == 0
                    ? juce::Colour::fromRGB (28, 31, 40)
                    : juce::Colour::fromRGB (24, 27, 35));
            g.setColour (bg);
            g.fillRect (r);

            // Subtle outer border for the selected row so it stands
            // out even when surrounded by similar-looking rows.
            if (selected)
            {
                g.setColour (juce::Colour::fromRGB (255, 255, 255).withAlpha (0.18f));
                g.drawRect (r.reduced (0, 1), 1);
            }

            // Accent bar on the left.  Wider + brighter when selected.
            // Colour mirrors the T2A/A2A badge so the user can tell
            // generation types apart at a glance.
            const bool isA2A = meta && meta->kind == "a2a";
            const int accentW = selected ? 5 : 3;
            g.setColour (selected
                ? juce::Colours::white
                : (isA2A
                    ? juce::Colour::fromRGB (88, 198, 165)   // green for A2A
                    : juce::Colour::fromRGB (255, 122, 89))); // orange for T2A
            g.fillRect (r.removeFromLeft (accentW));

            auto textR = r.withX (r.getX() + 10)
                           .withWidth (playButton.getX() - r.getX() - 12)
                           .withY (r.getY() + 4)
                           .withHeight (r.getHeight() - 8);

            if (meta)
            {
                // Rich view: identical to the T2A history row — kind
                // badge, timestamp, prompt, and model / BPM / duration
                // meta line.  This is what the user sees for files
                // they generated in DAWalka and then re-selected as
                // an A2A input.
                const int badgeW = 30;
                auto badgeR = textR.removeFromLeft (badgeW).withHeight (16);
                g.setColour (isA2A
                    ? juce::Colour::fromRGB (88, 198, 165).withAlpha (0.20f)
                    : juce::Colour::fromRGB (255, 122, 89).withAlpha (0.20f));
                g.fillRoundedRectangle (badgeR.toFloat(), 3.0f);
                g.setColour (isA2A
                    ? juce::Colour::fromRGB (88, 198, 165)
                    : juce::Colour::fromRGB (255, 122, 89));
                g.setFont (juce::Font (9.5f, juce::Font::bold));
                g.drawText (isA2A ? "A2A" : "T2A", badgeR,
                            juce::Justification::centred);
                textR.removeFromLeft (4);

                g.setColour (selected
                    ? juce::Colours::white
                    : juce::Colour::fromRGB (235, 237, 245));
                g.setFont (juce::Font (selected ? 12.5f : 12.0f, juce::Font::bold));
                g.drawText (richTimestamp, textR.removeFromTop (16),
                            juce::Justification::centredLeft);

                g.setColour (selected
                    ? juce::Colour::fromRGB (210, 214, 230)
                    : juce::Colour::fromRGB (180, 184, 200));
                g.setFont (juce::Font (11.0f));
                g.drawText (richPrompt, textR.removeFromTop (16),
                            juce::Justification::centredLeft);

                g.setColour (selected
                    ? juce::Colour::fromRGB (200, 210, 230)
                    : juce::Colour::fromRGB (120, 124, 140));
                g.setFont (juce::Font (10.0f));
                g.drawText (richMeta, textR, juce::Justification::centredLeft);
            }
            else
            {
                // Simple view: filename + size + duration + mtime.
                // Used for files dropped in from Finder that have no
                // matching history entry.
                g.setColour (selected
                    ? juce::Colours::white
                    : juce::Colour::fromRGB (235, 237, 245));
                g.setFont (juce::Font (selected ? 12.5f : 12.0f, juce::Font::bold));
                g.drawText (file.getFileName(), textR.removeFromTop (16),
                            juce::Justification::centredLeft);

                g.setColour (selected
                    ? juce::Colour::fromRGB (200, 210, 230)
                    : juce::Colour::fromRGB (120, 124, 140));
                g.setFont (juce::Font (10.0f));
                juce::String metaStr = fileSizeString (file);
                if (durationText.isNotEmpty())
                    metaStr += "  -  " + durationText;
                metaStr += "  -  " + file.getLastModificationTime().toString (false, true);
                g.drawText (metaStr, textR, juce::Justification::centredLeft);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 4);
            int bw = 28;     // [x] button width
            int pw = 48;     // Play button width
            int h  = 22;
            int y  = (r.getHeight() - h) / 2 + r.getY();

            deleteButton.setBounds (r.getRight() - bw,                    y, bw, h);
            playButton.setBounds   (r.getRight() - bw - pw - 4,           y, pw, h);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            // Direct back-pointer to the owning FileListComponent.  We
            // set this in FileListComponent::rebuild() — the parent
            // chain in JUCE is content→viewport→FileListComponent
            // (two hops, not one), so dynamic_cast<>
            // (getParentComponent()->getParentComponent()) would land
            // on the Viewport and silently fail.  A back-pointer is
            // both shorter and impossible to get wrong.
            if (owner) owner->setSelectedFile (file);
            if (cbSelect) cbSelect();
        }

        std::function<void()>                     cbSelect, cbPlay, cbDelete;
        bool                                      selected = false;
        juce::TextButton                          playButton, deleteButton;
        // Non-owning back-pointer to the FileListComponent that
        // created this row.  Set in rebuild() and never null while
        // the row is alive (the row is owned by the list's
        // OwnedArray, so they have the same lifetime).
        FileListComponent*                        owner = nullptr;

    private:
        static juce::String fileSizeString (const juce::File& f)
        {
            if (! f.existsAsFile()) return "?";
            auto bytes = f.getSize();
            if (bytes < 1024)            return juce::String (bytes) + " B";
            if (bytes < 1024 * 1024)    return juce::String (bytes / 1024.0, 1) + " KB";
            return juce::String (bytes / (1024.0 * 1024.0), 1) + " MB";
        }

        // Format a duration in seconds as M:SS.s (or H:MM:SS for tracks
        // over an hour).  Returns empty string on non-positive or
        // non-finite input.  Safe to call from paint() — the underlying
        // probe is exception-guarded and never reads sample data.
        static juce::String formatDuration (double secs)
        {
            if (! std::isfinite (secs) || secs <= 0.0) return {};
            int totalSecs = static_cast<int> (secs);
            int hours     = totalSecs / 3600;
            int minutes   = (totalSecs % 3600) / 60;
            int seconds   = totalSecs % 60;
            int tenths    = static_cast<int> ((secs - totalSecs) * 10.0);
            if (hours > 0)
                return juce::String::formatted ("%d:%02d:%02d", hours, minutes, seconds);
            return juce::String::formatted ("%d:%02d.%d", minutes, seconds, tenths);
        }
    };

    void rebuild();

    juce::File                           directory;
    juce::File                           selectedFile;
    // Persisted across rebuild() so the per-row Play/Stop label
    // stays correct after the timer's 1-Hz refresh / output-folder
    // rescan wipes and re-creates the rows.  The editor's
    // setPlayingFile() call is also stored here so the next
    // rebuild() can re-apply it.
    juce::File                           playingFile;
    juce::Viewport                       viewport;
    // The inner content component doubles as a DragAndDropContainer so
    // FileRow can initiate a system-level file drag (→ Logic's timeline)
    // via startDragging() in mouseDrag.  The drag itself is detected by
    // JUCE's Component::mouseDrag machinery, which walks the parent
    // chain looking for a DragAndDropContainer and pulls the description
    // from getDragSourceDescription() on the source component.
    ContentComp                          content;
    juce::OwnedArray<FileRow>            rows;

    juce::Label                          headerLabel { {}, "INPUT" };
    juce::TextButton                     menuButton  { "\u2026" };   // "…"

    // Drop-zone overlay shown while the user is dragging a Finder file
    // over the panel.  This is a child component (not drawn in paint())
    // so it sits ON TOP of the viewport and its rows — the user can
    // read the hint even when the list is full.  Mouse clicks pass
    // straight through so the drop still lands on the FileListComponent.
    class DragOverlay : public juce::Component
    {
    public:
        DragOverlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour::fromRGB (88, 198, 165).withAlpha (0.20f));
            g.fillRect (getLocalBounds());
            g.setColour (juce::Colour::fromRGB (88, 198, 165));
            g.drawRect (getLocalBounds().reduced (2), 2);
            g.setColour (juce::Colour::fromRGB (220, 232, 224));
            g.setFont (juce::Font (13.0f, juce::Font::bold));
            g.drawText ("Drop to add as A2A input", getLocalBounds(),
                        juce::Justification::centred);
        }
    };
    DragOverlay                          dragOverlay;

    std::function<void (const juce::File&)> onSelect;
    std::function<void (const juce::File&)> onPlay;
    std::function<void (const juce::File&)> onDelete;
    std::function<void()>                  onMenu;
    MetadataProvider                       metadataProvider;

    bool dragHovered = false;

    static constexpr int kRowH      = 56;   // matches HistoryComponent so 3-line rich rows fit
    static constexpr int kHeaderH   = 36;
    static constexpr int kButtonH   = 22;
};

} // namespace dawalka
