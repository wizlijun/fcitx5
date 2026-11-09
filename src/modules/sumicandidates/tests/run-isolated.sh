#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d -t sumi-fcitx-test.XXXXXX)
# Intentionally retain the private log/build for review. Never use the desktop bus.
printf 'Isolated test artifacts: %s\n' "$test_dir"
mkdir -p "$test_dir/lib" "$test_dir/data/fcitx5/addon" "$test_dir/config/fcitx5" "$test_dir/cache"
g++ -std=c++20 -Wall -Wextra -Werror -fPIC -shared \
    "$source_dir/sumicandidates.cpp" -o "$test_dir/lib/libsumicandidates.so" \
    $(pkg-config --cflags --libs Fcitx5Core) -I/usr/include/Fcitx5/Module/fcitx-module/dbus
sed -e 's/@FCITX_ADDON_TYPE@/SharedLibrary/g' -e 's/@PROJECT_VERSION@/5.1.23/g' \
    "$source_dir/sumicandidates.conf.in" > "$test_dir/data/fcitx5/addon/sumicandidates.conf"
cat > "$test_dir/config/fcitx5/profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=pinyin
[Groups/0/Items/0]
Name=keyboard-us
[Groups/0/Items/1]
Name=pinyin
[GroupOrder]
0=Default
PROFILE
export SUMI_FCITX_TEST_DIR="$test_dir" SUMI_FCITX_TEST_SOURCE="$source_dir"
dbus-run-session -- bash -eu -c '
    export SUMI_FCITX_ISOLATED_TEST=1
    export XDG_CONFIG_HOME="$SUMI_FCITX_TEST_DIR/config"
    export XDG_DATA_HOME="$SUMI_FCITX_TEST_DIR/data"
    export XDG_CACHE_HOME="$SUMI_FCITX_TEST_DIR/cache"
    export FCITX_ADDON_DIRS="$SUMI_FCITX_TEST_DIR/lib:/usr/lib/fcitx5"
    unset DISPLAY WAYLAND_DISPLAY
    fcitx5 --keep --disable all --enable dbus,dbusfrontend,keyboard,pinyin,pinyinhelper,punctuation,chttrans,spell,sumicandidates > "$SUMI_FCITX_TEST_DIR/fcitx.log" 2>&1 &
    test_fcitx_pid=$!
    trap "kill $test_fcitx_pid 2>/dev/null || true; wait $test_fcitx_pid 2>/dev/null || true" EXIT
    python "$SUMI_FCITX_TEST_SOURCE/tests/integration.py"
'
