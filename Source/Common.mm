#include "Common.h"
#import <Cocoa/Cocoa.h>
#include <cstdio>
#include <functional>

namespace dawalka {

juce::String makeUuid()
{
    return juce::Uuid().toString();
}

namespace {

class StandaloneWindowFixer : public juce::Timer
{
public:
    explicit StandaloneWindowFixer (juce::Component* editor)
        : editor_ (editor)
    {
        startTimerHz (10); // 10 раз в секунду
    }

    void timerCallback() override
    {
        if (!editor_)
        {
            stopTimer();
            return;
        }

        // Ждём пока окно появится на рабочем столе
        if (!editor_->isOnDesktop())
            return;

        std::fprintf(stderr, "DAWalka: Editor is now on desktop\n");
        fflush(stderr);

        // Walk up the parent chain to find our TopLevelWindow / DocumentWindow.
        juce::Component* c = editor_;
        juce::TopLevelWindow* tlw = nullptr;
        while (c)
        {
            if (auto* tw = dynamic_cast<juce::TopLevelWindow*>(c))
            {
                tlw = tw;
                break;
            }
            c = c->getParentComponent();
        }

        if (!tlw) { std::fprintf(stderr, "DAWalka: no TopLevelWindow found\n"); fflush(stderr); stopTimer(); return; }

        // ── Force native title bar with traffic-light buttons ──────────────
        tlw->setUsingNativeTitleBar (true);

        // Also reach down to the NSWindow level via Objective-C.
        @autoreleasepool {
            NSView* nsView = (NSView*)(uintptr_t) tlw->getWindowHandle();
            if (!nsView)
                nsView = (NSView*)(uintptr_t) editor_->getWindowHandle();

            if (nsView && [nsView respondsToSelector:@selector(window)])
            {
                NSWindow* nsWin = [nsView window];
                if (nsWin)
                {
                    [[nsWin standardWindowButton:NSWindowCloseButton] setHidden:NO];
                    [[nsWin standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
                    [[nsWin standardWindowButton:NSWindowZoomButton] setHidden:NO];

                    nsWin.titlebarAppearsTransparent = NO;
                    nsWin.titleVisibility = NSWindowTitleVisible;

                    std::fprintf(stderr, "DAWalka: NSWindow configured\n");
                    fflush(stderr);
                }
            }
        }

        // ── Hide "Audio input is muted" info bar ───────────────────────────
        int hiddenCount = 0;
        std::function<void(juce::Component*, juce::Component*)> tryHideRecursive =
            [&] (juce::Component* root, juce::Component* skip)
        {
            if (!root || root == skip) return;

            const int h = root->getHeight();
            const int w = root->getWidth();
            // Info bar: ~24px tall, full width.
            if (h > 15 && h < 40 && w > 300)
            {
                std::fprintf(stderr, "DAWalka: Hiding info bar component: %dx%d\n", w, h);
                fflush(stderr);
                root->setVisible (false);
                hiddenCount++;
                return; // no need to recurse into hidden component
            }

            for (int i = root->getNumChildComponents() - 1; i >= 0; --i)
            {
                tryHideRecursive(root->getChildComponent(i), skip);
            }
        };

        if (auto* parent = editor_->getParentComponent())
            tryHideRecursive(parent, nullptr);
        tryHideRecursive(tlw, nullptr);

        std::fprintf(stderr, "DAWalka: Hidden %d info bars\n", hiddenCount);
        fflush(stderr);

        // Keep scanning for up to 10 seconds to catch late-appearing info bars
        if (startTime_ == 0)
            startTime_ = juce::Time::getMillisecondCounter();

        const int64_t elapsed = juce::Time::getMillisecondCounter() - startTime_;
        if (elapsed > 10000)
        {
            stopTimer();
        }
    }

private:
    juce::Component* editor_;
    int64_t startTime_ = 0;
};

} // namespace

void fixStandaloneWindow (juce::Component* editor)
{
#if !JucePlugin_Build_Standalone
    return;
#endif

    if (!editor) return;

    std::fprintf(stderr, "DAWalka: fixStandaloneWindow called\n");
    fflush(stderr);

    // Используем таймер чтобы окно успело появиться на рабочем столе
    new StandaloneWindowFixer (editor);
}

} // namespace dawalka