/// Linux platform layer for kopi.
///
/// This is the Linux counterpart to hotkey_macos.mm.  It provides the same
/// interface via `hotkey.hpp`, but implemented with native X11 idioms:
///
///   - Global hotkey  -> XGrabKey (system-wide Alt+8), polled on a
///                       background thread and forwarded as an SDL event.
///   - Window show/hide -> SDL_ShowWindow / SDL_HideWindow, plus X input
///                         focus save/restore so paste lands back in the
///                         user's previous window.
///   - Paste          -> XTestFakeKeyEvent (Ctrl+V) via the XTest extension.
///   - Tray icon      -> raw X11 XEmbed client ("k"), left-click toggles the
///                       window, right-click quits.
///   - Login item     -> a systemd autostart desktop entry
///                       (~/.config/autostart/kopi.desktop).
///   - Clipboard      -> SDL3's clipboard API (text + full MIME support).
///
/// No Cocoa/Carbon code is reused; this file is self-contained.

#include "hotkey.hpp"

#include <SDL3/SDL.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

// ============================================================================
// Shared globals (guarded by atomics; SDL_Log/PushEvent are thread-safe)
// ============================================================================

static Uint32                 g_sdl_event = 0;
static std::atomic<bool>      g_hotkey_running{false};
static std::atomic<bool>      g_tray_running{false};
static std::thread            g_hotkey_thread;
static std::thread            g_tray_thread;

/// The X window that had input focus before kopi took it.  Used to hand
/// focus back before simulating a paste.  Only touched from the main thread
/// (show/hide/paste are all called from main.cpp on the SDL thread).
static Window                g_prev_focus = 0;

/// Xlib error handler: XGrabKey / docking can return BadAccess if another
/// client already owns the key or tray slot.  We never want those to abort
/// the app, so swallow them all (but log them for diagnosis).
static int x_error_handler(Display* dpy, XErrorEvent* ev) {
    char buf[256] = "";
    XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
    SDL_Log("kopi: X error ignored: %s (request=%d.%d resource=0x%lx)",
            buf, ev->request_code, ev->minor_code, ev->resourceid);
    return 0;
}

// ============================================================================
// Global hotkey (Alt+8) via XGrabKey
// ============================================================================

/// Grab a key/modifier combo (plus caps/num-lock variants) on the root
/// window so the OS routes KeyPress events to us system-wide.
static void grab_key_combos(Display* dpy, KeyCode key, unsigned int mods,
                            Window root, bool grab) {
    // Cover the usual lock-key combinations so Alt+8 works regardless of
    // Caps Lock / Num Lock state.
    static const unsigned int extra[] = { 0u, LockMask, Mod2Mask,
                                          LockMask | Mod2Mask };
    for (unsigned int m : extra) {
        if (grab)
            XGrabKey(dpy, key, mods | m, root, True,
                     GrabModeAsync, GrabModeAsync);
        else
            XUngrabKey(dpy, key, mods | m, root);
    }
}

