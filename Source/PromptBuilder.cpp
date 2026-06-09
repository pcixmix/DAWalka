#include "PromptBuilder.h"

namespace dawalka {

juce::String PromptBuilder::sanitise (const juce::String& in)
{
    auto s = in.replaceCharacter ('\n', ' ').replaceCharacter ('\r', ' ');
    s = s.replace (",,", ",");
    while (s.contains ("  ")) s = s.replace ("  ", " ");
    return s.trim();
}

juce::String PromptBuilder::build (const juce::String& userPrompt,
                                   int bpm,
                                   const MusicalKey& key,
                                   bool seamlessLoop,
                                   const juce::String& modelId,
                                   const juce::String& extraTags,
                                   bool includeKey,
                                   bool includeBpm)
{
    juce::StringArray parts;
    auto visible = sanitise (userPrompt);
    if (visible.isNotEmpty()) parts.add (visible);

    // SFX model is trained on short sound effects (gunshots, foley,
    // ambient, etc.).  Musical metadata (BPM, key, seamless loop,
    // "professional mix") adds noise that pushes the output toward
    // tonal music and away from what the user actually wants.
    const bool isSfx = (modelId == "sa3-sm-sfx");

    if (isSfx)
    {
        // The sa3-sm-sfx fine-tune responds to "SFX" as a trigger token
        // — without it, the model leans back toward musical output
        // even on sound-effect prompts.  Prepend it to whatever the
        // user typed.
        if (visible.isEmpty())
            parts.add ("SFX");
        else
            parts.set (0, "SFX, " + visible);
    }
    else
    {
        // includeKey / includeBpm let the user opt out of the two
        // tags that push the model toward tonal, rhythmic output
        // (useful when using a music model to generate sound
        // effects).  The bpm / key values are still used by the
        // duration slider and host-tempo sync — only the prompt
        // annotation is suppressed.
        if (includeBpm)
            parts.add (juce::String (bpm) + " BPM");
        if (includeKey)
            parts.add (key.toString());
        if (seamlessLoop) parts.add ("seamless loop");
        if (extraTags.isNotEmpty())
            for (auto t : juce::StringArray::fromTokens (extraTags, ",; ", {}))
                if (t.isNotEmpty()) parts.add (t);
        parts.add ("professional mix");
    }

    // Always — high quality recording is a useful anchor for both
    // music and SFX.
    parts.add ("high quality recording");

    return parts.joinIntoString (", ");
}

} // namespace dawalka
