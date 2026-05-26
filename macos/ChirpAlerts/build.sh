#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/Sources/ChirpAlerts"
APP="$ROOT/ChirpAlerts.app"
EXE="$APP/Contents/MacOS/ChirpAlerts"
PLIST="$ROOT/Info.plist"

# SwiftUI + system frameworks need the Swift compiler and macOS SDK to match.
# Command Line Tools alone often errors with:
#   "SDK is not supported by the compiler" / "redefinition of module SwiftBridging"
DEVDIR="$(xcode-select -p 2>/dev/null || true)"
if [[ "$DEVDIR" == *CommandLineTools* ]]; then
  echo "error: Active developer directory is Command Line Tools:" >&2
  echo "  $DEVDIR" >&2
  echo "" >&2
  echo "  ChirpAlerts uses SwiftUI; build with full Xcode so Swift matches the SDK:" >&2
  echo "    1) Install Xcode from the Mac App Store" >&2
  echo "    2) sudo xcode-select -s /Applications/Xcode.app/Contents/Developer" >&2
  echo "    3) Open Xcode once (accept license / install components)" >&2
  echo "    4) Re-run ./build.sh" >&2
  echo "" >&2
  echo "  To force a CLT-only attempt anyway: CHIRP_ALLOW_CLT_BUILD=1 ./build.sh" >&2
  if [[ "${CHIRP_ALLOW_CLT_BUILD:-}" != "1" ]]; then
    exit 1
  fi
  echo "warning: CHIRP_ALLOW_CLT_BUILD=1 — build may still fail on toolchain mismatch." >&2
fi

SDK="$(xcrun --show-sdk-path 2>/dev/null || true)"
if [[ -z "$SDK" ]]; then
  echo "error: xcrun could not find an SDK. Install Xcode or Command Line Tools." >&2
  exit 1
fi
ARCH="$(uname -m)"
TARGET="${ARCH}-apple-macosx13.0"
if [[ ! -f "$PLIST" ]]; then
  echo "error: missing $PLIST" >&2
  exit 1
fi

# UNUserNotificationCenter requires a real .app bundle (mainBundle); a bare swiftc binary crashes at launch.
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$PLIST" "$APP/Contents/Info.plist"
echo "Building ChirpAlerts.app → $EXE (target $TARGET)"
swiftc -O \
  -sdk "$SDK" \
  -target "$TARGET" \
  -framework AppKit \
  -framework SwiftUI \
  -framework UserNotifications \
  -framework Combine \
  "$SRC/ChirpAlertsApp.swift" \
  "$SRC/StoreModels.swift" \
  "$SRC/ReminderEngine.swift" \
  "$SRC/HUDPanel.swift" \
  -o "$EXE"
chmod +x "$EXE"
rm -f "$ROOT/ChirpAlerts"
echo "Done. Run: open \"$APP\""
