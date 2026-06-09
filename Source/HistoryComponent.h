#pragma once

#include "Common.h"
#include "GenerationHistory.h"

namespace dawalka {

class HistoryComponent : public juce::Component,
                         public juce::ChangeListener
{
public:
    HistoryComponent (GenerationHistory& h);
    ~HistoryComponent() override;

    void setOnSelectRequested (std::function<void (const HistoryEntry&)> cb);
    void setOnPlayRequested   (std::function<void (const HistoryEntry&)> cb);
    void setOnDeleteRequested (std::function<void (const HistoryEntry&)> cb);
    void setOnClearRequested  (std::function<void()> cb);
    void setOnMenuRequested   (std::function<void()> cb);   // popup menu (change output dir, etc.)

    juce::TextButton& getMenuButton() { return menuButton; }   // for PopupMenu anchor

    void paint (juce::Graphics& g) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // Mark the row whose entry.audioFile matches `f` as currently
    // playing — its Play button text flips to "Stop".  Every other
    // row's button resets to "Play".  An invalid File clears all
    // rows back to "Play" (e.g. after natural end of playback).
    void setPlayingFile (const juce::File& f);

    // Currently selected entry (the one whose Play/Insert would fire)
    int getSelectedIndex() const { return selectedIndex; }
    void setSelectedIndex (int i);

private:
    // Drag-source container — see FileListComponent for the same
    // pattern.  Inheriting Component + DragAndDropContainer is the
    // JUCE-blessed way to expose yourself as a drag origin.
    struct ContentComp : public juce::Component,
                         public juce::DragAndDropContainer
    {
    };

    void rebuild();
    void layoutRows();
    void drawRow (juce::Graphics& g, int idx, juce::Rectangle<int> bounds, const HistoryEntry& e);

    GenerationHistory&                       history;
    // Persisted across rebuild() so the per-row Play/Stop label
    // stays correct after the timer's 1-Hz output-folder rescan
    // wipes and re-creates the rows.  See setPlayingFile().
    juce::File                                playingFile;
    juce::Viewport                            viewport;
    // Inner content component that hosts the history rows.  Doubles
    // as a DragAndDropContainer so a HistoryRow can drag its
    // entry.audioFile out of the plugin and onto the host timeline.
    // JUCE's Component::mouseDrag walks the parent chain for a
    // DragAndDropContainer, then reads the description from
    // HistoryRow::getDragSourceDescription() — same pattern as
    // FileListComponent.
    ContentComp                               content;
    juce::Array<HistoryEntry>                 entries;
    int                                       selectedIndex = -1;
    juce::OwnedArray<class HistoryRow>       rows;

    juce::TextButton                          clearButton { "CLEAR" };
    juce::TextButton                          menuButton  { "\u2026" };   // "…" — opens the popup menu
    juce::Label                               headerLabel { {}, "HISTORY" };

    std::function<void (const HistoryEntry&)> onSelect;
    std::function<void (const HistoryEntry&)> onPlay;
    std::function<void (const HistoryEntry&)> onDelete;
    std::function<void()>                     onClear;
    std::function<void()>                     onMenu;

    static constexpr int kRowH       = 56;
    static constexpr int kHeaderH    = 36;
    static constexpr int kButtonH    = 22;
    static constexpr int kButtonGap  = 4;
};

} // namespace dawalka
