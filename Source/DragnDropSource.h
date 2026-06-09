#pragma once

#include "Common.h"

namespace dawalka {

// Helper to register a Component as a drag source for an audio file
class DragnDropSource
{
public:
    static void performDragDrop (juce::Component* source, const juce::File& f);
};

} // namespace dawalka
