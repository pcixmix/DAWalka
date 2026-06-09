#pragma once

#include "Common.h"

namespace dawalka {

class AppleSiliconDetector
{
public:
    struct Report
    {
        AppleSilicon chip       = AppleSilicon::Unknown;
        juce::String chipString;          // e.g. "Apple M2 Pro"
        int         coreCount  = 0;
        int         perfCores  = 0;
        int         effCores   = 0;
        uint64_t    memBytes   = 0;
        bool        isAppleSilicon = false;
        bool        hasMLX          = false;
    };

    // Safe to call from the message thread / plugin constructor:
    // chip, memory, core counts via sysctl only — no child process spawn.
    static Report detectBasic();

    // Full detection — also probes whether MLX is importable in python3.
    // Spawns /bin/sh, so MUST be called off the message thread (e.g. via
    // a juce::Thread or juce::Timer). Returns the basic report with
    // hasMLX filled in.
    static Report detectFull();

    // Legacy alias for detectBasic() — preserves existing call sites.
    static Report detect() { return detectBasic(); }

    static bool  isAppleSilicon();
    static juce::String chipName();
};

} // namespace dawalka
