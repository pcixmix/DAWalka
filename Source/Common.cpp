#include "Common.h"

namespace dawalka {

juce::String makeUuid()
{
    return juce::Uuid().toString();
}

} // namespace dawalka
