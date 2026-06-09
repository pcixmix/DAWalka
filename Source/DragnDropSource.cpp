#include "DragnDropSource.h"

namespace dawalka {

void DragnDropSource::performDragDrop (juce::Component* source, const juce::File& f)
{
    if (!source || !f.existsAsFile()) return;

    juce::StringArray files;
    files.add (f.getFullPathName());
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, true, source);
}

} // namespace dawalka