Uint32 kopi_register_hotkey() {
    if (g_sdl_event == 0) {
        g_sdl_event = SDL_RegisterEvents(1);
        if (g_sdl_event == 0) return 0;
    }
    // exchange(true) returns the previous value; if a thread is already
    // running we just hand back the registered event type.
    if (g_hotkey_running.exchange(true)) return g_sdl_event;

    g_hotkey_thread = std::thread([]() {
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) {
            SDL_Log("kopi: could not open X display for global hotkey");
            g_hotkey_running = false;
            return;
        }
        XSetErrorHandler(x_error_handler);
        const int screen = DefaultScreen(dpy);
        const Window root = RootWindow(dpy, screen);
        const KeyCode key = XKeysymToKeycode(dpy, XK_8);
        grab_key_combos(dpy, key, Mod1Mask, root, true);   // Mod1 = Alt
        XFlush(dpy);
        SDL_Log("kopi: global hotkey Alt+8 registered");

        XEvent ev;
        while (g_hotkey_running) {
            while (XPending(dpy)) {
                XNextEvent(dpy, &ev);
                if (ev.type == KeyPress && ev.xkey.keycode == key) {
                    SDL_Event sdl_ev{};
                    sdl_ev.type      = g_sdl_event;
                    sdl_ev.user.code = 1;
                    SDL_PushEvent(&sdl_ev);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        grab_key_combos(dpy, key, Mod1Mask, root, false);   // Mod1 = Alt
        XCloseDisplay(dpy);
        g_hotkey_running = false;
    });

    return g_sdl_event;
}

void kopi_unregister_hotkey() {
    if (g_hotkey_running.exchange(false) && g_hotkey_thread.joinable())
        g_hotkey_thread.join();
}

// ============================================================================
// Window show / hide (with input-focus save/restore)
// ============================================================================

void kopi_show_window(SDL_Window* sdl_win) {
    // Record the window the user was working in before we take focus, so we
    // can hand it back on hide and paste into it.
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy) {
        Window focus = None;
        int revert = 0;
        XGetInputFocus(dpy, &focus, &revert);
        if (focus != None && focus != PointerRoot)
            g_prev_focus = focus;
        XCloseDisplay(dpy);
    }

    SDL_ShowWindow(sdl_win);
    SDL_RaiseWindow(sdl_win);
}

void kopi_hide_window(SDL_Window* sdl_win) {
    SDL_HideWindow(sdl_win);

    // Return focus to the previous window so a paste lands there.
    if (g_prev_focus) {
        Display* dpy = XOpenDisplay(nullptr);
        if (dpy) {
            XSetInputFocus(dpy, g_prev_focus, RevertToParent, CurrentTime);
            XFlush(dpy);
            XCloseDisplay(dpy);
        }
    }
}

// ============================================================================
// Paste simulation via XTest
// ============================================================================

void kopi_copy_and_paste(const char* text) {
    SDL_SetClipboardText(text);
    kopi_paste();
}

void kopi_paste() {
    // Called from the SDL main thread right after kopi_hide_window, which has
    // already restored input focus.  XTestFakeKeyEvent posts hardware-level
    // key events into the focused window.
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return;

    // Give the target a beat to process the focus change / window unmap.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    int evt = 0, err = 0, major = 0, minor = 0;
    if (XTestQueryExtension(dpy, &evt, &err, &major, &minor)) {
        const KeyCode ctrl = XKeysymToKeycode(dpy, XK_Control_L);
        const KeyCode v    = XKeysymToKeycode(dpy, XK_v);
        XTestFakeKeyEvent(dpy, ctrl, True,  CurrentTime);
        XTestFakeKeyEvent(dpy, v,    True,  CurrentTime);
        XTestFakeKeyEvent(dpy, v,    False, CurrentTime);
        XTestFakeKeyEvent(dpy, ctrl, False, CurrentTime);
        XFlush(dpy);
        SDL_Log("kopi: Ctrl+V posted");
    } else {
        SDL_Log("kopi WARNING: XTest extension unavailable — paste disabled");
    }

    XCloseDisplay(dpy);
}

// ============================================================================
// System tray icon (minimal X11 XEmbed client)
// ============================================================================

void kopi_init_status_bar() {
    if (g_tray_running.exchange(true)) return;  // already running

    g_tray_thread = std::thread([]() {
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) {
            SDL_Log("kopi: could not open X display for tray icon");
            g_tray_running = false;
            return;
        }
        XSetErrorHandler(x_error_handler);
        const int screen = DefaultScreen(dpy);
        const Window root = RootWindow(dpy, screen);

        // The tray is the current owner of the _NET_SYSTEM_TRAY_S<screen>
        // selection.  If no tray is running, skip gracefully (the hotkey
        // still works).
        char sel_name[64];
        std::snprintf(sel_name, sizeof(sel_name), "_NET_SYSTEM_TRAY_S%d", screen);
        const Atom tray_sel = XInternAtom(dpy, sel_name, False);
        const Window tray = XGetSelectionOwner(dpy, tray_sel);
        if (tray == None) {
            SDL_Log("kopi: no system tray found — tray icon skipped "
                    "(hotkey still works)");
            XCloseDisplay(dpy);
            g_tray_running = false;
            return;
        }

        // The visual the tray would like icons to use.  On composited
        // desktops (xfce's systray with compositing) the tray hosts icons in
        // a 32-bit ARGB socket and advertises that here.  A 24-bit icon is
        // docked but left invisible in such an ARGB socket, so we create our
        // icon with the tray's visual whenever it is offered.
        Visual* tray_visual = nullptr;
        int tray_depth = 0;
        {
            const Atom vs = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
            Atom type = 0; int fmt = 0;
            unsigned long nitems = 0, bytes = 0;
            unsigned char* prop = nullptr;
            if (XGetWindowProperty(dpy, tray, vs, 0, 1, False, XA_VISUALID,
                                   &type, &fmt, &nitems, &bytes, &prop) == 0) {
                if (prop && fmt == 32) {
                    unsigned long vid = *reinterpret_cast<unsigned long*>(prop);
                    int nvis = 0;
                    XVisualInfo tmpl; tmpl.visualid = vid;
                    XVisualInfo* vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &nvis);
                    if (vi && nvis > 0) {
                        tray_visual = vi[0].visual;
                        tray_depth = vi[0].depth;
                        XFree(vi);
                    }
                }
                if (prop) XFree(prop);
            }
        }

        // 24x24 icon window.  Use a light-gray background so the black "k"
        // stays visible whatever the tray's theme.  Prefer the tray's
        // advertised visual (ARGB) so the icon matches the tray's socket;
        // fall back to the root visual on trays that advertise none or on
        // servers that cannot host an ARGB child of the 24-bit root.
        XSetWindowAttributes attrs;
        std::memset(&attrs, 0, sizeof(attrs));
        attrs.event_mask       = ButtonPressMask | StructureNotifyMask |
                                ExposureMask;
        attrs.background_pixel = 0xE0E0E0;
        Window icon = 0;
        bool used_tray_visual = false;
        if (tray_visual && tray_depth != 0) {
            icon = XCreateWindow(
                dpy, root, 0, 0, 24, 24,
                0, tray_depth, InputOutput, tray_visual,
                CWEventMask | CWBackPixel, &attrs);
            XSync(dpy, False);   // surface any BadMatch (no compositor)
            Window r, p; Window* kids = nullptr; unsigned int n = 0;
            if (XQueryTree(dpy, icon, &r, &p, &kids, &n))
                used_tray_visual = true;
            if (kids) XFree(kids);
            if (!used_tray_visual)
                icon = 0;   // create failed — fall back below
        }
        if (!icon) {
            icon = XCreateWindow(
                dpy, root, 0, 0, 24, 24,
                0, CopyFromParent, InputOutput, CopyFromParent,
                CWEventMask | CWBackPixel, &attrs);
        }
        if (used_tray_visual) {
            SDL_Log("kopi: tray icon uses the tray's %d-bit visual", tray_depth);
        } else if (tray_visual && tray_depth != 0) {
            SDL_Log("kopi WARNING: tray wants an ARGB (%d-bit) icon but the "
                    "server cannot host one under the %d-bit root; "
                    "the tray icon may not appear", tray_depth,
                    DefaultDepth(dpy, screen));
        } else {
            SDL_Log("kopi: tray icon uses the root visual");
        }

        // _XEMBED_INFO = { version 0, mapped 1 }.  Per the XEmbed convention
        // the property TYPE is the _XEMBED_INFO atom itself.
        const Atom xembed = XInternAtom(dpy, "_XEMBED_INFO", False);
        const long embed_info[2] = { 0, 1 };
        XChangeProperty(dpy, icon, xembed, xembed, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(embed_info), 2);

        // Map the icon before requesting docking — GtkSocket (used by the
        // xfce tray) embeds a mapped client window.
        XMapWindow(dpy, icon);
        XFlush(dpy);

        // Ask the tray to dock us: _NET_SYSTEM_TRAY_OPCODE with the
        // SYSTEM_TRAY_REQUEST_DOCK (0) message.  Protocol data layout is
        // { time, message, window }.
        const Atom opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
        XEvent dock;
        std::memset(&dock, 0, sizeof(dock));
        dock.xclient.type         = ClientMessage;
        dock.xclient.window       = tray;
        dock.xclient.message_type = opcode;
        dock.xclient.format       = 32;
        dock.xclient.data.l[0]    = 0;             // time
        dock.xclient.data.l[1]    = 0;             // SYSTEM_TRAY_REQUEST_DOCK
        dock.xclient.data.l[2]    = icon;         // window to dock
        dock.xclient.data.l[3]    = 0;
        dock.xclient.data.l[4]    = 0;
        XSendEvent(dpy, tray, False, NoEventMask, &dock);
        XFlush(dpy);
        SDL_Log("kopi: tray icon requested docking");

        // GC + font for the "k".  The tray reparents and maps the icon after
        // docking; we paint it on Expose.
        GC gc = XCreateGC(dpy, icon, 0, nullptr);
        XSetForeground(dpy, gc, 0x000000);
        XFontStruct* font = XLoadQueryFont(dpy, "fixed");
        if (font)
            XSetFont(dpy, gc, font->fid);

        auto draw_k = [&]() {
            if (font)
                XDrawString(dpy, icon, gc, 4, 16, "k", 1);
            XFlush(dpy);
        };

        while (g_tray_running) {
            while (XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                if (ev.type == ButtonPress) {
                    XButtonEvent* b = &ev.xbutton;
                    if (b->button == Button1) {
                        // Left-click: toggle the window.
                        SDL_Event sdl_ev{};
                        sdl_ev.type      = g_sdl_event;
                        sdl_ev.user.code = 1;
                        SDL_PushEvent(&sdl_ev);
                    } else if (b->button == Button3) {
                        // Right-click: quit.
                        SDL_Event q{};
                        q.type = SDL_EVENT_QUIT;
                        SDL_PushEvent(&q);
                    }
                } else if (ev.type == Expose) {
                    XClearWindow(dpy, icon);
                    draw_k();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (font)
            XFreeFont(dpy, font);
        XDestroyWindow(dpy, icon);
        XCloseDisplay(dpy);
        g_tray_running = false;
    });
}

void kopi_remove_status_item() {
    if (g_tray_running.exchange(false) && g_tray_thread.joinable())
        g_tray_thread.join();
}

// ============================================================================
// Launch at login (systemd autostart desktop entry)
// ============================================================================

static std::filesystem::path autostart_file() {
    std::filesystem::path dir;
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    if (cfg && *cfg) {
        dir = std::filesystem::path(cfg) / "autostart";
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home)
            dir = std::filesystem::path(home) / ".config/autostart";
        else
            dir = std::filesystem::path(".") / "autostart";
    }
    return dir / "kopi.desktop";
}

bool kopi_is_login_item() {
    return std::filesystem::exists(autostart_file());
}

bool kopi_toggle_login_item() {
    auto file = autostart_file();
    if (kopi_is_login_item()) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        SDL_Log("kopi: launch at login = OFF");
    } else {
        // Resolve the running binary so the desktop entry starts the real
        // kopi, wherever it lives.
        std::string exe = "kopi";
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            exe = buf;
        }

        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream out(file);
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=kopi clipboard manager\n"
            << "Exec=" << exe << "\n"
            << "Comment=Start kopi at login\n"
            << "X-GNOME-Autostart-enabled=true\n";
        SDL_Log("kopi: launch at login = ON (%s)", file.c_str());
    }

    bool enabled = kopi_is_login_item();
    SDL_Log("kopi: launch at login = %s", enabled ? "ON" : "OFF");
    return enabled;
}

