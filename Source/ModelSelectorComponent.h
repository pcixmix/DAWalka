#pragma once

#include "Common.h"
#include "ModelManager.h"
#include <map>

namespace dawalka {

class ModelSelectorComponent : public juce::Component,
                               private ModelManager::Listener
{
public:
    ModelSelectorComponent (ModelManager& mm);
    ~ModelSelectorComponent() override;

    void setOnSelectionChanged (std::function<void (const juce::String&)> cb);
    juce::String getSelectedId() const;
    // Select a model by its id (e.g. "sa3-sm-music").  No-op if the
    // id isn't in the current list — callers (e.g. the editor on
    // startup) can retry later when the list is populated.
    void setSelectedId (const juce::String& id);

    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

    // ModelManager::Listener
    void modelStateChanged (const ModelState& s) override;
    void modelListChanged() override { refresh(); }

private:
    ModelManager&                            mm;
    juce::ComboBox                           selector;
    juce::Label                              statusLabel;
    std::function<void (const juce::String&)> onSelectionChanged;
    std::map<int, juce::String>              idsByIndex;    // combo index -> model id

    juce::String currentId;
    juce::String pendingId;   // set by setSelectedId() when the list isn't ready yet

    void syncFromManager();
    void updateStatus();
    void comboChanged();
};

} // namespace dawalka
