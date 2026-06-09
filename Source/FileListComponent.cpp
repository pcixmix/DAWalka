#include "FileListComponent.h"

namespace dawalka {

// ── FileListComponent ──────────────────────────────────────────────────
FileListComponent::FileListComponent()
{
    addAndMakeVisible (headerLabel);
    headerLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    headerLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 174, 190));
    headerLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (menuButton);
    menuButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (44, 48, 60));
    menuButton.setColour (juce::TextButton::textColourOffId, juce::Colour::fromRGB (200, 204, 220));
    menuButton.setTooltip ("Input folder options");
    menuButton.onClick = [this] { if (onMenu) onMenu(); };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content);
    viewport.setScrollBarsShown (true, false);
    content.setSize (100, 100);

    // Drop-zone overlay — added LAST so it sits on top of the viewport
    // in the z-order.  Hidden by default; shown by fileDragEnter().
    addChildComponent (dragOverlay);
    dragOverlay.setVisible (false);
}

FileListComponent::~FileListComponent()
{
    rows.clear();
}

void FileListComponent::setOnSelect (std::function<void (const juce::File&)> cb) { onSelect = std::move (cb); }
void FileListComponent::setOnPlay   (std::function<void (const juce::File&)> cb) { onPlay   = std::move (cb); }
void FileListComponent::setOnDelete (std::function<void (const juce::File&)> cb) { onDelete = std::move (cb); }
void FileListComponent::setOnMenu   (std::function<void()> cb)                     { onMenu   = std::move (cb); }
void FileListComponent::setOnFilesDropped (std::function<void (const juce::Array<juce::File>&)> cb)
{
    onFilesDropped = std::move (cb);
}

bool FileListComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    // Accept any drag that contains at least one supported audio file
    // (WAV, AIFF, FLAC, OGG).  See Common.h::audioFileExtensions().
    for (const auto& f : files)
    {
        juce::String ext = juce::File (f).getFileExtension();
        if (isAudioFileExtension (ext)) return true;
    }
    return false;
}

void FileListComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHovered = true;
    // Cover the full panel (header + viewport).  The overlay is a
    // child component, so it paints OVER the viewport and rows —
    // unlike a paint() overlay which would be hidden behind them.
    dragOverlay.setBounds (getLocalBounds());
    dragOverlay.setVisible (true);
    dragOverlay.toFront (true);
}

void FileListComponent::fileDragExit (const juce::StringArray&)
{
    dragHovered = false;
    dragOverlay.setVisible (false);
}

void FileListComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragHovered = false;
    dragOverlay.setVisible (false);
    if (! onFilesDropped) return;
    juce::Array<juce::File> audio;
    for (const auto& f : files)
    {
        juce::File file (f);
        if (isAudioFileExtension (file.getFileExtension()))
            audio.add (file);
    }
    if (! audio.isEmpty())
        onFilesDropped (audio);
}

void FileListComponent::setDirectory (const juce::File& dir)
{
    directory = dir;
    if (! directory.isDirectory())
        directory.createDirectory();
    refresh();
}

void FileListComponent::setSelectedFile (const juce::File& f)
{
    if (selectedFile == f) return;
    selectedFile = f;
    for (int k = 0; k < rows.size(); ++k)
        rows[k]->setSelected (rows[k]->file == f);
    repaint();
}

void FileListComponent::refresh()
{
    rebuild();
}

void FileListComponent::setPlayingFile (const juce::File& f)
{
    // Persist the playing state so future rebuild() calls (the
    // timer's 1-Hz input-list refresh, output-folder rescans, etc.)
    // re-apply it.  Without this, every rebuild would clobber the
    // "Stop" label back to "Play" and the button would flicker.
    playingFile = f;
    for (auto& row : rows)
        row->setIsPlaying (row->file == f);
}

