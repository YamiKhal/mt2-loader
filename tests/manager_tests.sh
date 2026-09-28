#!/usr/bin/env bash
# Runs `mt2mm loader install|remove` against fake game folders, including loaders installed by hand.
# Run tests/run_tests.sh first (it builds the fake game), and `cargo build -p mt2mm-cli` in mt2-modmanager.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sed -n 's/#define MT2LOADER_VERSION "\(.*\)"/\1/p' "$ROOT/src/include/mt2loader_version.h")"
BUILD="$ROOT/build/manager_test"
BIN="$ROOT/build/test/bin"
DIST="$ROOT/dist"
MT2MM="${MT2MM:-$ROOT/../mt2-modmanager/target/debug/mt2mm.exe}"
GAME_DIR_REAL="/f/SteamLibrary/steamapps/common/MMORPG Tycoon 2"
# The game's own zlib1.dll; with the loader installed in the real game, the copy the manager saved.
ZLIB_DLL="${GAME_ZLIB:-$GAME_DIR_REAL/zlib1.dll}"

if grep -q "MT2LOADER_VERSION=" "$ZLIB_DLL" 2>/dev/null; then
    ZLIB_DLL="$GAME_DIR_REAL/mt2loader/zlib1.game.dll"
fi
CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror"

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

same_file() {
    cmp -s "$1" "$2"
}

new_game_folder() {
    local folder="$BUILD/$1"
    rm -rf "$folder"
    mkdir -p "$folder"
    cp "$BIN/MT2.exe" "$BIN/SDL3.dll" "$BIN/libphysfs.dll" "$folder/"
    cp "$ZLIB_DLL" "$folder/zlib1.dll"
    echo "$folder"
}

install_by_hand() {
    cp "$DIST/zlib1.dll" "$1/zlib1.dll"
    mkdir -p "$1/mt2loader"
    cp "$DIST/mt2loader/mt2loader.dll" "$1/mt2loader/"
}

loader() {
    local game="$1"
    shift
    "$MT2MM" --install "$(cygpath -w "$game")" --loader "$(cygpath -w "$DIST")" loader "$@" 2>&1
}

run_game() {
    "$BIN/quiet_launcher.exe" "$(cygpath -w "$1/MT2.exe")" "${2:-}"
}


test_install_and_remove() {
    local game output
    game="$(new_game_folder install_remove)"

    output="$(loader "$game")"
    check "fresh: not installed" contains "$output" "Loader:  not installed"

    output="$(loader "$game" install)"
    check "install: succeeds" contains "$output" "Installed the loader $VERSION as zlib1.dll"
    check "install: game's zlib kept for Remove" same_file "$ZLIB_DLL" "$game/mt2loader/zlib1.game.dll"
    check "install: proxy in place" same_file "$DIST/zlib1.dll" "$game/zlib1.dll"
    check "install: core in place" same_file "$DIST/mt2loader/mt2loader.dll" "$game/mt2loader/mt2loader.dll"
    check "install: nothing else in the game folder" test ! -e "$game/zlib1_original.dll"
    check "install: no temporary files" test -z "$(find "$game" -name '*.mt2mm-new')"

    run_game "$game"
    check "install: game runs" test $? = 0
    check "install: core ran" grep -q "core $VERSION started" "$game/mt2loader/loader.log"

    output="$(loader "$game")"
    check "status: enabled" contains "$output" "Loader:  enabled"
    check "status: shows the log" contains "$output" "loader.log"

    output="$(loader "$game" install)"
    check "update: replaces" contains "$output" "Replacing loader $VERSION"
    check "update: backup untouched" same_file "$ZLIB_DLL" "$game/mt2loader/zlib1.game.dll"

    output="$(loader "$game" remove)"
    check "remove: succeeds" contains "$output" "Put the game's own zlib1.dll back"
    check "remove: zlib1.dll is the game's again" same_file "$ZLIB_DLL" "$game/zlib1.dll"
    check "remove: loader folder gone" test ! -e "$game/mt2loader"

    run_game "$game"
    check "remove: game runs" test $? = 0

    output="$(loader "$game" remove)"
    check "remove twice: harmless" contains "$output" "already the game's own"
}

test_installed_by_hand() {
    local game output
    game="$(new_game_folder by_hand)"
    install_by_hand "$game"

    output="$(loader "$game")"
    check "by hand: detected" contains "$output" "Loader:  enabled"

    run_game "$game"
    check "by hand: game runs" test $? = 0

    output="$(loader "$game" install)"
    check "by hand: manager can update it" contains "$output" "Replacing loader $VERSION"

    output="$(loader "$game" remove)"
    check "by hand: remove needs Steam verify" contains "$output" "wasn't saved"
    check "by hand: nothing changed" same_file "$DIST/zlib1.dll" "$game/zlib1.dll"

    cp "$ZLIB_DLL" "$game/zlib1.dll"
    output="$(loader "$game" remove)"
    check "by hand: remove after verify tidies" contains "$output" "Removed the mt2loader folder"
}

