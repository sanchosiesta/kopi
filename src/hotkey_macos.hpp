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

/// Create a menu bar icon (NSStatusItem).  Click toggles the window.
void kopi_create_status_item(SDL_Window* sdl_win);

/// Remove the menu bar icon (called at shutdown).
void kopi_remove_status_item();
