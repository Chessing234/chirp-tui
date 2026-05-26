# CHIRP TUI

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-lightgrey.svg)](README.md)
[![Release CI](https://img.shields.io/github/actions/workflow/status/Chessing234/chirp-tui/release.yml?label=release%20CI)](https://github.com/Chessing234/chirp-tui/actions/workflows/release.yml)

**CHIRP TUI** is a small, dependency-free **C++17 terminal UI** for a **single flat reminder list** with **one or two background timers** (second timer off by default). When a timer fires, the app opens a **full-screen ANSI overlay** (flash, colors, blinking header, optional bell) listing your **open** reminders until you dismiss it.

The program binary is named **`reminders`** (same as the project’s internal target).

- **No ncurses, no JSON libraries** — only the C++ standard library, hand-rolled JSON, and a thin `platform.h` layer (`termios` / `ioctl` on POSIX, `SetConsoleMode` / `ReadConsoleInput` on Windows).
- **Windows 10+** uses **ConPTY virtual terminal processing** for the same escape-driven UI as macOS/Linux.
- Data file defaults to **`~/.reminders.json`** (or **`%USERPROFILE%\.reminders.json`** on Windows).

---

## Popups, other apps, and closing Terminal

`reminders` runs **inside a terminal**. It can only draw in **that** window.

| Situation | What happens |
|-----------|----------------|
| Terminal **closed** or `reminders` **not running** | Timers do not run; **nothing can pop up**. Start `reminders` again and leave that session open (or use a different kind of app for background alerts). |
| Terminal **open but in the background** (another app is focused) | On **macOS**, before the overlay the app tries to **bring Terminal / iTerm2 / WezTerm / Ghostty to the front** (based on `$TERM_PROGRAM`). Then the usual fullscreen overlay appears **in that terminal**. This is **best-effort** and does not apply to every terminal. |
| You want a banner **on top of Chrome / games / full-screen apps** | A terminal program **cannot** do that. You would need **macOS notifications**, a **menu bar app**, or Apple’s **Reminders / Calendar** — a different architecture than this repo. |

**Optional (macOS):** this repo includes **ChirpAlerts**, a small **menu bar** companion that reads the same `~/.reminders.json`, mirrors **T1/T2**, and fires **system notifications** plus a **floating HUD**. It is **not** a release binary (build from source with Xcode); see [`macos/ChirpAlerts/README.md`](macos/ChirpAlerts/README.md).

---

## Easiest setup (recommended)

Use a **prebuilt binary** from [Releases](https://github.com/Chessing234/chirp-tui/releases/latest).

### macOS (Apple Silicon or Intel, universal binary)

1. Open the [latest release](https://github.com/Chessing234/chirp-tui/releases/latest) and download **`reminders-macos-universal`**. Safari usually saves it to **`~/Downloads`**.

2. In Terminal, go to the folder that contains the file (adjust the path if yours is different):

```bash
cd ~/Downloads
ls reminders-macos-universal
```

If `ls` says **No such file**, use Finder → Downloads, confirm the exact filename (sometimes it gains a `(1)` suffix), then `cd` to that folder or use the full path in the commands below.

3. Install to `/usr/local/bin` (needs your password once):

```bash
chmod +x reminders-macos-universal
sudo mkdir -p /usr/local/bin
sudo mv reminders-macos-universal /usr/local/bin/reminders
```

**One-liner** (if the file is definitely in Downloads and named exactly as above):

```bash
chmod +x ~/Downloads/reminders-macos-universal && sudo mkdir -p /usr/local/bin && sudo mv ~/Downloads/reminders-macos-universal /usr/local/bin/reminders
```

4. Run:

```bash
reminders --help
reminders
```

If macOS blocks the app (“damaged” or unidentified developer), try: `xattr -dr com.apple.quarantine /usr/local/bin/reminders` then run `reminders` again.

If you prefer not to use `sudo`, put the binary anywhere on your **`PATH`** (for example create `~/bin`, move the file there as `reminders`, and add `export PATH="$HOME/bin:$PATH"` to `~/.zshrc`).

### Linux (x86_64)

```bash
VER=1.2.0
curl -fsSL -o reminders "https://github.com/Chessing234/chirp-tui/releases/download/v${VER}/reminders-linux-x86_64"
chmod +x reminders
sudo mv reminders /usr/local/bin/
```

On **ARM64** Linux, download **`reminders-linux-aarch64`** instead and use the same `chmod` / `mv` steps.

### Windows

1. From the latest release, download **`reminders-windows-x86_64.exe`**.
2. Rename to **`reminders.exe`** (optional) and place it in a folder on your **`PATH`**, or run it from any folder.
3. Open **Command Prompt** or **PowerShell** in that folder and run `reminders.exe --help`.

---

## Build from source (also easy)

You need **CMake 3.16+** and a **C++17** compiler.

### macOS / Linux

```bash
git clone https://github.com/Chessing234/chirp-tui.git
cd chirp-tui
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/reminders --help
```

### Windows (Visual Studio generator)

```bat
git clone https://github.com/Chessing234/chirp-tui.git
cd chirp-tui
cmake -B build
cmake --build build --config Release
build\Release\reminders.exe --help
```

### Install into a prefix (Unix)

```bash
cmake --install build --prefix /usr/local
```

---

## Optional: Homebrew / Scoop

These install methods need **checksums** filled in after each release (see comments in the formula / manifest).

### macOS (Homebrew, local formula)

```bash
brew install --formula ./Formula/reminders.rb
```

### Windows (Scoop)

```powershell
scoop install https://raw.githubusercontent.com/Chessing234/chirp-tui/main/scoop/reminders.json
```

Edit `Formula/reminders.rb` and `scoop/reminders.json` to set `url` / `sha256` (or `hash`) from the release assets before relying on them in automation.

---

## Usage

```text
reminders [options]
```

### CLI flags

| Flag | Description |
|------|-------------|
| `--t1 <min>` | First timer interval in minutes (default **60**). |
| `--t2 <min>` | Second timer in minutes; **0** disables it (default **0** = only the 60‑minute timer). |
| `--data-file <path>` | JSON file path (default **`~/.reminders.json`**). Leading `~/` expands on all platforms. |
| `--no-bell` | Disable terminal bell (`\a`) on popups. |
| `--version`, `-v` | Print version and exit. |
| `--help`, `-h` | Print help and exit. |

### Keybindings (main list)

| Key | Action |
|-----|--------|
| ↑ / ↓ | Move selection |
| `a` | Add reminder (prompted form) |
| `e` | Edit selected |
| `d` | Delete selected |
| `Space` | Toggle done |
| `s` | Settings (intervals, bell) |
| `q` | Quit (saves JSON) |

### Settings (`s`)

| Key | Action |
|-----|--------|
| `1` / `2` | Select T1 or T2 for `+` / `-` |
| `+` / `-` | ±5 minutes (T1 minimum **1**; T2 minimum **0** = second timer off) |
| `b` | Toggle bell |
| `s` | Save and return |
| `q` | Return to list |

---

## JSON format

Default path: **`~/.reminders.json`**. Each reminder has `id`, `title`, `description`, `due`, and `done`. Older files may still contain a legacy `priority` field; it is ignored on load and omitted when saving. Example: [`reminders.json`](reminders.json).

---

## ASCII “screenshot”

```text
 Reminders  — /home/you/.reminders.json —     Next popup in: 55:01  (T1 55:01 · T2 off)

 ── Open ──
 > Fix the deployment bug (due 2026-05-22 16:00)
   Review PR #42
   Update README

 ── Done ──
   Old chore (completed)

 ^v move | a add | e edit | d del | space done | s settings | q quit
```

---

## Contributing

1. Fork the repository.  
2. Create a branch (`git checkout -b fix-or-feature`).  
3. Make changes with a clear commit history.  
4. Open a **Pull Request** describing behavior, platforms tested, and any release-note impact.

---

## License

MIT — see [LICENSE](LICENSE).
