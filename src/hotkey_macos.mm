/// macOS global hotkey via Carbon Event Manager.
/// Registers Cmd+8 and posts a custom SDL event when pressed.
/// Handles instant window show/hide, active app tracking, and Cmd+V paste.

#include "hotkey.hpp"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#include <atomic>

static std::atomic<bool> g_hotkey_ready{false};
static EventHotKeyRef    g_hotkey_ref  = nullptr;
static Uint32            g_sdl_event   = 0;

static pid_t             g_previous_pid = 0;

// ============================================================================
// Menu bar status item
// ============================================================================

@interface KopiMenuTarget : NSObject
- (void)toggleWindow:(id)sender;
- (void)quitApp:(id)sender;
- (void)toggleLoginItem:(id)sender;
@end

@implementation KopiMenuTarget
- (void)toggleWindow:(id)sender {
    (void)sender;
    SDL_Event evt{};
    evt.type      = g_sdl_event;
    evt.user.code = 1;
    SDL_PushEvent(&evt);
}
- (void)quitApp:(id)sender {
    (void)sender;
    SDL_Event evt{};
    evt.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&evt);
}
- (void)toggleLoginItem:(id)sender {
    (void)sender;
    kopi_toggle_login_item();
}
@end

static NSStatusItem*   g_status_item = nil;
static KopiMenuTarget* g_menu_target = nil;

void kopi_init_status_bar()
{
    @autoreleasepool {
        if (g_status_item) return;

        g_menu_target = [[KopiMenuTarget alloc] init];
        // Hard retain — survives autorelease pool regardless of ARC/CMake caching
        CFRetain((__bridge CFTypeRef)g_menu_target);

        g_status_item = [NSStatusBar.systemStatusBar statusItemWithLength:28.0];
        CFRetain((__bridge CFTypeRef)g_status_item);

        g_status_item.button.title = @"k";
        g_status_item.button.font = [NSFont boldSystemFontOfSize:14.0];

        // Left-click: toggle window
        g_status_item.button.action = @selector(toggleWindow:);
        g_status_item.button.target = g_menu_target;

        // Dropdown menu
        NSMenu* menu = [[NSMenu alloc] init];

        NSMenuItem* loginItem = [[NSMenuItem alloc]
            initWithTitle:@"Launch at Login"
                   action:@selector(toggleLoginItem:)
            keyEquivalent:@""];
        loginItem.target = g_menu_target;
        loginItem.state = kopi_is_login_item() ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:loginItem];

        [menu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* quitItem = [[NSMenuItem alloc]
            initWithTitle:@"Quit kopi"
                   action:@selector(quitApp:)
            keyEquivalent:@"q"];
        quitItem.target = g_menu_target;
        [menu addItem:quitItem];

        g_status_item.menu = menu;
        g_status_item.visible = YES;
    }

    SDL_Log("kopi: status bar icon created");
}

void kopi_remove_status_item()
{
    if (g_status_item) {
        [NSStatusBar.systemStatusBar removeStatusItem:g_status_item];
        CFRelease((__bridge CFTypeRef)g_status_item);
        g_status_item = nil;
    }
    if (g_menu_target) {
        CFRelease((__bridge CFTypeRef)g_menu_target);
        g_menu_target = nil;
    }
}

// ============================================================================
// Accessibility permission
// ============================================================================

void kopi_request_accessibility()
{
    NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
    bool trusted = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
    if (!trusted) {
        SDL_Log("kopi WARNING: Accessibility permissions missing. Prompting user...");
    } else {
        SDL_Log("kopi: Accessibility permission granted");
    }
}

// ============================================================================
// Paste simulation
// ============================================================================

static void simulate_paste()
{
    if (!AXIsProcessTrusted()) {
        SDL_Log("kopi ERROR: Accessibility permission missing! "
                "Add kopi.app to System Settings > Privacy & Security > Accessibility.");
        return;
    }

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!source) return;

    CGEventRef v_down = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, true);
    CGEventSetFlags(v_down, kCGEventFlagMaskCommand);

    CGEventRef v_up = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, false);
    CGEventSetFlags(v_up, kCGEventFlagMaskCommand);

    CGEventPost(kCGHIDEventTap, v_down);
    usleep(20000);
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

// ============================================================================
// Login item (auto-start with system) — macOS 13+
// ============================================================================

bool kopi_is_login_item()
{
    @autoreleasepool {
        SMAppService* service = SMAppService.mainAppService;
        return service.status == SMAppServiceStatusEnabled;
    }
}

bool kopi_toggle_login_item()
{
    @autoreleasepool {
        SMAppService* service = SMAppService.mainAppService;
        NSError* error = nil;

        if (service.status == SMAppServiceStatusEnabled) {
            [service unregisterAndReturnError:&error];
        } else {
            [service registerAndReturnError:&error];
        }

        if (error) {
            SDL_Log("kopi: login item toggle error: %s",
                    error.localizedDescription.UTF8String);
        }

        bool enabled = (service.status == SMAppServiceStatusEnabled);
        SDL_Log("kopi: launch at login = %s", enabled ? "ON" : "OFF");
        return enabled;
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
    kopi_paste();
}

void kopi_paste()
{
    // Capture our PID on the main thread before dispatching.
    pid_t ourPid = NSProcessInfo.processInfo.processIdentifier;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @autoreleasepool {
            // Poll NSWorkspace (thread-safe for reading) until we're
            // no longer the frontmost app — focus has transferred.
            for (int i = 0; i < 50; ++i) {
                NSRunningApplication* front =
                    NSWorkspace.sharedWorkspace.frontmostApplication;
                if (front && front.processIdentifier != ourPid) break;
                usleep(10000);
            }
            // Extra buffer for the editor to be ready for input
            usleep(50000);
            simulate_paste();
        }
    });
}

// ============================================================================
// Clipboard Optimization
// ============================================================================

int64_t kopi_get_clipboard_change_count()
{
    @autoreleasepool {
        return [NSPasteboard.generalPasteboard changeCount];
    }
}

// ============================================================================
// MIME capture / restore
// ============================================================================

int kopi_capture_mime(std::vector<std::string>& out_types,
                      std::vector<std::vector<uint8_t>>& out_data)
{
    @autoreleasepool {
        NSPasteboard* pb = NSPasteboard.generalPasteboard;
        NSArray<NSPasteboardType>* types = pb.types;

        out_types.clear();
        out_data.clear();

        for (NSPasteboardType type in types) {
            NSData* nsdata = [pb dataForType:type];
            if (!nsdata) continue;

            out_types.push_back(type.UTF8String);
            const uint8_t* bytes = (const uint8_t*)nsdata.bytes;
            out_data.emplace_back(bytes, bytes + nsdata.length);
        }

        return (int)out_types.size();
    }
}

void kopi_restore_mime(const std::vector<std::string>& types,
                        const std::vector<std::vector<uint8_t>>& data)
{
    @autoreleasepool {
        NSPasteboard* pb = NSPasteboard.generalPasteboard;
        [pb clearContents];

        size_t n = std::min(types.size(), data.size());
        for (size_t i = 0; i < n; ++i) {
            NSData* nsdata = [NSData dataWithBytes:data[i].data()
                                            length:data[i].size()];
            [pb setData:nsdata forType:@(types[i].c_str())];
        }
    }
}
