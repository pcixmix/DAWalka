#include "ModelSelectorComponent.h"
#include "LookAndFeel.h"

namespace dawalka {

ModelSelectorComponent::ModelSelectorComponent (ModelManager& m)
    : mm (m)
{
    addAndMakeVisible (selector);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (folderButton);

    selector.onChange = [this] { comboChanged(); };

    folderButton.onClick = [this] { folderButtonClicked(); };
    folderButton.setButtonText ("...");
    folderButton.setTooltip ("Choose models folder");

    mm.addListener (this);
    refresh();
}

ModelSelectorComponent::~ModelSelectorComponent()
{
    mm.removeListener (this);
}

void ModelSelectorComponent::setOnSelectionChanged (std::function<void (const juce::String&)> cb)
{
    onSelectionChanged = std::move (cb);
}

void ModelSelectorComponent::setOnModelsDirChanged (std::function<void()> cb)
{
    onModelsDirChanged = std::move (cb);
}

juce::String ModelSelectorComponent::getSelectedId() const
{
    return currentId;
}

void ModelSelectorComponent::setSelectedId (const juce::String& id)
{
    if (id.isEmpty()) return;
    if (currentId == id) return;     // already there — no-op

    // Walk the index->id map to find a match.
    for (auto& kv : idsByIndex)
    {
        if (kv.second == id)
        {
            selector.setSelectedId (kv.first + 1, juce::dontSendNotification);
            currentId = id;
            if (onSelectionChanged) onSelectionChanged (currentId);
            updateStatus();
            return;
        }
    }
    // No match — the model list hasn't been populated yet.  Stash the
    // request so syncFromManager() can pick it up on the next refresh.
    pendingId = id;
    currentId = id;   // remembered for syncFromManager()
}

void ModelSelectorComponent::refresh()
{
    syncFromManager();
}

void ModelSelectorComponent::syncFromManager()
{
    int previousId = selector.getSelectedId();
    selector.clear();
    idsByIndex.clear();

    auto states = mm.getStates();
    int newId = 0;
    int idx = 1;

    for (auto& s : states)
    {
        auto name = s.desc.displayName;
        if (s.status != ModelStatus::Ready)
            name += "  (" + modelStatusToString (s.status) + ")";
        selector.addItem (name, idx);
        idsByIndex[idx - 1] = s.desc.id;
        // Pick the best matching id: explicit pending request first,
        // then the previously-selected one, then whatever happens to
        // be first in the list.
        if (! pendingId.isEmpty() && s.desc.id == pendingId) newId = idx;
        else if (newId == 0 && s.desc.id == currentId)        newId = idx;
        ++idx;
    }
    if (newId == 0) newId = previousId > 0 ? previousId : 1;
    selector.setSelectedId (newId, juce::dontSendNotification);
    pendingId = {};   // consumed

    auto it = idsByIndex.find (newId - 1);
    if (it != idsByIndex.end())
    {
        currentId = it->second;
        if (onSelectionChanged) onSelectionChanged (currentId);
    }
    updateStatus();
}

void ModelSelectorComponent::updateStatus()
{
    auto states = mm.getStates();

    juce::String target = currentId;
    if (target.isEmpty() && states.size() > 0)
        target = states[0].desc.id;

    for (auto& s : states)
    {
        if (s.desc.id != target) continue;

        juce::String status;
        switch (s.status)
        {
            case ModelStatus::Ready:        status = "Ready"; break;
            case ModelStatus::NotInstalled: status = "Not Installed"; break;
            case ModelStatus::Downloading:  status = juce::String ("Downloading ") +
                                                       juce::String (static_cast<int> (s.progress * 100.0)) + "%"; break;
            case ModelStatus::Verifying:    status = "Verifying..."; break;
            case ModelStatus::Loading:      status = "Loading..."; break;
            case ModelStatus::Error:        status = "Error: " + s.errorMessage; break;
        }
        statusLabel.setText (status, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId,
            s.status == ModelStatus::Ready     ? juce::Colour::fromRGB (90, 220, 150) :
            s.status == ModelStatus::Error     ? juce::Colour::fromRGB (255, 90, 100) :
                                                 juce::Colour::fromRGB (235, 237, 245));
        repaint();
        return;
    }

    statusLabel.setText ("No model", juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (150, 156, 172));
}

void ModelSelectorComponent::comboChanged()
{
    int idx = selector.getSelectedId() - 1;
    auto it = idsByIndex.find (idx);
    if (it != idsByIndex.end())
    {
        currentId = it->second;
        if (onSelectionChanged) onSelectionChanged (currentId);
        updateStatus();
    }
}

void ModelSelectorComponent::modelStateChanged (const ModelState&)
{
    juce::MessageManager::callAsync ([this] { updateStatus(); });
}

void ModelSelectorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (24, 27, 35));
    g.setColour (juce::Colour::fromRGB (40, 44, 56));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    // Update tooltip based on custom path
    auto customDir = mm.getCustomModelsDirectory();
    if (customDir.isDirectory())
        folderButton.setTooltip ("Models: " + customDir.getFileName() + "\nClick to change");
    else
        folderButton.setTooltip ("Choose models folder");
}

void ModelSelectorComponent::resized()
{
    // Layout: [status label] [combo fills the rest] [folder button]
    auto r = getLocalBounds().reduced (8, 2);
    int statusW = 90;
    int folderBtnW = 28;
    statusLabel.setBounds (r.removeFromLeft (statusW));
    r.removeFromLeft (4);
    folderButton.setBounds (r.removeFromRight (folderBtnW));
    r.removeFromRight (4);
    selector.setBounds (r);
}

void ModelSelectorComponent::folderButtonClicked()
{
    auto currentDir = mm.getCustomModelsDirectory();
    auto defaultDir = currentDir.isDirectory() ? currentDir
                                               : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    auto* chooser = new juce::FileChooser ("Choose Models Folder", defaultDir, "", true);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.isDirectory())
            {
                mm.setCustomModelsDirectory (result);
                if (onModelsDirChanged) onModelsDirChanged();
            }
            delete chooser;
        });
}

} // namespace dawalka
