#pragma once

#include "Common.h"

namespace dawalka {

class GenerationHistory : public juce::ChangeBroadcaster
{
public:
    GenerationHistory();
    ~GenerationHistory() override;

    void add (HistoryEntry e);
    void remove (const juce::String& id);
    void clear();
    void clearSession();   // alias for clear(); used by the UI Clear button

    // Remove only the entries whose audio file lives in the current
    // output folder, AND delete those WAVs from disk.  Used by the
    // OUTPUT panel's CLEAR button — must not touch files belonging
    // to the other mode (T2A vs A2A) that just happen to still be
    // in our `entries` cache.
    void clearCurrentOutputDir();

    // Switch the folder where generated WAVs land.  We KEEP all
    // existing entries in memory (the OUTPUT panel filters by
    // folder), so the A2A / T2A tags survive mode switches.  The
    // new folder is created if missing.
    void setOutputDir (const juce::File& dir);

    // Rescan the output folder and add any WAV files that aren't already
    // in the history.  Safe to call repeatedly — it only ADDS files,
    // never removes them, and dedupes by id (the WAV filename).
    void rescanOutputFolder();

    // All entries (regardless of which folder they live in).
    juce::Array<HistoryEntry> getAll() const;

    // Only entries whose audio file is a child of the CURRENT
    // outputDir — the right thing to feed to the OUTPUT panel
    // after setOutputDir has been called.  The OUTPUT panel uses
    // this so a T2A→A2A→T2A switch doesn't lose metadata.
    juce::Array<HistoryEntry> getEntriesInOutputDir() const;

    std::optional<HistoryEntry> findById (const juce::String& id) const;

    // Persistence
    void load();
    void save();

    juce::File getFile() const { return historyFile; }
    juce::File getOutputDir() const { return outputDir; }

private:
    juce::File                  historyFile;
    juce::File                  outputDir;
    mutable juce::CriticalSection mutex;
    juce::Array<HistoryEntry>  entries;
};

} // namespace dawalka
