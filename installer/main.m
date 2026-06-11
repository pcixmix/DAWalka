// =============================================================================
//  DAWalka Installer — Cocoa UI
// =============================================================================
//  Source for DAWalka.app/Contents/MacOS/DAWalka.  Built with
//  installer/make_app.sh (clang -framework Cocoa).
//
//  The window has two large action buttons ("Install" / "Uninstall") and a
//  scrolling text view that shows the live output of the script.  This
//  matters because:
//
//    1. The user can see real progress (downloads, auval, etc.) instead of
//       staring at a spinner.
//    2. If something fails, the user can copy/paste the error into a bug
//       report without digging through Console.app.
//
//  We run the shell scripts as NSTask subprocesses with stdout/stderr
//  piped back to the text view.  Both buttons are disabled while a script
//  is running, so the user can't kick off two operations at once.
//
//  Path resolution: the install/uninstall scripts live next to this
//  binary at ../Resources/Scripts/{install,uninstall}.sh.  We find them
//  via NSBundle pathForResource so the .app stays self-contained — moving
//  the bundle to a different folder (e.g. /Applications) still works.
// =============================================================================

#import <Cocoa/Cocoa.h>

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTextViewDelegate>
@property (strong) NSWindow             *window;
@property (strong) NSTextView            *outputView;
@property (strong) NSButton              *installButton;
@property (strong) NSButton              *uninstallButton;
@property (strong) NSButton              *revealButton;
@property (assign) BOOL                  scriptRunning;
@end

@implementation AppDelegate

// -----------------------------------------------------------------------------
// helpers
// -----------------------------------------------------------------------------

- (void)appendLine:(NSString *)text
{
    NSTextStorage *storage = [self.outputView textStorage];
    [storage appendAttributedString:
        [[NSAttributedString alloc] initWithString:text
                                        attributes:@{
            NSFontAttributeName: [NSFont fontWithName:@"Menlo" size:11],
            NSForegroundColorAttributeName: [NSColor labelColor],
        }]];
    [self.outputView scrollToEndOfDocument:nil];
}

- (void)setButtonsEnabled:(BOOL)enabled
{
    [self.installButton setEnabled:enabled];
    [self.uninstallButton setEnabled:enabled];
}

// Locate a script that lives at Contents/Resources/Scripts/<name>.sh.
// Returns nil if missing (shouldn't happen in a properly-built .app).
- (NSString *)scriptPath:(NSString *)name
{
    NSString *p = [[NSBundle mainBundle] pathForResource:name ofType:@"sh"
                                             inDirectory:@"Scripts"];
    if (!p) {
        // Fallback: look directly in Resources/Scripts (the bundle layout
        // we use has Scripts/ as a directory inside Resources, but
        // pathForResource:ofType:inDirectory: doesn't recurse).
        NSString *resDir = [[[NSBundle mainBundle] resourcePath]
                            stringByAppendingPathComponent:@"Scripts"];
        p = [[resDir stringByAppendingPathComponent:name]
             stringByAppendingPathExtension:@"sh"];
    }
    return p;
}

// -----------------------------------------------------------------------------
// script runner — runs /bin/bash <script> asynchronously and pipes the
// output back to the text view on the main thread.  The function is
// reentrant: clicking a button while the other is running is a no-op
// (we disable both buttons during the run).
// -----------------------------------------------------------------------------

