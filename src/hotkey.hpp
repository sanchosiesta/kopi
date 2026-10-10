#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// Platform abstraction for kopi.
//
// macOS build:  src/hotkey_macos.mm   (Carbon hotkey, AppKit status item,
//                                       CGEvent paste, NSPasteboard clipboard)
// Linux build:  src/hotkey_linux.cpp  (X11/XTest hotkey, XEmbed tray icon,
//                                      XTest paste, SDL3 clipboard)
// ============================================================================

/// Register the global summon hotkey (Cmd+8 on macOS, Ctrl+8 on Linux).
Uint32 kopi_register_hotkey();
void   kopi_unregister_hotkey();

/// Show / hide the SDL window instantly.
void kopi_show_window(SDL_Window* sdl_win);
void kopi_hide_window(SDL_Window* sdl_win);

/// Set clipboard, return focus to the previous window, then simulate paste.
/// Call AFTER hiding the window with kopi_hide_window.
void kopi_copy_and_paste(const char* text);

/// Paste without touching the clipboard (assumes clipboard already set).
void kopi_paste();

/// Create the status-bar / tray icon (a bold 'k').  Click toggles the window.
void kopi_init_status_bar();

/// Remove the status-bar / tray icon (called at shutdown).
void kopi_remove_status_item();

/// Toggle launching at login.  Returns new state.
bool kopi_toggle_login_item();

/// Query whether login item is currently registered.
bool kopi_is_login_item();

/// Get the clipboard change count (for efficient polling).
int64_t kopi_get_clipboard_change_count();

/// Capture all MIME types from the clipboard into parallel vectors.
/// Returns number of types captured.
int kopi_capture_mime(std::vector<std::string>& out_types,
                      std::vector<std::vector<uint8_t>>& out_data);

/// Restore MIME data to the clipboard (for rich paste).
void kopi_restore_mime(const std::vector<std::string>& types,
                       const std::vector<std::vector<uint8_t>>& data);

/// Request any platform permissions needed at startup.
void kopi_request_accessibility();