void FileListComponent::rebuild()
{
    rows.clear();
    content.removeAllChildren();

    juce::Array<juce::File> audio;
    if (directory.isDirectory())
    {
        // Scan all files (no extension wildcard) and filter in code, so
        // we can include WAV, AIFF, FLAC, OGG and skip macOS resource
        // forks (._foo) and hidden dotfiles.
        for (juce::DirectoryEntry e : juce::RangedDirectoryIterator (
                 directory, false, "*", juce::File::findFiles))
        {
            auto f = e.getFile();
            auto name = f.getFileName();
            if (name.startsWithChar ('.')) continue;        // .DS_Store, .hidden, …
            if (name.startsWith ("._"))    continue;        // resource fork
            if (! isAudioFileExtension (f.getFileExtension())) continue;
            audio.add (f);
        }
        // Newest first
        std::sort (audio.begin(), audio.end(),
                   [] (const juce::File& a, const juce::File& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });
    }

    int y = 4;
    for (int i = 0; i < audio.size(); ++i)
    {
        const auto f = audio.getReference (i);
        // Look up rich history metadata for this file (if a provider
        // was set and a matching entry exists).  Lets the INPUT
        // panel render identical row layout to the OUTPUT panel
        // for files the user generated in DAWalka.
        std::optional<HistoryEntry> meta;
        if (metadataProvider)
        {
            if (auto m = metadataProvider (f))
                meta = std::move (m);
        }
        auto row = std::make_unique<FileRow> (i, f,
            // Capture the File by VALUE, not a reference into the local
            // `audio` array.  The lambdas outlive rebuild() (they live
            // inside the FileRow's std::function members), so capturing
            // &audio would dangle the moment rebuild() returns and the
            // user later clicks the row.  Reading a freed juce::File
            // returns a String with garbage internals → segfault inside
            // juce::File::parseAbsolutePath.
            [this, f] {
                if (onSelect) onSelect (f);
            },
            [this, f] {
                if (onPlay) onPlay (f);
            },
            [this, f] {
                if (onDelete) onDelete (f);
            },
            std::move (meta));
        row->setIndex (i);
        row->setSelected (f == selectedFile);
        row->owner = this;   // back-pointer for mouseDown — see FileRow::mouseDown
        content.addAndMakeVisible (row.get());
        row->setBounds (0, y, content.getWidth(), kRowH);
        rows.add (std::move (row));
        y += kRowH;
    }
    int totalH = juce::jmax (kRowH, audio.size() * kRowH + 8);
    content.setSize (viewport.getWidth() - 4, totalH);

    // Re-apply the persisted playing state to the freshly-created
    // rows.  Each FileRow's constructor sets its playButton to "Play",
    // so without this the timer's periodic refresh would flicker the
    // currently-playing row's label between "Stop" and "Play".
    for (auto& row : rows)
        row->setIsPlaying (row->file == playingFile);
}

void FileListComponent::resized()
{
    auto r = getLocalBounds();
    auto headerR = r.removeFromTop (kHeaderH);
    int buttonW = 24;
    int rightEdge = headerR.getRight() - 6;
    menuButton.setBounds ({ rightEdge - buttonW, headerR.getY() + 6, buttonW, kButtonH });
    headerLabel.setBounds (headerR.reduced (0, 8));
    viewport.setBounds (r);
    for (auto& row : rows)
        row->setSize (viewport.getWidth() - 4, row->getHeight());
    content.setSize (viewport.getWidth() - 4, content.getHeight());
}

void FileListComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (24, 27, 35));

    // Empty-list hint (the drag-hover overlay is a child component,
    // see DragOverlay, so it paints on top of the viewport by itself).
    if (rows.isEmpty())
    {
        g.setColour (juce::Colour::fromRGB (120, 124, 140));
        g.setFont (juce::Font (12.0f));
        g.drawText ("Drop audio files (WAV, AIFF, FLAC, OGG)\n"
                    "into this folder to use them as A2A input",
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace dawalka
