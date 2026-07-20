#!/usr/bin/env bash
# Build / flash the Obsidian ES3N28P target via PlatformIO.
set -euo pipefail
cd "$(dirname "$0")"

CMD="${1:-build}"
UPLOAD_PORT="${UPLOAD_PORT:-/dev/ttyACM0}"

if ! command -v pio >/dev/null 2>&1; then
  echo "PlatformIO CLI (pio) not found on PATH."
  echo ""
  echo "Install once, then re-run this script:"
  echo "  pipx install platformio"
  echo "  # or:  python3 -m venv .pio-venv && .pio-venv/bin/pip install platformio"
  echo "  # then: export PATH=\"\$PWD/.pio-venv/bin:\$PATH\""
  exit 1
fi

case "$CMD" in
  build|"")
    pio run
    ;;
  upload)
    pio run -t upload --upload-port "$UPLOAD_PORT"
    ;;
  monitor)
    pio device monitor --port "$UPLOAD_PORT"
    ;;
  clean)
    pio run -t clean
    ;;
  *)
    echo "Usage: $0 [build|upload|monitor|clean]"
    echo "  UPLOAD_PORT=/dev/ttyACM0 $0 upload"
    exit 1
    ;;
esac