test_steam_verify() {
    local game output
    game="$(new_game_folder steam_verify)"
    loader "$game" install > /dev/null
    run_game "$game"

    cp "$ZLIB_DLL" "$game/zlib1.dll"
    output="$(loader "$game")"
    check "verify: noticed" contains "$output" "Steam put the game's own zlib1.dll back"

    output="$(loader "$game" install)"
    check "verify: install again" contains "$output" "Installed the loader"

    cp "$ZLIB_DLL" "$game/zlib1.dll"
    output="$(loader "$game" remove)"
    check "verify: remove tidies" contains "$output" "Removed the mt2loader folder"
    check "verify: game's zlib stays" same_file "$ZLIB_DLL" "$game/zlib1.dll"
}

test_core_lost() {
    local game output
    game="$(new_game_folder core_lost)"
    loader "$game" install > /dev/null
    rm "$game/mt2loader/mt2loader.dll"

    output="$(loader "$game")"
    check "core lost: status explains" contains "$output" "mt2loader.dll is missing or damaged"

    run_game "$game"
    check "core lost: game still runs" test $? = 0

    output="$(loader "$game" install)"
    check "core lost: install repairs" contains "$output" "Copied mt2loader"
}

test_legacy_install() {
    local game output
    game="$(new_game_folder legacy)"
    loader "$game" install > /dev/null
    cp "$ZLIB_DLL" "$game/zlib1_original.dll"

    output="$(loader "$game" remove)"
    check "0.1.0 leftover: deleted" contains "$output" "Deleted zlib1_original.dll"
    check "0.1.0 leftover: gone" test ! -e "$game/zlib1_original.dll"
}

test_foreign_zlib() {
    local game output
    game="$(new_game_folder foreign)"
    cp "$BIN/SDL3.dll" "$game/zlib1.dll"

    output="$(loader "$game" install)"
    check "foreign: install refuses" contains "$output" "isn't the game's or this loader's"
    check "foreign: nothing changed" same_file "$BIN/SDL3.dll" "$game/zlib1.dll"
    check "foreign: no loader folder" test ! -e "$game/mt2loader"
}

test_new_zlib_exports() {
    local game output
    game="$(new_game_folder new_exports)"
    cp "$BIN/zlib_extra.dll" "$game/zlib1.dll"

    output="$(loader "$game" install)"
    check "new zlib export: install refuses" contains "$output" "deflateFutureFeature"
    check "new zlib export: nothing changed" same_file "$BIN/zlib_extra.dll" "$game/zlib1.dll"
}

test_game_running() {
    local game output
    game="$(new_game_folder running)"
    run_game "$game" --wait &
    local game_process=$!
    sleep 2

    output="$(loader "$game" install)"
    check "running: install refuses" contains "$output" "is running"
    check "running: nothing changed" same_file "$ZLIB_DLL" "$game/zlib1.dll"

    wait "$game_process"
}

test_not_game_folder() {
    local game output
    game="$(new_game_folder not_game)"
    rm "$game/MT2.exe"

    output="$(loader "$game" install)"
    check "not the game folder: refused" contains "$output" "has no MT2.exe"
}

test_foreign_files_kept() {
    local game output
    game="$(new_game_folder foreign_files)"
    loader "$game" install > /dev/null
    echo "mine" > "$game/mt2loader/notes.txt"

    output="$(loader "$game" remove)"
    check "foreign files: reported" contains "$output" "notes.txt"
    check "foreign files: kept" test -e "$game/mt2loader/notes.txt"
    check "foreign files: loader's own gone" test ! -e "$game/mt2loader/mt2loader.dll"
}


cd "$ROOT" || exit 1
mkdir -p "$BUILD"

for required in "$ZLIB_DLL" "$BIN/MT2.exe" "$MT2MM"; do
    if [ ! -f "$required" ]; then
        echo "missing $required (run tests/run_tests.sh and build mt2mm first; GAME_ZLIB overrides the game's zlib1.dll path)"
        exit 1
    fi
done

gcc $CFLAGS -shared tests/fake_zlib_extra.c -o "$BIN/zlib_extra.dll" || exit 1

test_install_and_remove
test_installed_by_hand
test_steam_verify
test_core_lost
test_legacy_install
test_foreign_zlib
test_new_zlib_exports
test_game_running
test_not_game_folder
test_foreign_files_kept

echo "$passes passed, $failures failed"
[ "$failures" = 0 ]
