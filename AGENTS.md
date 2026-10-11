# AGENTS.md

Guidance for AI coding agents (e.g. Pi) working on this repository.

## What this is

`kopi` is a graphical clipboard manager with vim keybindings, written in
C++23.  It runs on **macOS** (Carbon/AppKit/CGEvent) and **Linux/X11**
(Xlib/XTest/XEmbed).  UI is Dear ImGui 1.91 rendered through SDL3.

Summon it with a global hotkey (**Cmd+8** macOS, **Alt+8** Linux), navigate
with vim keys, pick an entry — it hides, returns focus, and simulates the
paste shortcut.

## Architecture

Platform-specific code is isolated behind a shared interface so the UI
(`main.cpp`) and history/clipboard logic (`kopi.cpp`) are written once:

```
src/
  hotkey.hpp        <- shared interface (declarations only)
  hotkey_macos.mm   <- macOS implementation  (Objective-C++, ARC)
  hotkey_linux.cpp  <- Linux implementation  (X11/XTest, plain C++)
  kopi.hpp / kopi.cpp   <- history, dedup, search, multi-select (cross-platform)
  main.cpp          <- SDL3 + ImGui UI, event loop, vim keys (cross-platform)
packaging/kopi.spec + build-rpm.sh   <- Fedora/RHEL RPM build
```

The interface in `hotkey.hpp` is the contract.  Add a platform feature by
declaring it there, then implement it in *both* `hotkey_macos.mm` and
`hotkey_linux.cpp`.  Do **not** put platform-specific code in `main.cpp` or
`kopi.cpp`; if you need platform behavior, route it through `hotkey.hpp`.

## Building

### Linux (Fedora / RHEL / Rocky)
```sh
sudo dnf install gcc-c++ cmake make SDL3-devel libX11-devel libXtst-devel
cmake -B build -S .
cmake --build build -j$(nproc)
./build/kopi
```

### Linux (Ubuntu / Debian)
```sh
sudo apt install build-essential cmake libsdl3-dev libx11-dev libxtst-dev
cmake -B build -S .
cmake --build build -j$(nproc)
```

### macOS
```sh
brew install sdl3
cmake -B build -S .
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

Dear ImGui is fetched via CMake `FetchContent` (needs network on first
configure).  SDL3 is a system package; on Linux it must be SDL3, not SDL2.

## Platform gotchas

- **Global hotkey (Linux)** — `XGrabKey` on the root window for `Mod1Mask`
  (Alt) + `8`.  Runs on a background thread polling `XPending`; forwards via
  `SDL_PushEvent`.  Swallow `BadAccess` with a custom `XSetErrorHandler`.
- **Paste (Linux)** — `XTestFakeKeyEvent` needs the XTest extension.  Before
  pasting, `kopi_hide_window` restores the previous X input focus
  (`XSetInputFocus`), so the Ctrl+V lands in the user's last window.
- **Clipboard polling (Linux)** — X11 has no change-count API.  `kopi_get_clipboard_change_count`
  returns an FNV-1a hash of the clipboard text, so it reads the clipboard each
  poll.  Correct but not O(1); don't "optimize" this away or you'll miss
  same-owner clipboard changes.
- **MIME (Linux)** — uses SDL3 clipboard API: `SDL_GetClipboardMimeTypes`,
  `SDL_GetClipboardData` for capture; `SDL_SetClipboardData` with lazy
  callbacks for restore.  All SDL clipboard calls must run on the main thread.
- **Tray icon (Linux)** — raw X11 XEmbed client.  Left-click toggles the
  window, right-click quits.  Gracefully skips if no system tray is present
  (hotkey still works).  No dropdown menu (unlike macOS).
  - Reads `_NET_SYSTEM_TRAY_VISUAL` and creates the icon window with that
    (ARGB) visual when the tray advertises one — a 24-bit icon is docked but
    left invisible in a 32-bit ARGB socket.  Falls back to `CopyFromParent`
    (root visual) on plain trays, and warns when the server cannot host an
    ARGB child of the %d-bit root (e.g. no compositor).  Do not "simplify"
    this back to `CopyFromParent` only.
- **Launch at login (Linux)** — toggles a systemd autostart desktop entry at
  `~/.config/autostart/kopi.desktop`; exposed as a checkbox in the UI
  (`#ifdef __linux__` in `main.cpp`).  On macOS it lives in the tray menu.
- **Font** — an embedded, subsetted + stb_compress-compressed
  JuliaMonoSiesta lives in `src/font_data.hpp` and is loaded via
  `AddFontFromMemoryCompressedTTF`, so the look is identical on every OS.
  Regenerate it with `python3 packaging/make-font-header.py` (needs
  `pip install fonttools` + g++).  Use stb_compress, **not** gzip —
  ImGui's Gzip variant is gone in 1.91.9.
  OS monospace paths are a fallback only; `AddFontDefault()` is the last
  resort.  Note `AddFontFromFileTTF` asserts on failure, so only pass paths
  that exist.
- **Objective-C++ ARC** — `-fobjc-arc` is guarded to `APPLE` only; never apply
  it on Linux.

## Testing / verification

- After any change, build on the platform you touched: `cmake --build build`.
- Hotkey change: press Alt+8 (Linux) / Cmd+8 (macOS) and confirm the window
  toggles and the log line prints.
- Paste change: select an entry; confirm `kopi: Ctrl+V posted` (Linux) /
  `kopi: Cmd+V posted` (macOS) and that the target app receives the text.
- Clipboard capture: copy text in another app, summon kopi, confirm the entry
  appears; copy the same text again, confirm it's deduplicated.
- Tray: with a tray running, left-click toggles the window; right-click quits.
- History: entries persist across restarts in `~/.local/share/kopi/history.dat`.

## Conventions

- C++23 (`std::erase_if`, `std::chrono`, structured bindings).  No `using
  namespace std;`.
- ImGui immediate-mode UI in `render_ui()` inside `main.cpp`.
- Clipboard entries live in `kopi.hpp` (`struct Entry` with `text`,
  `mime_data`, `timestamp`); max `kMaxHistory = 500` entries, persisted
  base64-encoded.
- Commit messages are concise imperative summaries; keep the shared
  `hotkey.hpp` interface in sync on both platforms.
