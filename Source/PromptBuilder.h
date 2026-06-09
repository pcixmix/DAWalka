#pragma once

#include "Common.h"

namespace dawalka {

// Assembles the *visible* prompt (what the user types) and the *effective*
// prompt (what the model actually sees). The user only ever edits the visible
// part; the engine appends metadata that improves the result for the chosen
// model family (music vs SFX).
class PromptBuilder
{
public:
    // Effective prompt = visible + (model-specific metadata).
    //
    //   music models  (sa3-sm-music, sa3-medium)
    //      → visible + BPM + key + "seamless loop" + high-quality tags
    //   SFX model     (sa3-sm-sfx)
    //      → "SFX" trigger token prepended + visible + "high quality
    //        recording" only — without the "SFX" token, the model leans
    //        back toward musical output.  BPM, key, "seamless loop" and
    //        "professional mix" hurt more than they help for foley /
    //        ambient / one-shot sound effects.
    static juce::String build (const juce::String& userPrompt,
                               int bpm,
                               const MusicalKey& key,
                               bool seamlessLoop,
                               const juce::String& modelId,
                               const juce::String& extraTags = {},
                               bool includeKey = true,
                               bool includeBpm = true);

    // Sanitise input — remove line breaks, trim, drop leading/trailing commas
    static juce::String sanitise (const juce::String& in);
};

} // namespace dawalka
