#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ $(pkg-config --modversion hyprland) != 0.56.2 ]]; then
  echo 'This repair targets Hyprland 0.56.2; review newer versions before rebuilding.' >&2
  exit 1
fi
read -r -a cflags <<< "$(pkg-config --cflags hyprland)"
"${CXX:-g++}" -shared -fPIC -std=c++26 -O2 -Wall -Wextra \
  ime-fullscreen.cpp -o ime-fullscreen.so.new "${cflags[@]}"
mv -- ime-fullscreen.so.new ime-fullscreen.so