// ============================================================================
// Clipboard change detection
// ============================================================================

/// X11 has no clipboard change-count API, so we approximate it with an FNV-1a
/// hash of the current clipboard text.  This is correct (any content change
/// flips the hash) at the cost of reading the clipboard each poll — which is
/// what X11 clipboard managers do anyway.
int64_t kopi_get_clipboard_change_count() {
    char* raw = SDL_GetClipboardText();
    if (!raw) return 0;
    uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
    for (char* p = raw; *p; ++p) {
        h ^= static_cast<uint8_t>(*p);
        h *= 1099511628211ULL;            // FNV-1a prime
    }
    SDL_free(raw);
    return static_cast<int64_t>(h);
}

// ============================================================================
// MIME capture / restore (SDL3 clipboard API)
// ============================================================================

int kopi_capture_mime(std::vector<std::string>& out_types,
                      std::vector<std::vector<uint8_t>>& out_data) {
    out_types.clear();
    out_data.clear();

    size_t num = 0;
    char** types = SDL_GetClipboardMimeTypes(&num);
    if (!types) return 0;

    for (size_t i = 0; i < num; ++i) {
        size_t sz = 0;
        void* data = SDL_GetClipboardData(types[i], &sz);
        if (!data || sz == 0) continue;
        out_types.push_back(types[i]);
        auto& bytes = out_data.emplace_back();
        bytes.assign(static_cast<uint8_t*>(data),
                     static_cast<uint8_t*>(data) + sz);
        SDL_free(data);
    }

    SDL_free(types);
    return static_cast<int>(out_types.size());
}