- (void)runScriptNamed:(NSString *)scriptName
                 title:(NSString *)title
                 extra:(NSArray<NSString *> *)extraArgs
{
    if (self.scriptRunning) return;
    NSString *path = [self scriptPath:scriptName];
    if (!path || ![[NSFileManager defaultManager] isExecutableFileAtPath:path]) {
        [self appendLine:[NSString stringWithFormat:
            @"\n\u274C  Script not found or not executable: %@\n"
            @"      Re-create DAWalka.app via installer/make_app.sh\n", path ?: @"(nil)"]];
        return;
    }

    self.scriptRunning = YES;
    [self setButtonsEnabled:NO];
    [self appendLine:[NSString stringWithFormat:
        @"\n\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500 %@ \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n",
        title]];

    NSMutableArray *args = [NSMutableArray arrayWithObject:path];
    if (extraArgs.count > 0) [args addObjectsFromArray:extraArgs];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSTask *task = [[NSTask alloc] init];
        [task setLaunchPath:@"/bin/bash"];
        [task setArguments:args];

        NSPipe *pipe = [NSPipe pipe];
        [task setStandardOutput:pipe];
        [task setStandardError:pipe];
        [task setStandardInput:[NSPipe pipe]];  // close stdin so prompts fail cleanly

        @try {
            [task launch];
        } @catch (NSException *e) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self appendLine:[NSString stringWithFormat:
                    @"\n\u274C  Failed to launch: %@\n", e.reason]];
                self.scriptRunning = NO;
                [self setButtonsEnabled:YES];
            });
            return;
        }

        // Stream output as it arrives so the user sees progress
        // (model downloads, auval etc.) in real time instead of
        // waiting for the whole thing to finish.
        NSFileHandle *fh = [pipe fileHandleForReading];
        __block BOOL gotData = NO;
        while (1)
        {
            @autoreleasepool {
                NSData *chunk = [fh availableData];
                if (chunk.length == 0) break;
                gotData = YES;
                NSString *str = [[NSString alloc] initWithData:chunk
                                                      encoding:NSUTF8StringEncoding];
                if (str.length == 0) {
                    // Non-UTF8 (e.g. partial multi-byte) — fall back to
                    // lossy decode so we still see something.
                    str = [[NSString alloc] initWithData:chunk
                                               encoding:NSISOLatin1StringEncoding];
                }
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self appendLine:str];
                });
            }
        }
        [task waitUntilExit];
        int code = [task terminationStatus];

        dispatch_async(dispatch_get_main_queue(), ^{
            if (code == 0) {
                [self appendLine:@"\n\u2705  Done.\n"];
            } else {
                [self appendLine:[NSString stringWithFormat:
                    @"\n\u26A0\uFE0F   Script exited with code %d\n", code]];
            }
            self.scriptRunning = NO;
            [self setButtonsEnabled:YES];
        });
    });
}

// -----------------------------------------------------------------------------
// button actions
// -----------------------------------------------------------------------------

- (void)installClicked:(id)sender
{
    [self runScriptNamed:@"install"
                   title:@"Installing DAWalka"
                   extra:@[]];   // no --yes: install is non-interactive already
}

- (void)uninstallClicked:(id)sender
{
    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = @"Uninstall DAWalka?";
    confirm.informativeText = @"This removes the Audio Unit plugin, the Python venv, and (optionally) the downloaded models.\n\nYour generated audio files are kept by default.";
    [confirm addButtonWithTitle:@"Uninstall\u2026"];
    [confirm addButtonWithTitle:@"Cancel"];
    if ([confirm runModal] == NSAlertSecondButtonReturn) return;

    // Pass --all so uninstall.sh removes the plugin, the venv, the
    // downloaded models, and the cached settings/history in one shot.
    // We already confirmed via the NSAlert above.  The user's
    // generated WAVs in ~/Documents/DAWalka are ALWAYS kept by the
    // script (output-folder deletion is opt-in only) — the
    // informativeText above mentions that.
    [self runScriptNamed:@"uninstall"
                   title:@"Uninstalling DAWalka"
                   extra:@[@"--all"]];
}

- (void)revealClicked:(id)sender
{
    NSString *resDir = [[NSBundle mainBundle] resourcePath];
    [[NSWorkspace sharedWorkspace] selectFile:resDir
                     inFileViewerRootedAtPath:@""];
}

- (void)openDocsClicked:(id)sender
{
    NSString *readme = [[NSBundle mainBundle] pathForResource:@"README"
                                                       ofType:@"md"];
    if (readme) {
        NSURL *url = [NSURL fileURLWithPath:readme];
        [[NSWorkspace sharedWorkspace] openURL:url];
    } else {
        [[NSWorkspace sharedWorkspace] openURL:
            [NSURL URLWithString:@"https://github.com/pcixmix/DAWalka"]];  // fallback
    }
}

- (void)openGithubLink:(id)sender
{
    [[NSWorkspace sharedWorkspace]
        openURL:[NSURL URLWithString:@"https://github.com/pcixmix/DAWalka"]];
}

// NSTextViewDelegate — fires when the user clicks on a link in our
// GitHub text view.  NSLinkAttributeName on the attributed string is
// what makes the text view recognise the click; this method is what
// actually opens the URL.  Returning YES tells the text view the
// click was handled (so it doesn't try to do anything else with it).
- (BOOL)textView:(NSTextView *)textView
   clickedOnLink:(id)link
         atIndex:(NSUInteger)charIndex
{
    NSURL *url = nil;
    if ([link isKindOfClass:[NSURL class]]) {
        url = (NSURL *)link;
    } else if ([link isKindOfClass:[NSString class]]) {
        url = [NSURL URLWithString:(NSString *)link];
    }
    if (url) {
        [[NSWorkspace sharedWorkspace] openURL:url];
        return YES;
    }
    return NO;
}

