#!/usr/bin/env bash
# A plugin mod from the manager's library to the running (fake) game: native-code checks, approval, deploy, loader install.
# Run tests/run_tests.sh first. Reads the game's MMORPG.zip and zlib1.dll; writes only under build/.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/plugin_test"
BIN="$ROOT/build/test/bin"
MT2MM="${MT2MM:-$ROOT/../mt2-modmanager/target/debug/mt2mm.exe}"
GAME_DIR_REAL="/f/SteamLibrary/steamapps/common/MMORPG Tycoon 2"
# The game's own zlib1.dll; with the loader installed in the real game, the copy the manager saved.
ZLIB_DLL="${GAME_ZLIB:-$GAME_DIR_REAL/zlib1.dll}"

if grep -q "MT2LOADER_VERSION=" "$ZLIB_DLL" 2>/dev/null; then
    ZLIB_DLL="$GAME_DIR_REAL/mt2loader/zlib1.game.dll"
fi
DATA_ZIP="${GAME_DATA:-$GAME_DIR_REAL/Data/MMORPG.zip}"

GAME="$BUILD/game"
PROFILE="$GAME/profile"
LOADER_FILES="$BUILD/loader"
MOD_SOURCE="$BUILD/hello_plugin"
LOG="$GAME/mt2loader/loader.log"
VERSION="$(sed -n 's/#define MT2LOADER_VERSION "\(.*\)"/\1/p' "$ROOT/src/include/mt2loader_version.h")"

failures=0
passes=0


check() {
    local description="$1"
    shift

    if "$@"; then
        passes=$((passes + 1))
    else
        failures=$((failures + 1))
        echo "FAIL: $description"
    fi
}

contains() {
    grep -q -- "$2" <<< "$1"
}

log_has() {
    grep -q -- "$1" "$LOG" 2>/dev/null
}

manager() {
    "$MT2MM" --profile "$(cygpath -w "$PROFILE")" --install "$(cygpath -w "$GAME")" --data "$(cygpath -w "$DATA_ZIP")" \
        --loader "$(cygpath -w "$LOADER_FILES")" "$@" 2>&1
}

run_game() {
    "$BIN/quiet_launcher.exe" "$(cygpath -w "$GAME/MT2.exe")"
}

set_builds() {
    sed -i "s/\"game_builds\": \[.*\]/\"game_builds\": [$1]/" "$MOD_SOURCE/manifest.json"
}

readd_mod() {
    manager add "$(cygpath -w "$MOD_SOURCE")" > /dev/null
}


prepare() {
    rm -rf "$BUILD"
    mkdir -p "$PROFILE/mod" "$LOADER_FILES/mt2loader"
    cp "$BIN/MT2.exe" "$BIN/SDL3.dll" "$BIN/libphysfs.dll" "$GAME/"
    cp "$ZLIB_DLL" "$GAME/zlib1.dll"

    # The core built by run_tests.sh that knows the fake exe as the game build "test".
    cp "$ROOT/dist/zlib1.dll" "$LOADER_FILES/"
    cp "$BIN/test_core/mt2loader.dll" "$LOADER_FILES/mt2loader/"

    cp -r examples/hello_plugin/mod "$MOD_SOURCE"
    set_builds '"test"'
}


add_second_plugin_mod() {
    local second="$BUILD/hello_two"
    rm -rf "$second"
    cp -r "$MOD_SOURCE" "$second"
    sed -i 's/"hello_plugin"/"hello_two"/; s/"Hello Plugin"/"Hello Two"/' "$second/manifest.json"
    manager add "$(cygpath -w "$second")" > /dev/null
}

# The examples' DLL is built with Visual Studio, so this also runs an MSVC-built plugin in the fake game.
test_approval_and_deploy() {
    local output
    readd_mod
    manager apply hello_plugin > /dev/null

    output="$(manager check)"
    check "added: no error for native code" contains "$output" "0 error(s)"

    output="$(manager deploy)"
    check "unapproved: deploy refused" contains "$output" "wasn't approved for this load order: hello_plugin"
    check "unapproved: says how" contains "$output" "mt2mm trust"
    check "unapproved: nothing deployed" test ! -e "$PROFILE/mod/mm_hello_plugin"

    output="$(manager deploy --allow-native)"
    check "approved once: deployed" test -f "$PROFILE/mod/mm_hello_plugin/native/hello_plugin.dll"
    check "no loader: only a warning" contains "$output" "needs the MT2 Loader, which isn't installed"
    check "no loader: game folder untouched" test ! -e "$GAME/mt2loader"
    check "deploy: manifest next to the plugin" test -f "$PROFILE/mod/mm_hello_plugin/manifest.json"
    check "deploy: plugin not in the merged folder" test ! -e "$PROFILE/mod/zzzz_mm/native"

    output="$(manager deploy)"
    check "approved once: not remembered" contains "$output" "wasn't approved"

    output="$(manager trust)"
    check "trust: lists the mod and its DLL" contains "$output" "Hello Plugin (hello_plugin): native/hello_plugin.dll"

    output="$(manager deploy)"
    check "trusted: deploys without asking" contains "$output" "Deployed to"

    output="$(manager loader install)"
    check "install: from the manager's copy" contains "$output" "Installed the loader $VERSION"

    output="$(manager check)"
    check "installed: warning gone" test -z "$(grep "needs the MT2 Loader" <<< "$output")"

    run_game
    check "run: game exits cleanly" test $? = 0
    check "run: plugin started from the deployed mod" log_has "\[hello_plugin\] Hello from hello_plugin: game build test"
    check "run: summary" log_has "Plugins: 1 started"
}

