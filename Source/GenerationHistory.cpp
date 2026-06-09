#include "GenerationHistory.h"

namespace dawalka {

GenerationHistory::GenerationHistory()
{
    auto support = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile ("Library/Application Support")
                       .getChildFile ("DAWalka");
    historyFile = support.getChildFile (kHistoryFileName);
    // Default to the user's Documents folder (~/Documents/DAWalka/T2A)
    // so files are easy to find.  Can be overridden by setOutputDir() once
    // the user has chosen a different folder.
    outputDir   = defaultOutputDir();
    historyFile.getParentDirectory().createDirectory();
    outputDir.createDirectory();
    load();
    rescanOutputFolder();
}

void GenerationHistory::setOutputDir (const juce::File& dir)
{
    auto d = dir;
    if (d.existsAsFile()) d = d.getParentDirectory();   // user picked a file, not a folder
    if (! d.exists()) d.createDirectory();
    outputDir = d;
    // KEEP all entries across the folder switch — the OUTPUT panel
    // filters by current outputDir (see getEntriesInOutputDir), so
    // switching T2A→A2A→T2A doesn't lose the A2A / T2A tags that
    // each entry carries.  We used to drop the "wrong-folder"
    // entries here, but the rescan would later re-add them as
    // fresh entries (kind="t2a" default), which silently turned
    // A2A-generated clips into "T2A" in the badge — the regression
    // the user reported on 2026-06-06.  Leaving them in place fixes
    // that, and the per-folder filter in HistoryComponent keeps
    // the UI correct.
    save();
    sendChangeMessage();
    rescanOutputFolder();
}

GenerationHistory::~GenerationHistory()
{
    save();
}

void GenerationHistory::add (HistoryEntry e)
{
    const juce::CriticalSection::ScopedLockType l (mutex);
    // Dedup by id (the WAV filename stem) or by audioFile path.  If we
    // find an existing entry that matches, REPLACE it with the new
    // metadata — the rescan path may have added a placeholder entry
    // ("(no prompt info)") before the safety net got a chance to
    // deliver the rich metadata, and we want the rich version to win.
    for (int i = 0; i < entries.size(); ++i)
    {
        auto& existing = entries.getReference (i);
        if (existing.id == e.id || existing.audioFile == e.audioFile)
        {
            existing = e;
            save();
            sendChangeMessage();
            return;
        }
    }
    entries.insert (0, e);
    while (entries.size() > 200) entries.removeLast();
    save();
    sendChangeMessage();
}

void GenerationHistory::remove (const juce::String& id)
{
    juce::File fileToDelete;
    {
        const juce::CriticalSection::ScopedLockType l (mutex);
        for (int i = entries.size(); --i >= 0;)
        {
            if (entries[i].id == id)
            {
                fileToDelete = entries[i].audioFile;
                entries.remove (i);
                break;
            }
        }
    }
    // Delete the WAV from disk too — the file is the canonical artifact;
    // the history entry is just metadata.  If the file is already gone
    // (e.g. user dragged it into Logic and back), we silently succeed.
    if (fileToDelete.existsAsFile())
        fileToDelete.deleteFile();
    save();
    sendChangeMessage();
}

void GenerationHistory::clear()
{
    juce::Array<juce::File> filesToDelete;
    {
        const juce::CriticalSection::ScopedLockType l (mutex);
        for (auto& e : entries)
            if (e.audioFile.existsAsFile())
                filesToDelete.add (e.audioFile);
        entries.clear();
    }
    for (auto& f : filesToDelete)
        f.deleteFile();
    save();
    sendChangeMessage();
}

void GenerationHistory::clearSession()
{
    clear();
}

void GenerationHistory::clearCurrentOutputDir()
{
    // Like clear(), but only touches entries in the CURRENT output
    // folder.  Necessary because setOutputDir no longer drops
    // entries from other folders (we keep them so the A2A tag
    // survives a mode round-trip), and a CLEAR click in T2A mode
    // must not delete the user's A2A outputs.
    juce::Array<juce::File> filesToDelete;
    {
        const juce::CriticalSection::ScopedLockType l (mutex);
        for (int i = entries.size(); --i >= 0;)
        {
            if (entries[i].audioFile.isAChildOf (outputDir))
            {
                if (entries[i].audioFile.existsAsFile())
                    filesToDelete.add (entries[i].audioFile);
                entries.remove (i);
            }
        }
    }
    for (auto& f : filesToDelete)
        f.deleteFile();
    save();
    sendChangeMessage();
}

void GenerationHistory::rescanOutputFolder()
{
    if (! outputDir.isDirectory()) return;

    juce::Array<HistoryEntry> found;
    for (juce::DirectoryEntry entry : juce::RangedDirectoryIterator (
             outputDir, false, "*.wav", juce::File::findFiles))
    {
        auto f = entry.getFile();
        auto t = f.getLastModificationTime();
        auto stem = f.getFileNameWithoutExtension();   // the UUID

        // Try to enrich the entry from a matching job in history.json so
        // we keep the prompt / model / bpm the user originally submitted.
        HistoryEntry e;
        e.id          = stem;
        e.timestamp   = t;
        e.modelId     = "Unknown";
        e.prompt      = "(no prompt info)";
        e.bpm         = 120;
        e.durationSec = 0.0;
        e.audioFile   = f;
        e.seed        = 0;

        {
            const juce::CriticalSection::ScopedLockType l (mutex);
            for (auto& existing : entries)
            {
                if (existing.id == stem || existing.audioFile == f)
                {
                    // Keep the rich metadata from the previous entry
                    e.modelId     = existing.modelId;
                    e.prompt      = existing.prompt;
                    e.bpm         = existing.bpm;
                    e.key         = existing.key;
                    e.durationSec = existing.durationSec;
                    e.seed        = existing.seed;
                    // But use the fresh timestamp + path from disk
                    e.timestamp   = t;
                    e.audioFile   = f;
                    break;
                }
            }
        }

        found.add (e);
    }

    {
        const juce::CriticalSection::ScopedLockType l (mutex);
        // Merge: any history entry whose audio file no longer exists on
        // disk stays in history (user can still play it from cache if it
        // comes back); but we APPEND all on-disk files that aren't yet
        // represented.
        for (auto& f : found)
        {
            bool have = false;
            for (auto& e : entries)
            {
                if (e.id == f.id || e.audioFile == f.audioFile)
                {
                    have = true;
                    break;
                }
            }
            if (! have)
                entries.insert (0, f);
        }
        // Sort newest-first by timestamp
        std::sort (entries.begin(), entries.end(),
            [] (const HistoryEntry& a, const HistoryEntry& b) {
                return a.timestamp > b.timestamp;
            });
        while (entries.size() > 200) entries.removeLast();
    }
    save();
    sendChangeMessage();
}

juce::Array<HistoryEntry> GenerationHistory::getAll() const
{
    const juce::CriticalSection::ScopedLockType l (mutex);
    return entries;
}

juce::Array<HistoryEntry> GenerationHistory::getEntriesInOutputDir() const
{
    // Filter entries to those that live in the CURRENT output
    // folder.  Entries from other folders (e.g. the A2A folder when
    // the user is in T2A mode) stay in `entries` so a mode
    // round-trip preserves their kind= sourceFile= initNoiseLevel
    // metadata, but the OUTPUT panel only renders the current
    // folder's entries.
    const juce::CriticalSection::ScopedLockType l (mutex);
    juce::Array<HistoryEntry> out;
    for (const auto& e : entries)
        if (e.audioFile.isAChildOf (outputDir))
            out.add (e);
    return out;
}

std::optional<HistoryEntry> GenerationHistory::findById (const juce::String& id) const
{
    const juce::CriticalSection::ScopedLockType l (mutex);
    for (auto& e : entries) if (e.id == id) return e;
    return std::nullopt;
}

void GenerationHistory::load()
{
    const juce::CriticalSection::ScopedLockType l (mutex);
    entries.clear();
    if (!historyFile.existsAsFile()) return;
    auto v = juce::JSON::parse (historyFile);
    if (!v.isArray()) return;
    for (auto& i : *v.getArray())
        entries.add (HistoryEntry::fromVar (i));
}

void GenerationHistory::save()
{
    juce::Array<juce::var> arr;
    for (auto& e : entries) arr.add (e.toVar());
    historyFile.getParentDirectory().createDirectory();
    historyFile.replaceWithText (juce::JSON::toString (juce::var (arr), true));
}

} // namespace dawalka
