# kopi

A graphical clipboard manager with vim keybindings, built in modern C++23.

Summon it with **Cmd+8**, navigate with vim keys, pick an item — it pastes directly into your editor.

https://github.com/user-attachments/assets/placeholder

## Features

- **Global hotkey** — Cmd+8 summons kopi from any application
- **Vim keybindings** — `j`/`k` to navigate, `/` to search, `Enter`/`y`/`l` to pick, `dd` to delete, `gg`/`G` for top/bottom
- **Auto-paste** — selecting an item hides kopi, returns focus to your editor, and simulates Cmd+V automatically
- **Long history** — keeps up to 500 entries, persisted to disk, fully deduplicated
- **Background app** — no Dock icon, runs quietly polling the clipboard every 250ms
- **Instant dismiss** — Escape or Cmd+8 hides the window and returns focus to the previous app

## Requirements

- macOS 14+ (Sonoma or later)
- Xcode Command Line Tools (for the C++23 compiler)
- CMake 3.21+
- **Accessibility permission** — required for auto-paste (see below)

## Build

```sh
git clone https://github.com/your-org/kopi.git
cd kopi
cmake -B build -S .
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

The binary is at `build/kopi.app/Contents/MacOS/kopi`.

Dependencies are fetched automatically:
- **SDL3** — installed via Homebrew (`brew install sdl3`)
- **Dear ImGui** — fetched via CMake `FetchContent`

## Run

```sh
./build/kopi.app/Contents/MacOS/kopi
```

kopi starts hidden. Press **Cmd+8** to show the window. Press **Ctrl+Q** to quit (when the window is visible), or **Ctrl+C** from the terminal.

## Accessibility Permission

Auto-paste requires macOS Accessibility permission. Without it, selecting an item still copies to the clipboard — you just press Cmd+V yourself.

1. Open **System Settings → Privacy & Security → Accessibility**
2. Toggle ON the app you're launching kopi from (Terminal.app, iTerm, VS Code, etc.)
3. Restart kopi

Check terminal output: `kopi ERROR: Accessibility permission missing!` means it's not yet granted. `kopi: Cmd+V posted` means it's working.

## Keybindings

| Key | Action |
|-----|--------|
| `j` / `↓` | Move selection down |
| `k` / `↑` | Move selection up |
| `/` | Enter search mode |
| `Enter` / `y` / `l` | Copy selected item + auto-paste + hide |
| `dd` | Delete selected entry |
| `gg` | Jump to first entry |
| `G` (Shift+G) | Jump to last entry |
| `Ctrl+D` | Page down |
| `Ctrl+U` | Page up |
| `Escape` | Clear filter → exit search → hide window (3-stage) |
| `Ctrl+[` | Same as Escape |
| `Enter` (in search) | Confirm search, select first match |
| `Ctrl+Q` | Quit |

## Tech stack

| Component | Library | Why |
|-----------|---------|-----|
| UI | Dear ImGui 1.91 | Immediate mode — fastest iteration for tools |
| Windowing | SDL3 3.4 | Native clipboard API, Metal renderer |
| Hotkey | Carbon Event Manager | Global Cmd+8, works system-wide |
| Paste | CGEvent (HID tap) | Hardware-level Cmd+V simulation |
| Language | C++23 | `std::erase_if`, `std::chrono`, structured bindings |

## Data

History is stored at `~/.local/share/kopi/history.dat` — one entry per line, base64-encoded content with Unix timestamps. Maximum 500 entries. Duplicate entries are automatically collapsed to a single most-recent copy.

## License

MIT
