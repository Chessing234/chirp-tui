# ChirpAlerts (macOS companion)

Menu bar app that reads **`~/.reminders.json`** (same file as the `reminders` TUI), runs **T1 / T2** timers like the terminal app, and on each check-in:

1. Posts a **system notification** (visible even when other apps are focused).
2. Opens a **floating panel** styled like the TUI overlay (monospace, dark frame).

## Requirements

- macOS **13+**
- **Full Xcode** from the **Mac App Store** (several GB). ChirpAlerts uses **SwiftUI**; Apple’s **Command Line Tools alone** often hit a **Swift compiler vs macOS SDK mismatch** and the build fails.

`swift package` may also fail on CLT-only setups; use **`./build.sh`** after Xcode is selected (below).

### One-time setup (after Xcode is installed)

```bash
ls /Applications/ | grep -i xcode    # should show Xcode.app
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept      # if prompted (read license first)
open -a Xcode                        # first launch: finish extra component install
```

Then:

```bash
cd ~/Downloads/CHIRP/macos/ChirpAlerts   # or your clone path
./build.sh
```

If you use **Xcode-beta.app**:

```bash
sudo xcode-select -s /Applications/Xcode-beta.app/Contents/Developer
```

## Where to run `build.sh`

From the folder that contains **`build.sh`**:

```bash
cd ~/Downloads/CHIRP/macos/ChirpAlerts
./build.sh
```

## Build

```bash
./build.sh
```

Build output: **`./ChirpAlerts.app`** (a real app bundle). `UserNotifications` needs this; running a loose Mach-O next to `build.sh` will crash with `bundleProxyForCurrentProcess is nil`.

`build.sh` **refuses** to run when `xcode-select` points only at **Command Line Tools** (to avoid the confusing errors below). To try CLT anyway: `CHIRP_ALLOW_CLT_BUILD=1 ./build.sh`.

## Run

```bash
open ./ChirpAlerts.app
```

Or double-click **`ChirpAlerts.app`** in Finder. To run from Terminal without `open`:

```bash
./ChirpAlerts.app/Contents/MacOS/ChirpAlerts
```

Grant **Notifications** when macOS prompts. Look for the **bell** icon in the menu bar.

Use **“Test HUD + notification now”** once to verify permissions and the panel.

## Troubleshooting

### “SDK is not supported by the compiler” (Swift 6.2.x vs 6.2.y)

Your **swift** from Command Line Tools is **newer** (or older) than the **Swift interfaces** shipped inside the **macOS SDK** you’re compiling against. SwiftUI pulls in those modules, so the link fails.

**Fix:** use **Xcode’s** toolchain (steps above), not CLT-only. Do **not** rely on `sudo xcode-select -s /Library/Developer/CommandLineTools` for this app.

### “redefinition of module SwiftBridging”

Usually the **same root cause** as above (broken / mixed toolchain). Switching to **Xcode.app** and reopening Xcode fixes it for most people.

### `bundleProxyForCurrentProcess is nil` / `NSInternalInconsistencyException` (UserNotifications)

You launched a **bare executable** next to `build.sh`. **`UNUserNotificationCenter` only works inside a proper `.app` bundle** (valid `Bundle.main`). Pull the latest `build.sh`, run **`./build.sh`**, then start the app with **`open ./ChirpAlerts.app`** (not `./ChirpAlerts`).

### `invalid developer directory … Xcode.app` / `Unable to find application named Xcode`

**Xcode is not installed** (or it lives under a different name, e.g. **Xcode-beta.app**).

1. Open the **App Store**, search **Xcode**, tap **Get** / **Install** (large download, often **10–15 GB+**; needs free disk space and time on Wi‑Fi).
2. When the install finishes, confirm it exists:

   ```bash
   ls /Applications/ | grep -i xcode
   ```

   You should see **`Xcode.app`** (or **`Xcode-beta.app`**).

   If you only see **`Xcode.appdownload`**, the App Store download or install is **still in progress** (or stuck). Do **not** run `xcode-select` yet—wait until **`Xcode.app`** appears. Open the **App Store** → **Xcode** and watch the progress bar; ensure you have **enough free disk space** (often **25 GB+** free is safer than the bare minimum) and a **stable network**. If it never finishes, **restart the Mac**, open App Store again, or cancel and retry the Xcode install.

3. Point the tools at it and open once:

   ```bash
   sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
   open -a Xcode
   ```

   For beta: `sudo xcode-select -s /Applications/Xcode-beta.app/Contents/Developer`

If the App Store is blocked (work/school Mac), you need an admin to install Xcode, or install from [Apple Developer downloads](https://developer.apple.com/download/all/) (Apple ID; same app, manual install).

### Still stuck after Xcode

```bash
xcode-select -p
xcrun swift --version
```

Versions should come from **…/Xcode.app/Contents/Developer**. In Xcode: **Settings → Locations → Command Line Tools** — pick your Xcode version.

## Notes

- **Do not run two timer daemons** for the same schedule: if `reminders` (TUI) is also running with live timers, you can get double alerts. Typical setup: edit in the TUI when you want; quit it and leave **ChirpAlerts** running for background alerts—or only run one at a time.
- **ChirpAlerts** keeps its own “last fired” clock in memory (like the TUI). Restarting the app resets the countdown from “now”.
- First launch: if `~/.reminders.json` is missing, defaults **T1=60**, **T2=off** are used until the file exists.
