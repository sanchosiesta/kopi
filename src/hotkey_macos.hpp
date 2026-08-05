#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

/// Register the global Cmd+8 hotkey.
Uint32 kopi_register_hotkey();
void   kopi_unregister_hotkey();

/// Show / hide the SDL window instantly (no Cocoa animation).
void kopi_show_window(SDL_Window* sdl_win);
void kopi_hide_window(SDL_Window* sdl_win);

/// Set clipboard, deactivate app, wait for focus transfer, then
/// simulate Cmd+V into the frontmost app.
/// Call AFTER hiding the window with kopi_hide_window.
void kopi_copy_and_paste(const char* text);

/// Paste without touching the clipboard (assumes clipboard already set).
void kopi_paste();

/// Create the macOS status bar icon (a bold 'k').  Click toggles the window.
void kopi_init_status_bar();

/// Remove the status bar icon (called at shutdown).
void kopi_remove_status_item();

/// Toggle launching at login (macOS 13+).  Returns new state.
bool kopi_toggle_login_item();

/// Query whether login item is currently registered.
bool kopi_is_login_item();

/// Get the native macOS pasteboard change count (for efficient polling).
int64_t kopi_get_clipboard_change_count();

/// Capture all MIME types from the pasteboard into parallel vectors.
/// Returns number of types captured.
int kopi_capture_mime(std::vector<std::string>& out_types,
                      std::vector<std::vector<uint8_t>>& out_data);

/// Restore MIME data to the pasteboard (for rich paste).
void kopi_restore_mime(const std::vector<std::string>& types,
                        const std::vector<std::vector<uint8_t>>& data);

/// Request Accessibility permission at startup (shows system dialog if needed).
void kopi_request_accessibility();