// -----------------------------------------------------------------------------
// NSApplicationDelegate — build the window
// -----------------------------------------------------------------------------

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    // Activate as a regular GUI app (so we get a Dock icon and a real
    // window instead of a background-only process — we don't set
    // LSUIElement in Info.plist).
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];

    NSRect frame = NSMakeRect(0, 0, 580, 440);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled |
                                                         NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable)
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"DAWalka Installer";
    [self.window center];

    NSView *content = self.window.contentView;

    // Title ------------------------------------------------------------------
    NSTextField *title = [[NSTextField alloc]
        initWithFrame:NSMakeRect(20, 400, 540, 26)];
    title.stringValue = @"DAWalka \u2014 AI Audio Generator";
    title.font = [NSFont boldSystemFontOfSize:17];
    title.bezeled = NO;
    title.drawsBackground = NO;
    title.editable = NO;
    title.selectable = NO;
    [content addSubview:title];

    NSTextField *subtitle = [[NSTextField alloc]
        initWithFrame:NSMakeRect(20, 378, 540, 18)];
    subtitle.stringValue = @"Install or remove the Audio Unit plugin for Logic.";
    subtitle.textColor = [NSColor secondaryLabelColor];
    subtitle.font = [NSFont systemFontOfSize:11.5];
    subtitle.bezeled = NO;
    subtitle.drawsBackground = NO;
    subtitle.editable = NO;
    subtitle.selectable = NO;
    [content addSubview:subtitle];

    // GitHub link — single source for the latest version and a way
    // to support the author.  We use an NSTextView (not
    // NSTextField) with NSLinkAttributeName because NSTextView's
    // text storage actually fires the delegate's
    // textView:clickedOnLink:atIndex: when the user clicks a
    // link — NSTextField's cell swallows the click for caret
    // placement when selectable=YES, so target/action never runs.
    //
    // Visual: the URL itself is rendered underlined in the system
    // link colour; the trailing "  —  latest version & support the
    // author" sits next to it in a soft secondary colour, NOT
    // underlined, so it's clear what's clickable.  Left-aligned
    // to match the title and subtitle above (both at x=20, no
    // paragraph alignment), so the whole header column reads as
    // one tidy left-aligned block.
    NSTextView *githubLink = [[NSTextView alloc]
        initWithFrame:NSMakeRect(20, 346, 540, 24)];
    githubLink.editable = NO;
    // selectable=YES is required for NSTextView to detect and
    // dispatch link clicks.  editable=NO is required so the
    // text view never enters editing mode (which would otherwise
    // trap the click in a caret-insert loop).
    githubLink.selectable = YES;
    githubLink.drawsBackground = NO;
    // 5pt of vertical inset pushes the 11.5pt text down so it sits
    // visually centred in the 24pt-tall view (text glyph height is
    // ~13pt, 24-13=11, half=~5).
    githubLink.textContainerInset = NSMakeSize(0, 5);
    githubLink.textContainer.lineFragmentPadding = 0;
    // Lock the text view to a fixed size that matches our frame.
    // Without these, NSTextView's layout manager tries to size
    // itself to the text and ignores our frame.
    githubLink.horizontallyResizable = NO;
    githubLink.verticallyResizable = NO;
    githubLink.textContainer.widthTracksTextView = YES;
    githubLink.delegate = self;

    // Build the styled string in two ranges: the URL gets the link
    // attributes, the descriptive tail is plain secondary text.
    NSString *ghURLText = @"github.com/pcixmix/DAWalka";
    NSString *ghTail    = @"  \u2014  latest version & support the author";
    NSString *ghText    = [ghURLText stringByAppendingString:ghTail];
    NSMutableAttributedString *ghAttr =
        [[NSMutableAttributedString alloc] initWithString:ghText];
    NSRange urlRange = NSMakeRange(0, ghURLText.length);
    NSRange tailRange = NSMakeRange(ghURLText.length, ghTail.length);

    // URL part — link colour, underlined, system font
    [ghAttr addAttribute:NSFontAttributeName
                   value:[NSFont systemFontOfSize:11.5]
                   range:urlRange];
    [ghAttr addAttribute:NSForegroundColorAttributeName
                   value:[NSColor linkColor]
                   range:urlRange];
    [ghAttr addAttribute:NSUnderlineStyleAttributeName
                   value:@(NSUnderlineStyleSingle)
                   range:urlRange];
    [ghAttr addAttribute:NSLinkAttributeName
                   value:@"https://github.com/pcixmix/DAWalka"
                   range:urlRange];

    // Descriptive tail — same font, soft grey, no underline.
    // No NSLinkAttributeName here, so this text isn't clickable
    // and the hand-cursor only appears over the URL.
    [ghAttr addAttribute:NSFontAttributeName
                   value:[NSFont systemFontOfSize:11.5]
                   range:tailRange];
    [ghAttr addAttribute:NSForegroundColorAttributeName
                   value:[NSColor secondaryLabelColor]
                   range:tailRange];

    // Left-align to match the title and subtitle above (both
    // use the default NSTextField left-alignment at x=20 with
    // no paragraph style, so the whole header reads as a tidy
    // left-aligned column).
    NSMutableParagraphStyle *para = [[NSMutableParagraphStyle alloc] init];
    para.alignment = NSLeftTextAlignment;
    [ghAttr addAttribute:NSParagraphStyleAttributeName
                   value:para
                   range:NSMakeRange(0, ghText.length)];

    githubLink.textStorage.attributedString = ghAttr;
    [content addSubview:githubLink];

    // Buttons ----------------------------------------------------------------
    self.installButton = [[NSButton alloc]
        initWithFrame:NSMakeRect(20, 304, 170, 36)];
    self.installButton.title = @"Install";
    self.installButton.bezelStyle = NSBezelStyleRounded;
    self.installButton.target = self;
    self.installButton.action = @selector(installClicked:);
    self.installButton.keyEquivalent = @"\r";   // Enter triggers install by default
    [content addSubview:self.installButton];

    self.uninstallButton = [[NSButton alloc]
        initWithFrame:NSMakeRect(200, 304, 170, 36)];
    self.uninstallButton.title = @"Uninstall";
    self.uninstallButton.bezelStyle = NSBezelStyleRounded;
    self.uninstallButton.target = self;
    self.uninstallButton.action = @selector(uninstallClicked:);
    [content addSubview:self.uninstallButton];

    self.revealButton = [[NSButton alloc]
        initWithFrame:NSMakeRect(380, 304, 80, 36)];
    self.revealButton.title = @"Reveal";
    self.revealButton.bezelStyle = NSBezelStyleRounded;
    self.revealButton.target = self;
    self.revealButton.action = @selector(revealClicked:);
    self.revealButton.toolTip = @"Show DAWalka.app in Finder";
    [content addSubview:self.revealButton];

    NSButton *docsButton = [[NSButton alloc]
        initWithFrame:NSMakeRect(465, 304, 95, 36)];
    docsButton.title = @"README";
    docsButton.bezelStyle = NSBezelStyleRounded;
    docsButton.target = self;
    docsButton.action = @selector(openDocsClicked:);
    [content addSubview:docsButton];

    // Output view ------------------------------------------------------------
    NSScrollView *scroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(20, 20, 540, 270)];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    scroll.autohidesScrollers = NO;

    self.outputView = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0, 0, 540, 270)];
    self.outputView.editable = NO;
    self.outputView.selectable = YES;
    self.outputView.backgroundColor = [NSColor textBackgroundColor];
    self.outputView.textContainerInset = NSMakeSize(8, 8);
    self.outputView.font = [NSFont fontWithName:@"Menlo" size:11];

    scroll.documentView = self.outputView;
    [content addSubview:scroll];

    // Version footer — small print at the bottom-left of the window.
    // A separate label (not in the scroll view) so it stays put
    // even when the user scrolls the install output above it.
    NSTextField *versionLabel = [[NSTextField alloc]
        initWithFrame:NSMakeRect(20, 4, 200, 14)];
    versionLabel.stringValue = @"DAWalka v1.1.1";
    versionLabel.textColor = [NSColor tertiaryLabelColor];
    versionLabel.font = [NSFont systemFontOfSize:9.5];
    versionLabel.bezeled = NO;
    versionLabel.drawsBackground = NO;
    versionLabel.editable = NO;
    versionLabel.selectable = NO;
    [content addSubview:versionLabel];

    // Welcome text -----------------------------------------------------------
    [self appendLine:@"DAWalka Installer\n"];
    [self appendLine:@"=================\n\n"];
    [self appendLine:@"Click \xE2\x9C\x95 Install to set up DAWalka, or \xE2\x9C\x95 Uninstall to remove it.\n\n"];
    [self appendLine:@"Install does:\n"];
    [self appendLine:@"  1. Creates a Python virtual environment\n"];
    [self appendLine:@"  2. Installs MLX, sentencepiece, aiohttp, ...\n"];
    [self appendLine:@"  3. Downloads the Stable Audio 3 model weights (~6.7 GB)\n"];
    [self appendLine:@"  4. Copies the pre-built Audio Unit to ~/Library/Audio/Plug-Ins/Components/\n"];
    [self appendLine:@"  5. Verifies with auval\n\n"];
    [self appendLine:@"All output is shown below in real time.  If something fails,\n"];
    [self appendLine:@"you can copy the error from here for a bug report.\n\n"];
    [self appendLine:@"---\n"];
    [self appendLine:@"DAWalka v1.1.1\n"];

    [self.window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

@end

// -----------------------------------------------------------------------------
// entry point
// -----------------------------------------------------------------------------
int main(int argc, const char *argv[])
{
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
