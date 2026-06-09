#include "AppleSiliconDetector.h"
#include <sys/sysctl.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <unistd.h>

namespace dawalka {

namespace {

juce::String sysctlString (const char* name)
{
    char buf[256] = { 0 };
    size_t len = sizeof (buf);
    if (sysctlbyname (name, buf, &len, nullptr, 0) != 0)
        return {};
    return juce::String (buf);
}

long sysctlInt (const char* name)
{
    int value = 0;
    size_t len = sizeof (value);
    if (sysctlbyname (name, &value, &len, nullptr, 0) != 0)
        return 0;
    return value;
}

uint64_t totalMemory()
{
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    uint64_t value = 0;
    size_t len = sizeof (value);
    if (sysctl (mib, 2, &value, &len, nullptr, 0) != 0)
        return 0;
    return value;
}

AppleSilicon classify (const juce::String& brand)
{
    const auto s = brand.toLowerCase();
    auto contains = [&] (const char* needle) { return s.contains (needle); };

    if (s.contains ("m4 max"))   return AppleSilicon::M4Max;
    if (s.contains ("m4 pro"))   return AppleSilicon::M4Pro;
    if (s.contains ("m4"))       return AppleSilicon::M4;

    if (s.contains ("m3 max"))   return AppleSilicon::M3Max;
    if (s.contains ("m3 pro"))   return AppleSilicon::M3Pro;
    if (s.contains ("m3"))       return AppleSilicon::M3;

    if (s.contains ("m2 ultra")) return AppleSilicon::M2Ultra;
    if (s.contains ("m2 max"))   return AppleSilicon::M2Max;
    if (s.contains ("m2 pro"))   return AppleSilicon::M2Pro;
    if (s.contains ("m2"))       return AppleSilicon::M2;

    if (s.contains ("m1 ultra")) return AppleSilicon::M1Ultra;
    if (s.contains ("m1 max"))   return AppleSilicon::M1Max;
    if (s.contains ("m1 pro"))   return AppleSilicon::M1Pro;
    if (s.contains ("m1"))       return AppleSilicon::M1;

    return AppleSilicon::Unknown;
}

} // namespace

AppleSiliconDetector::Report AppleSiliconDetector::detectBasic()
{
    Report r;
    r.chipString = sysctlString ("machdep.cpu.brand_string");
    if (r.chipString.isEmpty())
        r.chipString = sysctlString ("hw.model");

    r.chip = classify (r.chipString);
    r.isAppleSilicon = (r.chip != AppleSilicon::Unknown) ||
                       r.chipString.containsIgnoreCase ("Apple");

    r.coreCount = static_cast<int> (sysctlInt ("hw.ncpu"));
    r.perfCores = static_cast<int> (sysctlInt ("hw.perflevel0.logicalcpu"));
    r.effCores  = static_cast<int> (sysctlInt ("hw.perflevel1.logicalcpu"));
    if (r.perfCores == 0) r.perfCores = r.coreCount;
    r.memBytes  = totalMemory();

    return r;
}

AppleSiliconDetector::Report AppleSiliconDetector::detectFull()
{
    auto r = detectBasic();

    // MLX probe MUST be off the message thread (and outside any sandboxed
    // host that disallows fork/exec). Use detectFull() from a background
    // thread; never from the plugin / editor constructor.
    if (r.isAppleSilicon)
    {
        juce::ChildProcess p;
        juce::StringArray args;
        args.add ("/bin/sh");
        args.add ("-c");
        args.add ("python3 -c 'import mlx.core; print(\"ok\")' 2>/dev/null");

        if (p.start (args, /*shellCommand*/ false))
        {
            juce::String out;
            char buf[256];
            for (;;)
            {
                const int n = p.readProcessOutput (buf, sizeof (buf) - 1);
                if (n > 0)
                {
                    buf[n] = '\0';
                    out += buf;
                }
                else
                {
                    break;
                }
            }
            r.hasMLX = out.contains ("ok");
        }
    }

    return r;
}

bool AppleSiliconDetector::isAppleSilicon()
{
    static const bool cached = detect().isAppleSilicon;
    return cached;
}

juce::String AppleSiliconDetector::chipName()
{
    static const juce::String cached = detect().chipString;
    return cached;
}

} // namespace dawalka
