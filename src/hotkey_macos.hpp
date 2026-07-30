#pragma once

#include <SDL3/SDL.h>

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

/// Create the macOS status bar icon (a bold 'k').  Click toggles the window.
void kopi_init_status_bar();

/// Remove the status bar icon (called at shutdown).
void kopi_remove_status_item();

/// Request Accessibility permission at startup (shows system dialog if needed).
void kopi_request_accessibility();
