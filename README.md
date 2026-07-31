# kopi

A graphical clipboard manager with vim keybindings, built in modern C++23.

Summon it with **Cmd+8**, navigate with vim keys, pick an item — it pastes directly into your editor.

## Features

- **Global hotkey** — Cmd+8 summons kopi from any application
- **Vim keybindings** — `j`/`k` to navigate, `/` to search, `Space` for multi-select, `Enter`/`y`/`l` to pick
- **Ctrl+J/K** — navigate filtered results while still typing in the search box
- **Auto-paste** — selecting an item hides kopi, returns focus to your editor, and simulates Cmd+V automatically
- **Multi-line preview** — up to 10 lines per entry, variable-height items
- **Multi-select + merge** — select multiple entries with `Space` and merge them on paste
- **MIME capture** — optional toggle to capture HTML, RTF, and images alongside plain text for rich paste
- **Long history** — keeps up to 500 entries, persisted to disk, fully deduplicated
- **Whitespace ignored** — empty or whitespace-only copies are filtered out
- **Menu bar icon** — bold **k** in the menu bar, click to toggle, right-click for Quit
- **Launch at Login** — toggle in the menu bar dropdown to auto-start with macOS
- **O(1) clipboard polling** — uses `NSPasteboard.changeCount` to avoid memory allocation at idle
- **Light theme** — clean ImGui light style
- **Background app** — `LSUIElement`, no Dock icon

## Requirements

- macOS 14+ (Sonoma or later)
- Xcode Command Line Tools (for the C++23 compiler)
- CMake 3.21+
- **Accessibility permission** — required for auto-paste (see below)

## Build

```sh
git clone https://github.com/sanchosiesta/kopi.git
cd kopi
brew install sdl3
cmake -B build -S .
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

The binary is at `build/kopi.app/Contents/MacOS/kopi`.

Dependencies are fetched automatically:
- **SDL3** — installed via Homebrew
- **Dear ImGui** — fetched via CMake `FetchContent`

## Run

```sh
./build/kopi.app/Contents/MacOS/kopi
```

kopi starts hidden. Press **Cmd+8** or click the **k** in the menu bar to show the window. Press **Ctrl+Q** to quit, or **Ctrl+C** from the terminal.

## Accessibility Permission

Auto-paste requires macOS Accessibility permission. Without it, selecting an item still copies to the clipboard — you just press Cmd+V yourself.

On first launch, macOS will prompt you to grant permission. You can also set it manually:

1. Open **System Settings → Privacy & Security → Accessibility**
2. Toggle ON the app you're launching kopi from (Terminal.app, iTerm, VS Code, etc.)
3. Restart kopi

Check terminal output: `kopi ERROR: Accessibility permission missing!` means it's not yet granted. `kopi: Cmd+V posted` means it's working.

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

Enable the **"Capture rich content"** checkbox to store HTML, RTF, and images alongside plain text. Entries with rich data show type badges (`[H]`, `[R]`, `[I]`). When pasting, all stored types are restored to the clipboard — browsers get HTML, text editors get plain text.

## Tech stack

| Component | Library | Why |
|-----------|---------|-----|
| UI | Dear ImGui 1.91 | Immediate mode — fastest iteration for tools |
| Windowing | SDL3 3.4 | Native clipboard API, Metal renderer |
| Hotkey | Carbon Event Manager | Global Cmd+8, works system-wide |
| Paste | CGEvent (HID tap) | Hardware-level Cmd+V simulation |
| Clipboard poll | NSPasteboard.changeCount | O(1) integer check, zero allocation at idle |
| MIME | NSPasteboard types | Full MIME capture/restore for rich paste |
| Login item | SMAppService | Native macOS launch-at-login |
| Language | C++23 | `std::erase_if`, `std::chrono`, structured bindings |

## Data

History is stored at `~/.local/share/kopi/history.dat`. Each entry contains a Unix timestamp, optional MIME data (type + base64-encoded bytes), and base64-encoded plain text. Maximum 500 entries. Duplicates are collapsed. Old format files are automatically detected and upgraded.

## License

MIT
