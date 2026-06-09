#include "Common.h"
#import <Cocoa/Cocoa.h>

namespace dawalka {

// Copy a file path to the macOS pasteboard as a FILE URL — not as plain
// text.  Apps like Logic Pro recognize the file-URL flavor and can
// paste it as a new audio region at the playhead.
//
// The system sandbox allows the general pasteboard to be written from
// any process, so this works from a sandboxed AU host.
void copyFileToPasteboard (const juce::String& path)
{
    if (path.isEmpty()) return;

    NSString* nsPath = [NSString stringWithUTF8String:path.toUTF8()];

    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb declareTypes:@[ NSPasteboardTypeFileURL ] owner:nil];

    NSString* fileURL = [[NSURL fileURLWithPath:nsPath] absoluteString];
    if (fileURL != nil)
        [pb setString:fileURL forType:NSPasteboardTypeFileURL];
}

// Bring another running app to the front by bundle id.  Returns true
// if the app was found and activated.  Used so the user can press
// Cmd+V in Logic immediately after we put the file on the pasteboard.
bool activateAppByBundleId (const juce::String& bundleId)
{
    NSArray<NSRunningApplication*>* apps =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:
            [NSString stringWithUTF8String:bundleId.toUTF8()]];
    for (NSRunningApplication* a in apps)
    {
        if ([a activateWithOptions:NSApplicationActivateAllWindows])
            return true;
    }
    return false;
}

} // namespace dawalka

