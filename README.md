# kopi

A graphical clipboard manager with vim keybindings, built in modern C++23.

Works on **macOS** and **Linux** (X11). Summon it with a global hotkey —
**Cmd+8** on macOS, **Ctrl+8** on Linux — navigate with vim keys, pick an
item — it pastes directly into your editor.

## Features

- **Global hotkey** — Cmd+8 (macOS) / Ctrl+8 (Linux) summons kopi from any application
- **Vim keybindings** — `j`/`k` to navigate, `/` to search, `Space` for multi-select, `Enter`/`y`/`l` to pick
- **Ctrl+J/K** — navigate filtered results while still typing in the search box
- **Auto-paste** — selecting an item hides kopi, returns focus to your editor, and simulates the paste shortcut automatically
- **Multi-line preview** — up to 10 lines per entry, variable-height items
- **Multi-select + merge** — select multiple entries with `Space` and merge them on paste
- **MIME capture** — optional toggle to capture HTML, RTF, and images alongside plain text for rich paste
- **Long history** — keeps up to 500 entries, persisted to disk, fully deduplicated
- **Whitespace ignored** — empty or whitespace-only copies are filtered out
- **Status/tray icon** — bold **k** in the menu bar (macOS) or system tray (Linux); click to toggle
- **Launch at Login** — menu-bar toggle on macOS, or a checkbox in the UI on Linux (systemd autostart)
- **Efficient clipboard polling** — macOS uses `NSPasteboard.changeCount` (O(1)); Linux hashes clipboard text
- **Light theme** — clean ImGui light style
- **Background app** — no Dock icon on macOS

## Requirements

- **macOS 14+** (Sonoma or later), Xcode Command Line Tools, CMake 3.21+
  - **Accessibility permission** — required for auto-paste (see below)
- **Linux (X11)**, CMake 3.21+, a C++23 compiler (gcc 13+ or clang 16+)
  - `libsdl3-dev`, `libx11-dev`, `libxtst-dev`
  - A system tray for the tray icon (optional — hotkey works without it)

## Build

### macOS
```sh
git clone https://github.com/sanchosiesta/kopi.git
cd kopi
brew install sdl3
cmake -B build -S .
cmake --build build -j$(sysctl -n hw.logicalcpu)
```
The binary is at `build/kopi.app/Contents/MacOS/kopi`.

### Linux (Ubuntu/Debian)
```sh
git clone https://github.com/sanchosiesta/kopi.git
cd kopi
sudo apt install build-essential cmake libsdl3-dev libx11-dev libxtst-dev
cmake -B build -S .
cmake --build build -j$(nproc)
```
The binary is at `build/kopi`.

Dependencies are fetched automatically:
- **SDL3** — via the system package manager
- **Dear ImGui** — fetched via CMake `FetchContent`
- **X11 / XTest** — system packages (Linux only)

## Run

### macOS
```sh
./build/kopi.app/Contents/MacOS/kopi
```
kopi starts hidden. Press **Cmd+8** or click the **k** in the menu bar to show
the window. Press **Ctrl+Q** to quit, or **Ctrl+C** from the terminal.

### Linux
```sh
./build/kopi
```
kopi starts hidden. Press **Ctrl+8** or click the **k** in the system tray to
show the window. Press **Ctrl+Q** to quit, or **Ctrl+C** from the terminal.

## Accessibility Permission (macOS only)

Auto-paste requires macOS Accessibility permission. Without it, selecting an
item still copies to the clipboard — you just press Cmd+V yourself.

On first launch, macOS will prompt you to grant permission. You can also set
it manually:

1. Open **System Settings → Privacy & Security → Accessibility**
2. Toggle ON the app you're launching kopi from (Terminal.app, iTerm, VS Code, etc.)
3. Restart kopi

Check terminal output: `kopi ERROR: Accessibility permission missing!` means
it's not yet granted. `kopi: Cmd+V posted` means it's working.

On **Linux**, no permission is needed — paste uses the **XTest extension**
(`kopi: Ctrl+V posted` in the log confirms it works). If XTest is unavailable,
kopi falls back to copy-only.

## Keybindings

| Key | Action |
|-----|--------|
| `j` / `↓` / `Ctrl+J` | Move selection down |
| `k` / `↑` / `Ctrl+K` | Move selection up |
| `/` | Enter search mode |
| `Ctrl+J` / `Ctrl+K` | Navigate filtered list while search is focused |
| `Space` | Toggle multi-select on current item |
| `Enter` / `y` / `l` | Paste: merge if multi-selected, else single item |
| `dd` | Delete selected entry |
| `gg` | Jump to first entry |
| `G` (Shift+G) | Jump to last entry |
| `Ctrl+D` | Page down |
| `Ctrl+U` | Page up |
| `Escape` | Exit search → hide window |
| `Ctrl+[` | Same as Escape |
| `Enter` (in search) | Confirm search, select first match |
| `Ctrl+Q` | Quit |

## MIME Capture

Enable the **"Capture rich content"** checkbox to store HTML, RTF, and images
alongside plain text. Entries with rich data show type badges (`[H]`, `[R]`,
`[I]`). When pasting, all stored types are restored to the clipboard — browsers
get HTML, text editors get plain text. On Linux this uses SDL3's clipboard
MIME API; on macOS it uses `NSPasteboard` types.

## Tech stack

| Component | macOS | Linux |
|-----------|-------|-------|
| UI | Dear ImGui 1.91 | Dear ImGui 1.91 |
| Windowing | SDL3 + Metal | SDL3 + SDL_Renderer |
| Hotkey | Carbon Event Manager (Cmd+8) | X11 `XGrabKey` (Ctrl+8) |
| Paste | CGEvent HID tap | XTest `XTestFakeKeyEvent` |
| Clipboard poll | `NSPasteboard.changeCount` (O(1)) | FNV-1a hash of clipboard text |
| MIME | `NSPasteboard` types | SDL3 clipboard MIME API |
| Status icon | `NSStatusItem` | X11 XEmbed tray client |
| Login item | `SMAppService` | systemd autostart `.desktop` |
| Language | C++23 | C++23 |

## Data

History is stored at `~/.local/share/kopi/history.dat`. Each entry contains a
Unix timestamp, optional MIME data (type + base64-encoded bytes), and
base64-encoded plain text. Maximum 500 entries. Duplicates are collapsed. Old
format files are automatically detected and upgraded.

## License

MIT