/// Data source handed to SDL for lazy clipboard serving (so rich types like
/// HTML/RTF/images are only serialized when actually requested).
struct MimeClipboard {
    std::vector<std::string> types;
    std::vector<std::vector<uint8_t>> data;
};

static const void* SDLCALL kopi_mime_cb(void* userdata, const char* mime_type,
                                        size_t* size) {
    auto* mc = static_cast<MimeClipboard*>(userdata);
    for (size_t i = 0; i < mc->types.size(); ++i) {
        if (mc->types[i] == mime_type) {
            *size = mc->data[i].size();
            return mc->data[i].data();
        }
    }
    *size = 0;
    return nullptr;
}

static void SDLCALL kopi_mime_cleanup_cb(void* userdata) {
    delete static_cast<MimeClipboard*>(userdata);
}

void kopi_restore_mime(const std::vector<std::string>& types,
                       const std::vector<std::vector<uint8_t>>& data) {
    auto* mc = new MimeClipboard{ types, data };

    std::vector<const char*> mime_types;
    mime_types.reserve(types.size());
    for (auto& t : types) mime_types.push_back(t.c_str());

    if (!SDL_SetClipboardData(kopi_mime_cb, kopi_mime_cleanup_cb, mc,
                              mime_types.data(), mime_types.size())) {
        delete mc;
        SDL_Log("kopi ERROR: SDL_SetClipboardData failed: %s", SDL_GetError());
    }
}

// ============================================================================
// Permissions
// ============================================================================

void kopi_request_accessibility() {
    SDL_Log("kopi: Linux — no Accessibility permission required "
            "(paste uses the XTest extension)");
}
