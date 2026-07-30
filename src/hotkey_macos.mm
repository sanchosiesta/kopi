/// macOS global hotkey via Carbon Event Manager.
/// Registers Cmd+8 and posts a custom SDL event when pressed.
/// Also provides helpers to hide/show the app properly (returning focus),
/// and to simulate Cmd+V paste after copying.

#include "hotkey_macos.hpp"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <atomic>
#include <dlfcn.h>

static std::atomic<bool>        g_hotkey_ready{false};
static EventHotKeyRef           g_hotkey_ref  = nullptr;
static Uint32                   g_sdl_event   = 0;

// ============================================================================
// Paste simulation
// ============================================================================

static void simulate_paste()
{
    CGEventRef v_down = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)9, true);
    CGEventSetFlags(v_down, kCGEventFlagMaskCommand);
    CGEventRef v_up = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)9, false);

    CGEventPost(kCGSessionEventTap, v_down);
    usleep(5000);
    CGEventPost(kCGSessionEventTap, v_up);

    CFRelease(v_down);
    CFRelease(v_up);
    SDL_Log("kopi: Cmd+V posted");
}

// ============================================================================
// Carbon hotkey callback
// ============================================================================

static OSStatus hotkey_callback(EventHandlerCallRef, EventRef, void*)
{
    SDL_Event evt{};
    evt.type     = g_sdl_event;
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
// Window show/hide using SDL (no Cocoa animation)
// ============================================================================

/// Get the NSWindow backing an SDL window.
static NSWindow* get_nswindow(SDL_Window* sdl_win)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_win);
    return (__bridge NSWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}

void kopi_show_window(SDL_Window* sdl_win)
{
    @autoreleasepool {
        NSWindow* win = get_nswindow(sdl_win);
        if (!win) return;
        // Order front and make key — instant, no animation
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
}

void kopi_hide_window(SDL_Window* sdl_win)
{
    @autoreleasepool {
        NSWindow* win = get_nswindow(sdl_win);
        if (!win) return;
        // Order out instantly — no animation
        [win orderOut:nil];
    }
}

// ============================================================================
// Copy + paste flow (called after window is hidden)
// ============================================================================

void kopi_copy_and_paste(const char* text)
{
    @autoreleasepool {
        // 1. Set clipboard
        [NSPasteboard.generalPasteboard clearContents];
        [NSPasteboard.generalPasteboard setString:@(text)
                                          forType:NSPasteboardTypeString];

        // 2. Deactivate — focus returns to previous app
        [NSApp deactivate];

        // 3. Wait until our app is no longer frontmost (timeout 3 s)
        NSRunningApplication* ourApp = NSRunningApplication.currentApplication;
        for (int i = 0; i < 300; ++i) {
            usleep(10000);
            NSRunningApplication* front =
                [NSWorkspace.sharedWorkspace frontmostApplication];
            if (![front isEqual:ourApp]) break;
        }

        // 4. Paste into whichever app now has focus
        simulate_paste();
    }
}
