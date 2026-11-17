#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ ! -f ime-fullscreen.so ]]; then
  echo 'Run ./build.sh first, or use the binary release archive.' >&2
  exit 1
fi
destination="${XDG_DATA_HOME:-$HOME/.local/share}/hyprland/plugins/ime-fullscreen"
mkdir -p -- "$destination"
# Rename the new library into place; never truncate a library in a live process.
install -m 755 ime-fullscreen.so "$destination/ime-fullscreen.so.new"
mv -- "$destination/ime-fullscreen.so.new" "$destination/ime-fullscreen.so"
install -m 644 ime-fullscreen.cpp README.md LICENSE "$destination/"
install -m 755 build.sh "$destination/"
printf 'Installed: %s/ime-fullscreen.so\n' "$destination"
printf 'See README.md for loading and automatic startup. No desktop config was changed.\n'