test_update_keeps_approval() {
    local output
    # Bytes after the end of a DLL don't change how it loads: like a mod update.
    printf 'update' >> "$MOD_SOURCE/native/hello_plugin.dll"
    readd_mod

    output="$(manager deploy)"
    check "updated mod: still approved" contains "$output" "Deployed to"
}

test_changed_set_asks_again() {
    local output
    add_second_plugin_mod
    manager apply hello_two > /dev/null

    output="$(manager deploy)"
    check "native mod added: asks again" contains "$output" "wasn't approved for this load order: hello_plugin, hello_two"

    manager trust > /dev/null
    output="$(manager deploy)"
    check "new set trusted: deploys" contains "$output" "Deployed to"

    run_game
    check "two plugin mods: both started" log_has "Plugins: 2 started"

    manager unapply hello_two > /dev/null
    output="$(manager deploy)"
    check "back to an approved set: deploys" contains "$output" "Deployed to"

    manager remove hello_two > /dev/null
}

# Settings a plugin mod declares in config.json: set in the manager, deployed next to the plugin, read by it.
test_plugin_settings() {
    local mod="$BUILD/settings_mod"
    local output
    rm -rf "$mod"
    mkdir -p "$mod/native"
    cp "$BIN/settings_plugin.dll" "$mod/native/settings_mod.dll"
    printf '{"id": "settings_mod", "name": "Settings Mod", "version": "1.0.0", "loader": {"plugins": ["native/settings_mod.dll"], "game_builds": ["test"]}}' \
        > "$mod/manifest.json"
    printf '[{"key": "greeting", "type": "string", "default": "hello"}, {"key": "count", "type": "int", "default": 1, "min": 0, "max": 5}]' \
        > "$mod/config.json"

    manager add "$(cygpath -w "$mod")" > /dev/null
    manager apply settings_mod > /dev/null
    manager trust > /dev/null
    manager settings settings_mod set count 4 > /dev/null

    output="$(manager deploy)"
    check "settings: deployed" contains "$output" "Deployed to"
    check "settings: no 'not used' warning for a plugin's settings" test -z "$(grep "is not used" <<< "$output")"
    check "settings: config.json next to the plugin" test -f "$PROFILE/mod/mm_settings_mod/config.json"
    check "settings: the player's values next to the plugin" grep -q '"count": 4' "$PROFILE/mod/mm_settings_mod/settings.json"

    run_game
    check "settings: the plugin reads the player's value and the default" log_has "\[settings_mod\] greeting=hello count=4"

    manager unapply settings_mod > /dev/null
    manager remove settings_mod > /dev/null
}

test_manifest_mistakes() {
    local output
    cp "$BIN/SDL3.dll" "$MOD_SOURCE/native/stray.dll"
    readd_mod

    output="$(manager check)"
    check "undeclared DLL: error" contains "$output" "native/stray.dll: is native code (a DLL) that manifest.json doesn't declare"

    rm "$MOD_SOURCE/native/stray.dll"
    set_builds ''
    readd_mod

    output="$(manager check)"
    check "no game builds: error" contains "$output" "must list the game builds"

    set_builds '"test"'
    sed -i 's|"plugins": \["native/hello_plugin.dll"\]|"plugins": ["native/hello_plugin.dll"], "features": ["flying_cars"]|' "$MOD_SOURCE/manifest.json"
    readd_mod

    output="$(manager check)"
    check "unknown feature: error" contains "$output" "'flying_cars' isn't a loader feature"

    sed -i 's|, "features": \["flying_cars"\]||' "$MOD_SOURCE/manifest.json"
    readd_mod
}

test_untrust() {
    local output
    output="$(manager untrust)"
    check "untrust: forgets" contains "$output" "Forgot 3 approval(s)"

    output="$(manager deploy)"
    check "untrust: asks again" contains "$output" "wasn't approved"
}

test_loader_stays_when_mods_go() {
    local output
    manager unapply hello_plugin > /dev/null
    manager deploy > /dev/null

    output="$(manager loader)"
    check "no plugin mods: loader left alone" contains "$output" "Loader:  enabled"
    check "no plugin mods: plugin folder gone" test ! -e "$PROFILE/mod/mm_hello_plugin"

    run_game
    check "no plugin mods: nothing started" log_has "Plugins: 0 started"
}


cd "$ROOT" || exit 1
export PATH="/c/msys64/mingw64/bin:$PATH"

for required in "$ZLIB_DLL" "$DATA_ZIP" "$BIN/MT2.exe" "$BIN/test_core/mt2loader.dll" "$MT2MM"; do
    if [ ! -f "$required" ]; then
        echo "missing $required (run tests/run_tests.sh and build mt2mm first; GAME_ZLIB and GAME_DATA override the game paths)"
        exit 1
    fi
done

export MT2_FAKE_WRITE_DIR="$(cygpath -w "$PROFILE")"

prepare

test_approval_and_deploy
test_update_keeps_approval
test_changed_set_asks_again
test_plugin_settings
test_manifest_mistakes
test_untrust
test_loader_stays_when_mods_go

echo "$passes passed, $failures failed"
[ "$failures" = 0 ]
