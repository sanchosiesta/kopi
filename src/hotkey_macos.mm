/// macOS global hotkey via Carbon Event Manager.
/// Registers Cmd+8 and posts a custom SDL event when pressed.
/// Handles instant window show/hide, active app tracking, and Cmd+V paste.

#include "hotkey_macos.hpp"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <atomic>

static std::atomic<bool> g_hotkey_ready{false};
static EventHotKeyRef    g_hotkey_ref  = nullptr;
static Uint32            g_sdl_event   = 0;

static pid_t             g_previous_pid = 0;

// ============================================================================
// Paste simulation
// ============================================================================

static void simulate_paste()
{
    if (!AXIsProcessTrusted()) {
        SDL_Log("kopi ERROR: Accessibility permission missing! "
                "Enable kopi (or your Terminal) under System Settings > "
                "Privacy & Security > Accessibility.");
        return;
    }

    // Private event source — uncouples from physical keyboard state
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!source) return;

    // Key code 9 = 'v' on ANSI layout
    CGEventRef v_down = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, true);
    CGEventSetFlags(v_down, kCGEventFlagMaskCommand);

    CGEventRef v_up = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, false);
    CGEventSetFlags(v_up, kCGEventFlagMaskCommand);

    // Post to HID level — treated identically to a physical key press
    CGEventPost(kCGHIDEventTap, v_down);
    usleep(20000);  // 20 ms press duration
    CGEventPost(kCGHIDEventTap, v_up);

    CFRelease(v_down);
    CFRelease(v_up);
    CFRelease(source);

    SDL_Log("kopi: Cmd+V posted");
}

// ============================================================================
// Carbon hotkey callback
// ============================================================================

static OSStatus hotkey_callback(EventHandlerCallRef, EventRef, void*)
{
    SDL_Event evt{};
    evt.type      = g_sdl_event;
    evt.user.code = 1;
    SDL_PushEvent(&evt);
    return noErr;
}

Uint32 kopi_register_hotkey()
{
    if (g_hotkey_ready) return g_sdl_event;

    g_sdl_event = SDL_RegisterEvents(1);
    if (g_sdl_event == 0) return 0;

    EventTypeSpec spec = {kEventClassKeyboard, kEventHotKeyPressed};
    InstallApplicationEventHandler(
        NewEventHandlerUPP(hotkey_callback), 1, &spec, nullptr, nullptr);

    EventHotKeyID hotkey_id = {'kopi', 0};
    OSStatus status = RegisterEventHotKey(
        28, cmdKey, hotkey_id,
        GetApplicationEventTarget(), 0, &g_hotkey_ref);

    if (status != noErr) {
        SDL_Log("kopi: RegisterEventHotKey failed (status %d)", status);
        return 0;
    }

    g_hotkey_ready = true;
    SDL_Log("kopi: global hotkey Cmd+8 registered");
    return g_sdl_event;
}

void kopi_unregister_hotkey()
{
    if (g_hotkey_ref) {
        UnregisterEventHotKey(g_hotkey_ref);
        g_hotkey_ref = nullptr;
    }
    g_hotkey_ready = false;
}

// ============================================================================
// Window show/hide
// ============================================================================

static NSWindow* get_nswindow(SDL_Window* sdl_win)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_win);
    return (__bridge NSWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}

void kopi_show_window(SDL_Window* sdl_win)
{
    @autoreleasepool {
        NSRunningApplication* current = NSWorkspace.sharedWorkspace.frontmostApplication;
        NSRunningApplication* ourApp  = NSRunningApplication.currentApplication;

        if (current && current.processIdentifier != ourApp.processIdentifier) {
            g_previous_pid = current.processIdentifier;
        }

        [NSApp activateIgnoringOtherApps:YES];

        NSWindow* win = get_nswindow(sdl_win);
        if (win) {
            [win makeKeyAndOrderFront:nil];
        }
    }
}

void kopi_hide_window(SDL_Window* sdl_win)
{
    @autoreleasepool {
        NSWindow* win = get_nswindow(sdl_win);
        if (win) {
            [win orderOut:nil];
        }

        if (g_previous_pid != 0) {
            NSRunningApplication* prev =
                [NSRunningApplication runningApplicationWithProcessIdentifier:g_previous_pid];
            if (prev) {
                [prev activateWithOptions:NSApplicationActivateIgnoringOtherApps];
            } else {
                [NSApp hide:nil];
            }
        } else {
            [NSApp hide:nil];
        }
    }
}

// ============================================================================
// Copy + paste
// ============================================================================

void kopi_copy_and_paste(const char* text)
{
    @autoreleasepool {
        [NSPasteboard.generalPasteboard clearContents];
        [NSPasteboard.generalPasteboard setString:@(text)
                                          forType:NSPasteboardTypeString];
    }

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @autoreleasepool {
            // Wait deterministically for kopi to lose the active state
            for (int i = 0; i < 50; ++i) {
                if (![NSApp isActive]) {
                    break;
                }
                usleep(10000);
            }

            usleep(50000);
            simulate_paste();
        }
    });
}
