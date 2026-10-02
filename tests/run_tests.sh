#!/usr/bin/env bash
# Runs the proxy and core against a fake MT2.exe, SDL3.dll and libphysfs.dll, without the mod manager.
# Needs MSYS2 MINGW64 gcc and zlib, and the loader built first (mingw32-make).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/test"
DIST="$ROOT/dist"
CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror"
CXXFLAGS="-std=c++20 -O2 -Wall -Wextra -Werror -Wpedantic"
# As the project template links a plugin: no runtime DLLs to ship.
CXX_STATIC="-static -static-libgcc -static-libstdc++"
CORE_CFLAGS="$CFLAGS -D__USE_MINGW_ANSI_STDIO=1 -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00"
CORE_SOURCES="src/core/*.c build/mapped_fields.c src/common/*.c third_party/minhook/src/*.c third_party/minhook/src/hde/hde64.c"
VERSION="$(sed -n 's/#define MT2LOADER_VERSION "\(.*\)"/\1/p' "$ROOT/src/include/mt2loader_version.h")"

failures=0
passes=0


build_fixtures() {
    mkdir -p "$BUILD/bin/test_core"
    gcc $CFLAGS -shared tests/fake_sdl3.c -o "$BUILD/bin/SDL3.dll" -Wl,--out-implib,"$BUILD/bin/libSDL3.dll.a" || exit 1
    gcc $CFLAGS -shared tests/fake_physfs.c -o "$BUILD/bin/libphysfs.dll" -Wl,--out-implib,"$BUILD/bin/libphysfs.dll.a" || exit 1
    # No link timestamp, so a test core can recognize this exe as a known build, named "test".
    # Not stripped: plugins find the fake game's functions in its symbol table, as in the real game.
    # Its C++ half uses std::string, linked in statically as the game does.
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -fno-rtti -c tests/fake_classes.cpp -o "$BUILD/bin/fake_classes.o" || exit 1
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-invalid-offsetof -c tests/fake_reflection.cpp -o "$BUILD/bin/fake_reflection.o" || exit 1
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-invalid-offsetof -c tests/fake_saves.cpp -o "$BUILD/bin/fake_saves.o" || exit 1
    gcc $CFLAGS -c tests/fake_game.c -o "$BUILD/bin/fake_game.o" || exit 1
    g++ "$BUILD/bin/fake_game.o" "$BUILD/bin/fake_classes.o" "$BUILD/bin/fake_reflection.o" "$BUILD/bin/fake_saves.o" -o "$BUILD/bin/MT2.exe" -L"$BUILD/bin" -lSDL3 -lphysfs -lz \
        -static-libstdc++ -static-libgcc -Wl,--no-insert-timestamp || exit 1
    gcc $CFLAGS -municode tests/quiet_launcher.c -o "$BUILD/bin/quiet_launcher.exe" || exit 1
    gcc $CORE_CFLAGS -municode tests/probe_real_exe.c src/common/import_patch.c src/core/game_build.c src/core/text_check.c \
        src/common/pe_image.c -o "$BUILD/bin/probe_real_exe.exe" || exit 1
    gcc $CORE_CFLAGS -Ithird_party/minhook/include -Isrc/core -municode tests/probe_plugin_real_exe.c $CORE_SOURCES build/libcjson_private.a \
        -l:libiberty.a -lshell32 -o "$BUILD/bin/probe_plugin_real_exe.exe" || exit 1
    build_test_core
    build_plugins
}

# Plugins are built from source for each run, with the same header plugin authors use.
build_plugins() {
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s examples/hello_plugin/src/plugin.cpp -o "$BUILD/bin/hello_plugin.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s examples/weapons_any_rig/src/plugin.cpp -o "$BUILD/bin/weapons_any_rig.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s examples/top_spenders/src/plugin.cpp -o "$BUILD/bin/top_spenders.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s examples/quest_markers/src/plugin.cpp -o "$BUILD/bin/quest_markers.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s examples/quests_expanded/src/*.cpp -o "$BUILD/bin/quests_expanded.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s examples/dungeon_captives/src/*.cpp -o "$BUILD/bin/dungeon_captives.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s tests/cpp_plugin.cpp -o "$BUILD/bin/cpp_plugin.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s tests/settings_plugin.cpp -o "$BUILD/bin/settings_plugin.dll" || exit 1
    g++ $CXXFLAGS -Isdk/include -shared $CXX_STATIC -s tests/real_exe_api_plugin.cpp -o "$BUILD/bin/real_exe_api_plugin.dll" || exit 1
    gcc $CFLAGS -Isdk/include -shared -static-libgcc -s tests/api_plugin.c -o "$BUILD/bin/api_plugin.dll" || exit 1
    build_msvc_plugins
}

# The same test plugins built by Visual Studio, when it's installed (MSVC=0 skips them).
build_msvc_plugins() {
    MSVC_BIN=""

    if [ "${MSVC:-1}" = 0 ] || ! command -v cmake > /dev/null; then
        return
    fi

    if cmake -S tests/msvc -B build/msvc -A x64 > "$BUILD/msvc_configure.log" 2>&1 \
        && cmake --build build/msvc --config Release > "$BUILD/msvc_build.log" 2>&1; then
        MSVC_BIN="$ROOT/build/msvc/Release"
    else
        echo "FAIL: Visual Studio build of the test plugins (see $BUILD/msvc_build.log)"
        failures=$((failures + 1))
    fi
}

build_test_core() {
    local image_size
    image_size="$(objdump -p "$BUILD/bin/MT2.exe" | awk '/SizeOfImage/ { print $2 }')"

    gcc $CORE_CFLAGS -Ithird_party/minhook/include -Isrc/core -DMT2LOADER_TEST_IMAGE_SIZE=0x"$image_size" $CORE_SOURCES \
        -shared -static-libgcc -s build/libcjson_private.a -l:libiberty.a -lshell32 -o "$BUILD/bin/test_core/mt2loader.dll" || exit 1
}

new_game_folder() {
    local folder="$BUILD/$1"
    local core="${2:-$DIST/mt2loader/mt2loader.dll}"
    rm -rf "$folder"
    mkdir -p "$folder/mt2loader" "$folder/profile/mod"
    cp "$BUILD/bin/MT2.exe" "$BUILD/bin/SDL3.dll" "$BUILD/bin/libphysfs.dll" "$folder/"
    cp "$DIST/zlib1.dll" "$folder/zlib1.dll"
    cp "$core" "$folder/mt2loader/"
    echo "$folder"
}

new_plugin_game_folder() {
    new_game_folder "$1" "$BUILD/bin/test_core/mt2loader.dll"
}

add_hello_mod() {
    local game="$1"
    local folder="$2"
    local builds="$3"
    cp -r examples/hello_plugin/mod "$game/profile/mod/$folder"
    cp "$BUILD/bin/hello_plugin.dll" "$game/profile/mod/$folder/native/hello_plugin.dll"
    sed -i "s/\"game_builds\": \[.*\]/\"game_builds\": [$builds]/" "$game/profile/mod/$folder/manifest.json"
}

rename_mod() {
    sed -i "s/\"hello_plugin\"/\"$2\"/" "$1/manifest.json"
}

run_game() {
    local exe="$1"
    local argument="${2:-}"
    "$BUILD/bin/quiet_launcher.exe" "$(cygpath -w "$exe")" "$argument"
}

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

log_has() {
    grep -q -- "$2" "$1/mt2loader/loader.log" 2>/dev/null
}

log_lacks() {
    ! log_has "$1" "$2"
}

exit_is() {
    [ "$1" = "$2" ]
}


test_normal_launch() {
    local game
    game="$(new_game_folder normal)"
    run_game "$game/MT2.exe"
    local code=$?

    check "normal: game exits cleanly" exit_is "$code" 0
    check "normal: proxy attached" log_has "$game" "proxy $VERSION attached"
    check "normal: SDL_Init seen" log_has "$game" "SDL_Init called on thread"
    check "normal: core started" log_has "$game" "core $VERSION started (proxy $VERSION)"
    check "normal: fake exe is not a known build" log_has "$game" "Game build not recognized"
    check "normal: code check matches" log_has "$game" "Code in memory matches MT2.exe on disk"
    check "normal: unknown build runs no plugin" log_has "$game" "No plugin runs: the game build isn't recognized"
    check "normal: SDL_Quit seen" log_has "$game" "SDL_Quit called"
    check "normal: session marker removed" test ! -e "$game/mt2loader/session.txt"

    run_game "$game/MT2.exe"
    check "normal: previous log kept" test -s "$game/mt2loader/loader.previous.log"
}

# The fake game compresses and decompresses through zlib1.dll, so a clean exit also proves the proxy's own zlib works.
test_without_loader_folder() {
    local game
    game="$(new_game_folder no_folder)"
    rm -rf "$game/mt2loader"
    run_game "$game/MT2.exe"

    check "no folder: game runs, zlib works" exit_is "$?" 0
    check "no folder: nothing written" test ! -e "$game/mt2loader"
}

test_disabled() {
    local game
    game="$(new_game_folder disabled)"
    touch "$game/mt2loader/disabled"
    run_game "$game/MT2.exe"

    check "disabled: game exits cleanly" exit_is "$?" 0
    check "disabled: says so" log_has "$game" "The loader is disabled"
    check "disabled: core not started" log_lacks "$game" "core $VERSION started"
    check "disabled: no session marker" test ! -e "$game/mt2loader/session.txt"
}

test_core_missing() {
    local game
    game="$(new_game_folder core_missing)"
    rm "$game/mt2loader/mt2loader.dll"
    run_game "$game/MT2.exe"

    check "core missing: game exits cleanly" exit_is "$?" 0
    check "core missing: says so" log_has "$game" "mt2loader.dll couldn't be loaded"
}

test_not_a_core() {
    local game
    game="$(new_game_folder not_a_core)"
    cp "$BUILD/bin/SDL3.dll" "$game/mt2loader/mt2loader.dll"
    run_game "$game/MT2.exe"

    check "not a core: game exits cleanly" exit_is "$?" 0
    check "not a core: says so" log_has "$game" "it isn't a loader core"
}

test_crash_streak() {
    local game
    game="$(new_game_folder crash_streak)"

    run_game "$game/MT2.exe" --crash
    check "crash 1: simulated crash" exit_is "$?" 13
    run_game "$game/MT2.exe" --crash
    check "crash 2: counted" log_has "$game" "didn't exit cleanly (1 in a row"

    run_game "$game/MT2.exe"
    check "safe launch: game exits cleanly" exit_is "$?" 0
    check "safe launch: announced" log_has "$game" "crashed 2 times in a row"
    check "safe launch: core not started" log_lacks "$game" "core $VERSION started"

    run_game "$game/MT2.exe"
    check "after safe launch: core back" log_has "$game" "core $VERSION started"
}

test_crash_without_core() {
    local game
    game="$(new_game_folder crash_without_core)"
    mv "$game/mt2loader/mt2loader.dll" "$game/core.bak"

    run_game "$game/MT2.exe" --crash
    run_game "$game/MT2.exe" --crash
    check "crash without core: not blamed" log_has "$game" "the loader's code wasn't running"

    mv "$game/core.bak" "$game/mt2loader/mt2loader.dll"
    run_game "$game/MT2.exe"
    check "crash without core: no safe launch" log_has "$game" "core $VERSION started"
}

test_other_exe() {
    local game
    game="$(new_game_folder other_exe)"
    mv "$game/MT2.exe" "$game/Other.exe"
    run_game "$game/Other.exe"

    check "other exe: runs" exit_is "$?" 0
    check "other exe: proxy stays passive" test ! -e "$game/mt2loader/loader.log"
}


test_plugin_manual_install() {
    local game
    game="$(new_plugin_game_folder plugin_manual)"
    add_hello_mod "$game" hello_plugin '"test"'
    mkdir -p "$game/profile/mod/data_only_mod"
    echo '{"id": "data_only", "name": "Data only"}' > "$game/profile/mod/data_only_mod/manifest.json"
    run_game "$game/MT2.exe"

    check "plugin: game exits cleanly" exit_is "$?" 0
    check "plugin: waits for the mods folder" log_has "$game" "Plugins start when the game looks for its mods"
    check "plugin: finds the mods folder" log_has "$game" "Looking for plugins in .*profile.mod"
    check "plugin: started" log_has "$game" "\[hello_plugin\] starting native/hello_plugin.dll"
    check "plugin: runs" log_has "$game" "\[hello_plugin\] Hello from hello_plugin: game build test, loader $VERSION"
    check "plugin: summary" log_has "$game" "Plugins: 1 started, from 2 mod folders"
}

test_plugin_other_build() {
    local game
    game="$(new_plugin_game_folder plugin_other_build)"
    add_hello_mod "$game" hello_plugin '"0.30.7"'
    run_game "$game/MT2.exe"

    check "other build: not started" log_has "$game" "made for game build 0.30.7, and this is test"
}

test_plugin_no_mods() {
    local game
    game="$(new_plugin_game_folder plugin_no_mods)"
    add_hello_mod "$game" hello_plugin '"test"'
    run_game "$game/MT2.exe" --no-mods

    check "--no-mods: respected" log_has "$game" "No plugin runs: the game was started with --no-mods"
    check "--no-mods: not started" log_lacks "$game" "starting native"
}

test_plugin_bad_manifests() {
    local game mods
    game="$(new_plugin_game_folder plugin_bad)"
    mods="$game/profile/mod"

    add_hello_mod "$game" broken_json '"test"'
    echo '{ "id": "broken", ' > "$mods/broken_json/manifest.json"

    add_hello_mod "$game" escapes '"test"'
    rename_mod "$mods/escapes" escapes
    sed -i 's|native/hello_plugin.dll|../../escape.dll|' "$mods/escapes/manifest.json"

    add_hello_mod "$game" no_builds ''
    rename_mod "$mods/no_builds" no_builds

    add_hello_mod "$game" missing_dll '"test"'
    rename_mod "$mods/missing_dll" missing_dll
    rm "$mods/missing_dll/native/hello_plugin.dll"

    add_hello_mod "$game" not_a_plugin '"test"'
    rename_mod "$mods/not_a_plugin" not_a_plugin
    cp "$BUILD/bin/SDL3.dll" "$mods/not_a_plugin/native/hello_plugin.dll"

    run_game "$game/MT2.exe"

    check "bad manifests: game still runs" exit_is "$?" 0
    check "bad JSON: reported" log_has "$game" "broken_json: manifest.json isn't valid JSON"
    check "escaping path: refused" log_has "$game" "escapes: loader.plugins has an entry that isn't a .dll path inside the mod"
    check "no builds: refused" log_has "$game" "no_builds: loader.game_builds must list"
    check "missing DLL: reported" log_has "$game" "\[missing_dll\] native/hello_plugin.dll not started: the file isn't there"
    check "not a plugin: reported" log_has "$game" "\[not_a_plugin\] native/hello_plugin.dll not started: it has no plugin_init"
    check "bad manifests: nothing started" log_has "$game" "Plugins: 0 started"
}

test_plugin_two_copies() {
    local game
    game="$(new_plugin_game_folder plugin_two_copies)"
    add_hello_mod "$game" hello_plugin '"test"'
    add_hello_mod "$game" mm_hello_plugin '"test"'
    run_game "$game/MT2.exe"

    check "two copies: second skipped" log_has "$game" "a mod with this id already started its plugins"
    check "two copies: started once" log_has "$game" "Plugins: 1 started, from 2 mod folders"
}

add_api_mod() {
    local game="$1"
    local id="$2"
    local dll="$3"
    local folder="$game/profile/mod/$id"

    mkdir -p "$folder/native"
    cp "$dll" "$folder/native/api_plugin.dll"
    printf '{"id": "%s", "name": "%s", "loader": {"plugins": ["native/api_plugin.dll"], "game_builds": ["test"]}}' "$id" "$id" > "$folder/manifest.json"
}

results_have() {
    grep -q -- "$2" "$1/results.txt" 2>/dev/null
}

count_in_log() {
    grep -c -- "$2" "$1/mt2loader/loader.log" 2>/dev/null
}

test_plugin_api() {
    local game
    game="$(new_plugin_game_folder plugin_api)"
    add_api_mod "$game" api_a "$BUILD/bin/api_plugin.dll"
    add_api_mod "$game" api_b "$BUILD/bin/api_plugin.dll"
    run_game "$game/MT2.exe"

    check "api: game exits cleanly" exit_is "$?" 0
    check "api: symbols read" log_has "$game" "Game symbols: [0-9]* read from MT2.exe"
    check "api: readable names logged once" exit_is "$(count_in_log "$game" "Readable names of the game's symbols ready")" 1
    check "api: every check passed" log_lacks "$game" "FAILED"
    check "api: all checks ran" exit_is "$(count_in_log "$game" ": ok$")" 36
    check "api: ambiguous name explained" log_has "$game" "'FakeCharacter::Overloaded' matches 2 game symbols: FakeCharacter::Overloaded("
    check "api: missing name explained" log_has "$game" "No game symbol is named 'FakeCharacter::Nothing'. Search the names with: mt2sdk find"
    check "api: hook logged" log_has "$game" "\[api_a\] Hooked FakeCharacter::EquipWeaponModel(bool)$"
    check "api: chained hook logged" log_has "$game" "\[api_b\] Hooked FakeCharacter::EquipWeaponModel(bool) (2 hooks on it now; this one runs first)"
    check "api: write logged" log_has "$game" "\[api_a\] Wrote 4 bytes at fake_gold"
    check "api: shared name taken" log_has "$game" "\[api_b\] Couldn't share 'api_a.value': api_a already shares that name"
    check "api: plugin_ready called" log_has "$game" "\[api_b\] native/api_plugin.dll: plugin_ready"
    check "api: both hooks ran, newest first" results_have "$game" "equip=111"
    check "api: unhooked function back to normal" results_have "$game" "overloaded=7"
    check "api: write reached the game" results_have "$game" "gold=42"
    check "api: game still runs its own compare" results_have "$game" "rig=1"
}

# The raw C API from a plugin Visual Studio built.
test_plugin_api_msvc() {
    if [ -z "$MSVC_BIN" ]; then
        echo "skipped: Visual Studio builds (MSVC=0, or no cmake)"

        return
    fi

    local game
    game="$(new_plugin_game_folder plugin_api_msvc)"
    add_api_mod "$game" api_a "$MSVC_BIN/api_plugin.dll"
    add_api_mod "$game" api_b "$MSVC_BIN/api_plugin.dll"
    run_game "$game/MT2.exe"

    check "msvc C api: game exits cleanly" exit_is "$?" 0
    check "msvc C api: every check passed" log_lacks "$game" "FAILED"
    check "msvc C api: all checks ran" exit_is "$(count_in_log "$game" ": ok$")" 36
    check "msvc C api: both hooks ran" results_have "$game" "equip=111"
}

add_cpp_mod() {
    local game="$1"
    local id="$2"
    local dll="$3"
    local folder="$game/profile/mod/$id"

    mkdir -p "$folder/native"
    cp "$dll" "$folder/native/$id.dll"
    printf '{"id": "%s", "name": "%s", "loader": {"plugins": ["native/%s.dll"], "game_builds": ["test"]}}' "$id" "$id" "$id" > "$folder/manifest.json"
}

# Settings as a mod declares them, and the player's values as the mod manager writes them: count beyond its max,
# enabled of the wrong type (so its default), ratio and title not set.
add_cpp_settings() {
    cat > "$1/config.json" <<'JSON'
[
    { "key": "count", "type": "int", "default": 3, "min": 0, "max": 10 },
    { "key": "ratio", "type": "float", "default": 0.5 },
    { "key": "enabled", "type": "bool", "default": false },
    { "key": "mode", "type": "choice", "default": "a", "options": [{ "value": "a", "label": "A" }, { "value": "b", "label": "B" }] },
    { "key": "title", "type": "string", "default": "hi" }
]
JSON
    printf '{ "count": 25, "mode": "b", "enabled": "yes" }' > "$1/settings.json"
}

imports_no_runtime() {
    ! objdump -p "$1" | grep -i -q -E "DLL Name: (libstdc\+\+|libgcc|libwinpthread|vcruntime|msvcp)"
}

# The C++ wrapper, from a plugin built by one compiler: every call, hook, string and undo against the fake game.
test_cpp_plugin() {
    local label="$1"
    local dll="$2"
    local game
    game="$(new_plugin_game_folder "cpp_$label")"
    add_cpp_mod "$game" cpp_main "$dll"
    add_cpp_mod "$game" cpp_fail "$dll"
    add_cpp_settings "$game/profile/mod/cpp_main"
    run_game "$game/MT2.exe"

    check "$label C++: game exits cleanly" exit_is "$?" 0
    check "$label C++: no runtime DLLs to ship" imports_no_runtime "$dll"
    check "$label C++: every check passed" log_lacks "$game" "FAILED"
    check "$label C++: all checks ran" exit_is "$(count_in_log "$game" "\[cpp_main\] check .*: ok$")" 82
    check "$label C++: declarations match the game" log_lacks "$game" "Check the declaration"
    check "$label C++: throwing hook logged once" exit_is "$(count_in_log "$game" "The hook on FakeCharacter::Overloaded(int) failed: this hook fails on purpose. The game's own code ran instead")" 1
    check "$label C++: failed start explained" log_has "$game" "\[cpp_fail\] Not started: stopping on purpose. Its 13 changes are undone"
    check "$label C++: hook chained" results_have "$game" "equip=11$"
    check "$label C++: throwing hook fell back to the game" results_have "$game" "overloaded=7$"
    check "$label C++: failing code after a function doesn't run it again" results_have "$game" "counted_once=1$"
    check "$label C++: failing code after keeps the game's result" results_have "$game" "doubled=42$"
    check "$label C++: failing code after a function with a result doesn't run it again" results_have "$game" "counted_twice=2$"
    check "$label C++: failing code after logged" log_has "$game" "The code after FakeCharacter::Doubled(int) const failed: this code after fails on purpose too. The game's own result stands"
    check "$label C++: before ran with the arguments, failed start's patch undone" results_have "$game" "gold=105$"
    check "$label C++: removed hook gone" results_have "$game" "rig=1$"
    check "$label C++: string returned from a hook" results_have "$game" "title=Fake character with 1 weapons (hooked)$"
    check "$label C++: long string made by a hook" results_have "$game" "greet=Hi Player, from a hook with a long text$"
    check "$label C++: float hook" results_have "$game" "scale=2.5$"
    check "$label C++: 8-byte struct hook" results_have "$game" "position2=1.5,7$"
    check "$label C++: 12-byte struct hook with a capture" results_have "$game" "position3=2,4,101$"
    check "$label C++: 12-byte struct call hook" results_have "$game" "reach=1107$"
    check "$label C++: failed start's call hook undone" results_have "$game" "weapons_humanoid=2$"
    check "$label C++: call picked by its text says yes" results_have "$game" "weapons_dragon=1$"
    check "$label C++: number changed" results_have "$game" "limit=1234$"
    check "$label C++: text changed" results_have "$game" "motto=a changed motto that is longer$"
    check "$label C++: before with floats and stack arguments" results_have "$game" "seen=22$"
    check "$label C++: result untouched by before" results_have "$game" "sum6=22$"
    check "$label C++: one lambda in two places" results_have "$game" "first=50$"
    check "$label C++: one lambda in two places, second" results_have "$game" "second=50$"
    check "$label C++: fields by name, list and singleton" results_have "$game" "total=42$"
    check "$label C++: list grown as the game grows it, then reserved" results_have "$game" "helpers=5 of 16$"
    check "$label C++: text field changed by name" results_have "$game" "toon_name=Renamed by a plugin, with a long name$"
    check "$label C++: created object destroyed by the game's destructor" results_have "$game" "boards_destroyed=1$"
    check "$label C++: game's own enum words still read" results_have "$game" "load_level=0$"
    check "$label C++: new enum word read from data" results_have "$game" "load_spenders=100$"
    check "$label C++: unknown enum word still refused" results_have "$game" "load_unknown=-1$"
    check "$label C++: new enum word converted from text" results_have "$game" "convert_spenders=100$"
    check "$label C++: new enum value converted to its word" results_have "$game" "word_of_spenders=spenders$"
    check "$label C++: the game's own enum words unchanged" results_have "$game" "word_of_gold=gold$"
    check "$label C++: new enum word written" results_have "$game" "written_spenders=spenders$"
    check "$label C++: new enum word saved with a field" results_have "$game" "saved_field_spenders=spenders$"
    check "$label C++: new enum word loaded into a field" results_have "$game" "loaded_field_spenders=100$"
    check "$label C++: unknown enum word still refused by a field" results_have "$game" "loaded_field_unknown=-1$"
    check "$label C++: remembered values saved" grep -q "^greeting = hello there$" "$game/profile/mt2loader/cpp_main/remembered.txt"
    check "$label C++: missing setting explained" log_has "$game" "\[cpp_main\] No setting 'nothing': config.json declares no setting 'nothing'"
    check "$label C++: saved values ready" log_has "$game" "Plugin values in saved games: ready (4 places in the game's code save objects, 3 kinds of object)"
    check "$label C++: values in text files ready" log_has "$game" "Plugin values in the game's text files (like a save's rules.vrt): ready"
    check "$label C++: bad saved key explained" log_has "$game" "\[cpp_main\] 'bad key!' can't be a key for a saved value"
    check "$label C++: unsaved object explained" log_has "$game" "\[cpp_main\] Couldn't keep 'x' in the saved game: the game doesn't save this object the usual way"
    check "$label C++: save still reads as the game's" results_have "$game" "save_intact=1$"
    check "$label C++: one extra field per object with values" results_have "$game" "save_plugin_fields=2$"
    check "$label C++: linked object saved too" results_have "$game" "save_link_ids=1$"
    check "$label C++: save counted" log_has "$game" "Saved plugin values on 2 game objects"
    check "$label C++: new object inherits no values" results_have "$game" "fresh_marker=none none visits=0 first=none$"
    check "$label C++: game's own fields load" results_have "$game" "loaded_gold=250$"
    check "$label C++: game's own objects load" results_have "$game" "loaded_npcs=2$"
    check "$label C++: values back after loading" results_have "$game" "loaded_npc0=1 question #FF8800 visits=3 first=self$"
    check "$label C++: object without values" results_have "$game" "loaded_npc1=2 none none visits=3 first=other$"
    check "$label C++: a link goes with the object it points at" results_have "$game" "after_link_target_deleted=none none visits=3 first=none$"
    check "$label C++: values written to a text file" results_have "$game" "rules_plugin_fields=1$"
    check "$label C++: text file keeps the game's fields" results_have "$game" "rules_fields=2$"
    check "$label C++: game skips the values' field" results_have "$game" "rules_skipped=1$"
    check "$label C++: values back from a text file" results_have "$game" "rules_loaded=1 kept$"
    check "$label C++: load counted" log_has "$game" "Loaded plugin values on 2 game objects"
}

test_cpp_plugins() {
    test_cpp_plugin gcc "$BUILD/bin/cpp_plugin.dll"

    if [ -n "$MSVC_BIN" ]; then
        test_cpp_plugin msvc "$MSVC_BIN/cpp_plugin.dll"
    fi
}

# What a new plugin author does: mt2sdk new, then build the project as it comes, with each compiler.
test_sdk_new_project() {
    local folder="$BUILD/sdk_new"
    local project="$folder/Wing_Flaps"
    local output
    rm -rf "$folder"
    mkdir -p "$folder"

    output="$("$DIST/sdk/mt2sdk.exe" new "$(cygpath -w "$project")" "Wing Flaps")"
    check "sdk new: project made" grep -q "mod id wing_flaps" <<< "$output"
    check "sdk new: C++ header copied" test -f "$project/include/mt2loader.hpp"
    check "sdk new: C layer copied" test -f "$project/include/mt2loader.h"

    # Parameters are named for readability even when unused, as a modder would write them.
    g++ $CXXFLAGS -Wno-unused-parameter -I"$project/include" -shared $CXX_STATIC -s "$project"/src/*.cpp -o "$folder/gcc_wing_flaps.dll"
    check "sdk new: GCC builds the template" test -f "$folder/gcc_wing_flaps.dll"

    if [ -z "$MSVC_BIN" ]; then
        return
    fi

    cmake -S "$project" -B "$folder/build" -A x64 -DCOPY_TO_GAME=OFF > "$folder/configure.log" 2>&1 \
        && cmake --build "$folder/build" --config Release > "$folder/build.log" 2>&1
    check "sdk new: Visual Studio builds the template into mod/native" test -f "$project/mod/native/wing_flaps.dll"

    output="$("$DIST/sdk/mt2sdk.exe" check "$(cygpath -w "$project/mod")" 2>&1)"
    check "sdk new: the built mod passes mt2sdk check" grep -q "0 problem" <<< "$output"
}

# C++ code that includes the raw C header is pointed to the wrapper.
test_c_header_in_cpp() {
    local output
    output="$(echo '#include <mt2loader.h>' | g++ -std=c++20 -fsyntax-only -Isdk/include -x c++ - 2>&1)"

    check "C header in C++: refused" grep -q "include <mt2loader.hpp> instead" <<< "$output"
}

test_plugin_crash_streak() {
    local game
    game="$(new_plugin_game_folder plugin_crash)"
    add_hello_mod "$game" hello_plugin '"test"'

    run_game "$game/MT2.exe" --crash
    run_game "$game/MT2.exe" --crash
    run_game "$game/MT2.exe"

    check "crashes: safe launch skips plugins" log_lacks "$game" "starting native"
}


# Maps the real game's exe without running it: set MT2_EXE to its path to include this.
test_real_exe() {
    if [ -z "${MT2_EXE:-}" ]; then
        echo "skipped: real MT2.exe probe (set MT2_EXE)"

        return
    fi

    local output
    output="$("$BUILD/bin/probe_real_exe.exe" "$(cygpath -w "$MT2_EXE")")"
    local code=$?
    echo "$output"

    check "real exe: probe succeeds" exit_is "$code" 0
    check "real exe: known build" grep -q "build: 0.30.7" <<< "$output"
    check "real exe: code on disk readable" grep -q " 0 different" <<< "$output"

    # The example mod against the real game's code: it must find both rig compares by name and text, send exactly
    # those two calls into the plugin (which says yes), and leave the branches after them alone.
    output="$("$BUILD/bin/probe_plugin_real_exe.exe" "$(cygpath -w "$MT2_EXE")" "$(cygpath -w "$BUILD/bin/weapons_any_rig.dll")" \
        weapons_any_rig call:3a1e4a call:6b3099 3a1e51 6b30a4)"
    code=$?
    echo "$output"

    check "real exe: weapons mod starts" exit_is "$code" 0
    check "real exe: weapons mod finds the character check" grep -q "Hooked the call at mmoCharacter::EquipWeaponModel(bool)+0x5a$" <<< "$output"
    check "real exe: weapons mod finds the editor check" grep -q "Hooked the call at mmoCostumeEditorView::_UpdateGrid()+0x89$" <<< "$output"
    check "real exe: character check says yes" grep -q "rva 003a1e4a: e8 .* -> the plugin, returns 1" <<< "$output"
    check "real exe: editor check says yes" grep -q "rva 006b3099: e8 .* -> the plugin, returns 1" <<< "$output"
    check "real exe: branches untouched" grep -q "rva 003a1e51: 75 5d" <<< "$output"
    # Every place an object's fields are saved: vsObject<...>::SaveValuesToStream, and where the compiler inlined it
    # (into SaveToStream, or into a list's save, as for the NPCs).
    check "real exe: values in saved games ready" grep -q "Plugin values in saved games: ready (1680 places in the game's code save objects, 1262 kinds of object)" <<< "$output"

    # The leaderboard example: every name it uses is in the game, and each call it picks is the only one of its kind.
    rm -rf "$BUILD/probe_top_spenders"
    mkdir -p "$BUILD/probe_top_spenders/native"
    cp examples/top_spenders/mod/config.json "$BUILD/probe_top_spenders/"
    cp "$BUILD/bin/top_spenders.dll" "$BUILD/probe_top_spenders/native/"
    output="$("$BUILD/bin/probe_plugin_real_exe.exe" "$(cygpath -w "$MT2_EXE")" "$(cygpath -w "$BUILD/probe_top_spenders/native/top_spenders.dll")" \
        top_spenders 724954 7252c7)"
    code=$?
    echo "$output"

    check "real exe: top spenders mod starts" exit_is "$code" 0
    check "real exe: top spenders mod reads its settings" grep -q "biggest spenders (counting: lifetime, from 1)$" <<< "$output"
    check "real exe: top spenders mod teaches the tab's type" grep -q 'mmoLeaderboard::Type reads "spenders" as 100$' <<< "$output"
    check "real exe: top spenders mod finds the number formatting" grep -q "Hooked the call at mmoLeaderboardView::UpdateUI(float)+0x134$" <<< "$output"
    check "real exe: top spenders mod finds the tab decision" grep -q "Hooked the call at mmoLeaderboardsWindow::OnShow()+0xb7$" <<< "$output"
    check "real exe: number formatting goes to the plugin" grep -q "rva 00724954: e8 .* -> the plugin" <<< "$output"
    check "real exe: tab decision goes to the plugin" grep -q "rva 007252c7: e8 .* -> the plugin" <<< "$output"

    # The quest markers example: every game function it calls is found, and it reads from the game's own code where a
    # quest giver keeps its marker and its shard.
    rm -rf "$BUILD/probe_quest_markers"
    mkdir -p "$BUILD/probe_quest_markers/native"
    cp examples/quest_markers/mod/config.json "$BUILD/probe_quest_markers/"
    cp "$BUILD/bin/quest_markers.dll" "$BUILD/probe_quest_markers/native/"
    output="$("$BUILD/bin/probe_plugin_real_exe.exe" "$(cygpath -w "$MT2_EXE")" "$(cygpath -w "$BUILD/probe_quest_markers/native/quest_markers.dll")" quest_markers saves:mmoNPC saves:mmoToon)"
    code=$?
    echo "$output"

    check "real exe: quest markers mod starts" exit_is "$code" 0
    check "real exe: quest markers mod finds the marker and shard" grep -q "default marker: game #FDFE0C 1.00 0.00; marker at +0x810, shard at +0x170; " <<< "$output"
    check "real exe: quest markers mod finds a model's fragments" grep -q "; model levels at +0xa8 +0xb0, fragments at +0x8 +0x10; " <<< "$output"
    check "real exe: quest markers mod finds a slider's value" grep -q "; slider value at +0x4d0)$" <<< "$output"
    check "real exe: quest markers mod colors the marker" grep -q "Hooked the call at mmoNPC::_PlaceMarker() \[clone .part.0\]+0x14e$" <<< "$output"
    check "real exe: quest markers mod follows the Quests tab" exit_is "$(grep -c "Hooked the call at mmoNPCInfoWindow::InitContents()" <<< "$output")" 4
    # Plugin values can be kept on quest givers (as the markers are) and on players (as the Quests Expanded mod does).
    check "real exe: values kept on quest givers" grep -q "^saves mmoNPC: yes$" <<< "$output"
    check "real exe: values kept on players" grep -q "^saves mmoToon: yes$" <<< "$output"

    # The Quests Expanded mod: each call it changes is the only one of its kind. What the game doesn't name, it reaches by
    # the names mt2-mappings gives it, each confirmed in the game's code.
    rm -rf "$BUILD/probe_quests_expanded"
    mkdir -p "$BUILD/probe_quests_expanded/native"
    cp examples/quests_expanded/mod/config.json "$BUILD/probe_quests_expanded/"
    cp "$BUILD/bin/quests_expanded.dll" "$BUILD/probe_quests_expanded/native/"
    local quests_fields=("field:mmoPane::visible" "field:mmoButtonPane::tooltipPane" "field:mmoQuestSelector::quest" "field:mmoAdvertisement::action"
        "field:mmoAdvertisement::advanceNeed" "field:mmoAdvertisement::lootNeed" "field:mmoAdvertisement::position" "field:mmoNPCAdvertisement::npc"
        "field:mmoQuest::advertisement" "field:mmoRegion::npcs" "field:mmoReleaseDemand::changeKeys" "field:mmoToonDoQuestAction::toon"
        "field:mmoToonDoQuestAction::quest" "field:mmoQuestDisplay::quest" "field:mmoQuestDisplay::arrows" "field:mmoQuestList::npc"
        "field:mmoToonPlanTask::toon" "field:mmoQuestAdvertisement::quest" "field:mmoArrow::from" "field:mmoArrow::to"
        "field:mmoArrow::changed" "field:vsPool<T>::m_totalCount" "field:mmoObject::uid" "field:mmoBuilding::dungeon"
        "field:mmoDungeonInstance::dungeon" "field:vsTransform3D::m_translation")
    output="$("$BUILD/bin/probe_plugin_real_exe.exe" "$(cygpath -w "$MT2_EXE")" "$(cygpath -w "$BUILD/probe_quests_expanded/native/quests_expanded.dll")" quests_expanded 3ec662 3ec555 3ec58b 3e277b 3ec7ac 3eb27b 3ec792 45b652 saves:mmoCustomRules "${quests_fields[@]}")"
    code=$?
    echo "$output"

    check "real exe: Quests Expanded starts" exit_is "$code" 0
    check "real exe: every mapped field Quests Expanded uses is confirmed" exit_is "$(grep -c "^field .*: +0x" <<< "$output")" 26
    check "real exe: a weak pointer's mapping" grep -q "^field mmoQuestSelector::quest: +0x3d8 vsWeakPointer<mmoQuest>$" <<< "$output"
    check "real exe: a template's mapping" grep -q "^field vsPool<T>::m_totalCount: +0x18 int$" <<< "$output"
    check "real exe: a destructor with its work copied in is watched twice" exit_is "$(grep -c "Hooked mmoDoSetNPCQuests::~mmoDoSetNPCQuests()" <<< "$output")" 2
    check "real exe: hand-ins take the quest they're given" grep -q "rva 003ec662: e8 .* -> the plugin" <<< "$output"
    check "real exe: hand-ins are given by offline quest givers too" grep -q "rva 003ec555: e8 .* -> the plugin" <<< "$output"
    check "real exe: level gates when taking a quest" grep -q "rva 003ec58b: e8 .* -> the plugin" <<< "$output"
    check "real exe: level gates when finding quest givers" grep -q "rva 003e277b: e8 .* -> the plugin" <<< "$output"
    check "real exe: level gates when planning" grep -q "rva 003ec7ac: e8 .* -> the plugin" <<< "$output"
    check "real exe: main questline places kept" grep -q "rva 003eb27b: e8 .* -> the plugin" <<< "$output"
    check "real exe: players invited back when a line grows" grep -q "rva 003ec792: e8 .* -> the plugin" <<< "$output"
    check "real exe: quest cards made empty slots" exit_is "$(grep -c "Hooked mmoQuestSelector::SetupAs" <<< "$output")" 2
    check "real exe: the Quests report's window is loaded with the game's" grep -q "rva 0045b652: e8 .* -> the plugin" <<< "$output"
    check "real exe: new thoughts kept in saved games" exit_is "$(grep -c "Hooked .*<mmoToonThought::Type" <<< "$output")" 10
    check "real exe: new release demand kept in saved games" exit_is "$(grep -c "Hooked .*<mmoReleaseDemand::Type" <<< "$output")" 10
    check "real exe: story demand follows the release manager" exit_is "$(grep -c "Hooked mmoReleaseManager::\(_SetupNewDemands\|DoRelease\)\|Hooked the call at mmoReleaseManager::_BuildHeat_NewDay" <<< "$output")" 3
    check "real exe: new main questline chapters are release features" grep -q "Hooked mmoRelease::_ScanFor_NewDungeons()" <<< "$output"
    check "real exe: the Quests report and the settings and details windows follow their windows" exit_is "$(grep -c "Hooked mmoWindow::\(Show(bool, bool) \[clone .part.0\]\|UpdateUI\|UICommand\)" <<< "$output")" 5
    # The Story demand is a custom rule: its checkbox in the game's lists, and its value in each saved game's rules.vrt.
    check "real exe: values in the game's text files ready" grep -q "Plugin values in the game's text files (like a save's rules.vrt): ready" <<< "$output"
    check "real exe: values kept on rules" grep -q "^saves mmoCustomRules: yes$" <<< "$output"
    check "real exe: custom rule follows the rules" exit_is "$(grep -c "Hooked \(mmoNewGameWindow::_PopulateCustomRulesGrid\|mmoCustomRules::SetupGrid\|mmoPane::Command\|mmoModeInGame::SetNewGameParameters\|vsObject<mmoCustomRules, vsNullObject>::\(SaveToFilename\|LoadFromRecord\)\)" <<< "$output")" 7
    check "real exe: custom rules forget the last game's" grep -q "^\[quests_expanded\] Hooked mmoModeInGame::DoInit()$" <<< "$output"
    check "real exe: custom rules make room while the game sizes its list" exit_is "$(grep -c "Hooked the call at mmoCustomRules::SetupGrid" <<< "$output")" 2
    check "real exe: custom rule added" grep -q "Custom rule \"quests_expanded_disable_story\" added" <<< "$output"

    # The Dungeon Captives mod, and what it uses: a gizmo type's variants and a character type's colors from
    # mt2-mappings, confirmed in the game's code, and the size of a character's model read from it.
    rm -rf "$BUILD/probe_dungeon_captives"
    mkdir -p "$BUILD/probe_dungeon_captives/native"
    cp "$BUILD/bin/dungeon_captives.dll" "$BUILD/probe_dungeon_captives/native/"
    output="$("$BUILD/bin/probe_plugin_real_exe.exe" "$(cygpath -w "$MT2_EXE")" "$(cygpath -w "$BUILD/probe_dungeon_captives/native/dungeon_captives.dll")" dungeon_captives \
        field:mmoGizmoDefinition::variants field:mmoCharacterType::colors field:mmoCharacterType::nothing size:mmoActor size:mmoCharacterActor)"
    code=$?
    echo "$output"

    check "real exe: Dungeon Captives starts" exit_is "$code" 0
    check "real exe: mapped field confirmed" grep -q "^field mmoGizmoDefinition::variants: +0xa8 vsArrayStore<mmoGizmoVariant>$" <<< "$output"
    check "real exe: mapped field of a plain type confirmed" grep -q "^field mmoCharacterType::colors: +0x18 int$" <<< "$output"
    check "real exe: a field mt2-mappings lacks" grep -q "^field mmoCharacterType::nothing: not mapped$" <<< "$output"
    check "real exe: a class's size from the game's code" grep -q "^size mmoActor: 0x48$" <<< "$output"
    check "real exe: a bigger class's size" grep -q "^size mmoCharacterActor: 0xe0$" <<< "$output"
    check "real exe: captives are picked in the character window" exit_is "$(grep -c "Hooked mmoCursorBehaviourGizmo::\(Activate\|Update\|Deactivate\)" <<< "$output")" 5

    # What the API reads from the game's code on its own: arrays' classes, a virtual function's slot, which destructors
    # to watch.
    rm -rf "$BUILD/probe_api"
    mkdir -p "$BUILD/probe_api/native"
    cp "$BUILD/bin/real_exe_api_plugin.dll" "$BUILD/probe_api/native/"
    output="$("$BUILD/bin/probe_plugin_real_exe.exe" "$(cygpath -w "$MT2_EXE")" "$(cygpath -w "$BUILD/probe_api/native/real_exe_api_plugin.dll")" probe_api)"
    code=$?
    echo "$output"

    check "real exe: API lookups start" exit_is "$code" 0
    check "real exe: arrays of the game's types, spaces or not" grep -q "Arrays of the game's types found" <<< "$output"
    check "real exe: a destructor whose work is copied into the deleting one" exit_is "$(grep -c "Hooked mmoDoSetNPCQuests::~mmoDoSetNPCQuests()" <<< "$output")" 2
    check "real exe: a destructor the deleting one calls" exit_is "$(grep -c "Hooked mmoNPC::~mmoNPC()" <<< "$output")" 1
}

# The research commands against the real game, checked with what LOADER.md and the weapons example already know.
test_sdk_research() {
    if [ -z "${MT2_EXE:-}" ]; then
        echo "skipped: mt2sdk research commands (set MT2_EXE)"
        return
    fi

    local sdk="$DIST/sdk/mt2sdk.exe"
    local exe
    local output
    exe="$(cygpath -w "$MT2_EXE")"

    output="$("$sdk" dis "mmoCharacter::EquipWeaponModel(bool)" --exe "$exe")"
    check "sdk dis: the rig check's text" grep -q '^  +0x53 .*lea rdx, \[0x14148deb2\] *; "humanoid"$' <<< "$output"
    check "sdk dis: the rig check's call" grep -q '^  +0x5a .*call 0x1411e3aa0 *; bool std::operator==<' <<< "$output"
    check "sdk dis: jumps inside the function" grep -q '^  +0x4d .*jle 0x1403a1e9c *; +0xac$' <<< "$output"

    output="$("$sdk" uses "mmoCharacter::EquipWeaponModel(bool)" --exe "$exe")"
    check "sdk uses: calls" grep -q "^    mmoSkeletonInstance::SetWeaponAttachment(" <<< "$output"
    check "sdk uses: texts" grep -q '^    "hand"$' <<< "$output"

    output="$("$sdk" refs "mmoCharacter::EquipWeaponModel(bool)" --exe "$exe")"
    check "sdk refs: callers" grep -q "^  mmoNPC::Generate(mmoCharacterTypeID const&, bool)+0x8b  *0x1403c717b  call$" <<< "$output"

    output="$("$sdk" refs mmoWindow::OnShow --exe "$exe")"
    check "sdk refs: a virtual's slot in each vtable" grep -q "^  vtable for mmoActionBar+0xc0 .* table (virtual, slot 22)$" <<< "$output"
    check "sdk refs: where it's compared, not called (inlined)" grep -q "^  mmoWindow::Show(bool, bool) \[clone .part.0\]+0x12e .* address$" <<< "$output"

    output="$("$sdk" text humanoid --exe "$exe")"
    check "sdk text: where a text is used" grep -q "^  mmoCostumeEditorView::_UpdateGrid()+0x82 .* address$" <<< "$output"

    output="$("$sdk" vtable mmoWindow --exe "$exe")"
    check "sdk vtable: slots" grep -q "^   22  +0xb0    mmoWindow::OnShow()$" <<< "$output"

    output="$("$sdk" vtable mmoDesktopLauncherWindow --exe "$exe")"
    check "sdk vtable: a second base's table" grep -q "^  table for the base at +0x18$" <<< "$output"

    output="$("$sdk" dis mmoNothingHere --exe "$exe" 2>&1)"
    check "sdk dis: unknown name explained" grep -q "mmoNothingHere isn't a name in the game. Search with: mt2sdk find <words>" <<< "$output"

    # Each kind of mistake in a mapping file, with its line.
    output="$("$sdk" mappings check "$(cygpath -w "$ROOT/tests/mappings_bad")" --exe "$exe")"
    check "mappings: unknown class" grep -q "^bad.mapping:1: no function, global or type info in the game belongs to mmoNothing$" <<< "$output"
    check "mappings: offset past the size" grep -q "^bad.mapping:4: 0x170 is past the end of mmoCharacter (0x100 bytes)$" <<< "$output"
    check "mappings: seen place inside an instruction" grep -q "^bad.mapping:5: +0x25d isn't the start of an instruction in mmoCharacter::EquipWeaponModel(bool)" <<< "$output"
    check "mappings: two fields at one offset" grep -q "^bad.mapping:6: mmoCharacter already has a field shard at 0x170 (line 4)$" <<< "$output"
    check "mappings: unknown method" grep -q "^bad.mapping:7: mmoCharacter::Nope(int) isn't a name in the game$" <<< "$output"
    check "mappings: tabs refused" grep -q "^bad.mapping:8: indent with 4 spaces, not tabs$" <<< "$output"
    check "mappings: size against the code" grep -q "^bad.mapping:10: the game makes each mmoActor with 0x48 bytes (operator new before its constructor), not 0x40$" <<< "$output"
    check "mappings: a field the game already names, in a base class" grep -q "^bad.mapping:12: 0x6f8 is already the game's field mmoCharacter::level (int at 0x6f8)$" <<< "$output"

    # Unnamed slots that hold an address (how MinGW reaches vtables and globals) are named by what they hold.
    output="$("$sdk" dis "mmoToon::GetSelfAdvertisements()" --exe "$exe")"
    check "sdk dis: a slot named by what it holds" grep -q "^  +0x19a .*; address of vtable for mmoNPCAdvertisement$" <<< "$output"

    output="$("$sdk" mappings json "$(cygpath -w "$ROOT/tests/mappings_sizes")" "$(cygpath -w "$BUILD/mappings_sizes.json")" --exe "$exe")"
    check "mappings json: a class's size from the code" grep -q '"size":	72,' "$BUILD/mappings_sizes.json"
    check "mappings json: a method's address" grep -q '"address":	"0x140022220"' "$BUILD/mappings_sizes.json"

    # The game's classes as C++ for plugins: made from the game and the mappings, and every kind of function they have
    # compiles as a plugin uses it.
    if [ -d "$ROOT/../mt2-mappings" ]; then
        mkdir -p "$BUILD/headers"
        output="$("$sdk" headers "$(cygpath -w "$BUILD/headers/mt2game.hpp")" --mappings "$(cygpath -w "$ROOT/../mt2-mappings")" --exe "$exe")"
        check "sdk headers: written" grep -q "classes written to" <<< "$output"
        check "sdk headers: the game's own field, by its name" grep -q 'inline int& mmoCharacter::level() const { return game::field<int>(self, "mmoCharacter::level"); }' "$BUILD/headers/mt2game.hpp"
        check "sdk headers: a mapped field, by its mapped name" grep -q 'inline int& mmoCharacterType::colors() const { return game::field<int>(self, "mmoCharacterType::colors"); }' "$BUILD/headers/mt2game.hpp"
        check "sdk headers: an enum with the game's words" grep -q "enum class mmoNPC_State : int { Idle = 0, Combat = 1, Dead = 2 };" "$BUILD/headers/mt2game.hpp"
        g++ $CXXFLAGS -fsyntax-only -Isdk/include -I"$BUILD/headers" tests/game_classes_usage.cpp > "$BUILD/headers/compile.log" 2>&1
        check "sdk headers: compile with a plugin that uses them" exit_is "$?" 0
    fi

    # What the exe says about a class and an enum, without Ghidra.
    output="$("$sdk" class mmoNPC --exe "$exe")"
    check "sdk class: built on" grep -q "^class mmoNPC : vsObject<mmoNPC, mmoCharacter>    0x980 bytes$" <<< "$output"
    check "sdk class: source file" grep -q "^  source: Games/MMORPG/MapEntities/MMO_NPC.cpp$" <<< "$output"
    check "sdk class: a field the game names" grep -q "+0x834   mmoNPC::State *state" <<< "$output"

    output="$("$sdk" enum mmoNPC::State --exe "$exe")"
    check "sdk enum: the game's words" exit_is "$(grep -c "Idle\|Combat\|Dead" <<< "$output")" 3

    # A page per class to share (names only), and an update check that finds nothing between a build and itself.
    rm -rf "$BUILD/reference"
    output="$("$sdk" reference "$(cygpath -w "$BUILD/reference")" --exe "$exe")"
    check "sdk reference: pages written" grep -q "class pages written to" <<< "$output"
    check "sdk reference: a class page" grep -q "^Built on \[mmoCharacter\](mmoCharacter.md) (through \`vsObject<mmoNPC, mmoCharacter>\`)" "$BUILD/reference/classes/mmoNPC.md"

    output="$(cd "$BUILD" && "$sdk" diff "$exe" --exe "$exe")"
    check "sdk diff: a build against itself" grep -q "^Functions: 0 added, 0 removed, 0 changed size$" <<< "$output"

    # A new project gets the game's classes and debugger settings with the game's folder in them.
    rm -rf "$BUILD/sdk_new_devkit"
    "$sdk" new "$(cygpath -w "$BUILD/sdk_new_devkit/Kit_Test")" --exe "$exe" > /dev/null
    check "sdk new: the game's classes" test -f "$BUILD/sdk_new_devkit/Kit_Test/include/mt2game.hpp"
    check "sdk new: VS Code attaches to the game" grep -q '"request": "attach"' "$BUILD/sdk_new_devkit/Kit_Test/.vscode/launch.json"
    check "sdk new: Visual Studio starts the game" grep -q 'MT2.exe' "$BUILD/sdk_new_devkit/Kit_Test/.vs/launch.vs.json"

    # The mappings project next to this repository, when it's there, checks clean against the game it describes.
    if [ -d "$ROOT/../mt2-mappings" ]; then
        output="$("$sdk" mappings check "$(cygpath -w "$ROOT/../mt2-mappings")" --exe "$exe")"
        check "mappings: mt2-mappings checks clean" grep -q " 0 problems$" <<< "$output"
    fi
}

# The export's folding of copied-in engine code, on a decompiled function; needs a JDK (skipped without one).
test_ghidra_folds() {
    local java_bin="${JAVA_HOME:+$JAVA_HOME/bin}"
    local javac="${java_bin:+$java_bin/}javac"
    local out="$BUILD/fold"

    if ! command -v "$javac" > /dev/null; then
        echo "skipped: Ghidra folds (no JDK)"

        return
    fi

    rm -rf "$out" && mkdir -p "$out"
    "$javac" -nowarn -d "$(cygpath -w "$out")" tools/ghidra/MT2Fold.java tools/ghidra/MT2Idioms.java tests/fold/FoldCheck.java 2> /dev/null
    local folded
    folded="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/decompiled.txt)"

    check "folds: an assert is vsAssert with its source line" grep -qF 'vsAssert(m_scene, "Trying to build a mmoBlueprint as a group without a scene being set"); // MMO_Blueprint.cpp line 653' <<< "$folded"
    check "folds: formatting and vsLog_ are vsLog" grep -qF 'vsLog("Scenery object \'"'"'%s:%d\'"'"' has no renderable instance model??", uVar14,' <<< "$folded"
    check "folds: a piecewise copy is one assignment" grep -qF 'local_1e8[0] = *(vsTransform3D *)mmoGroup::GetPropLocalTransform(group,(int)lVar18);' <<< "$folded"
    check "folds: vsArray's inlined add is AddItem" grep -qF 'this->modelInstances.AddItem(modelInstance);' <<< "$folded"
    check "folds: an add of an object copied in pieces" grep -qF 'this->matrix4x4s.AddItem(*(vsMatrix4x4 *)puVar11);' <<< "$folded"
    check "folds: __dynamic_cast is dynamic_cast" grep -qF 'dynamic_cast<mmoScenery*>(lVar8)' <<< "$folded"
    check "folds: a dynamic_cast over two lines" grep -qF 'pvVar6 = dynamic_cast<mmoScenery*>(*(void **)(local_280 + lVar18 * 8));' <<< "$folded"
    check "folds: a local vsArray's cleanup is left out" bash -c '! grep -q "PTR__vsArray" <<< "$1"' _ "$folded"
    check "folds: unused locals are left out" bash -c '! grep -q "uStack_220" <<< "$1"' _ "$folded"

    local bounds
    bounds="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/bounds.txt)"
    check "folds: a copied-in assert with a made message is vsAssertF" grep -qF 'vsAssertF(id >= 0 && id < m_arrayLength, "Out of bounds vsArray access: requested element %d, capacity of %d (array of %s)", index, *(int *)(lVar2 + 0x108), "vsColor"); // VS_Array.h line 260' <<< "$bounds"
    local format
    format="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/format.txt)"
    check "folds: text made with tinyformat is vsFormatString" grep -qF 'local_1e8 = vsFormatString("MessageTo Camera FocusOn %d", this->uid);' <<< "$format"

    local destructor globals
    destructor="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/destructor.txt "mmoCostume::~mmoCostume")"
    globals="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/globals.txt "_GLOBAL__sub_I_s_nameProperty")"
    check "folds: a destructor leaves out its members' own cleanup" bash -c '! grep -q "operator_delete\|~vsNullObject\|actorName" <<< "$1"' _ "$destructor"
    check "folds: a container deleting its items is delete, in a for loop" grep -qF 'for (index = 0; index < count; index++) {' <<< "$destructor"
    check "folds: an item deleted through its vtable is delete" grep -qF 'delete items2[index];' <<< "$destructor"
    check "folds: a property is the global it sets up" grep -qF 'mmoCostume::s_nameProperty = vsPropertyBase("name",false); // the field at +0x18' <<< "$globals"
    check "folds: the compiler's type info and atexit are left out" bash -c '! grep -q "s_RTTI\|atexit\|__static_init" <<< "$1"' _ "$globals"

    local constructor
    constructor="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/constructor.txt "mmoCostume::mmoCostume")"
    check "folds: a constructor leaves out the default constructors C++ calls by itself" bash -c '! grep -q "vsNullObject" <<< "$1"' _ "$constructor"
    check "folds: a base constructor given values stays" grep -qF 'mmoWindow::mmoWindow((mmoWindow *)this,"Costume");' <<< "$constructor"
    check "folds: a loop after other statements in its guard is a for loop" grep -qF 'for (index = 0; index < count; index++) {' <<< "$constructor"

    local copied
    copied="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/copied_text.txt)"
    check "folds: formatted text copied out by hand is vsFormatString" grep -qF 'text = vsFormatString("colorSlot[%d]", index);' <<< "$copied"
    check "folds: a counted do-while is a for loop" grep -qF 'for (index = 0; index < 8; index++) {' <<< "$copied"

    local zeros
    zeros="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/zeros.txt)"
    check "folds: bytes cleared one by one are memset" grep -qF 'memset(acStack_1d8 + 6, 0, 6);' <<< "$zeros"

    check "folds: the message's stream and strings are left out" bash -c '! grep -q "ostringstream\|local_288\|Demangle" <<< "$1"' _ "$bounds"

    local singleton
    singleton="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/singleton.txt)"

    check "folds: a singleton's lookup is Instance()" grep -qF '*(int *)(vsSingleton<mmoClock>::Instance() + 0x44);' <<< "$singleton"
    check "folds: the lookup and its assert are left out" bash -c '! grep -qE "VS_Singleton.h|LAB_140419931|pvVar1" <<< "$1"' _ "$singleton"

    local formatted
    formatted="$("${java_bin:+$java_bin/}java" -cp "$(cygpath -w "$out")" FoldCheck tests/fold/formatted.txt)"

    check "folds: a formatted message is vsAssertF" grep -qF 'vsAssertF(adjustment >= 0.f, "Setting a %f adjustment on a positive-type attribute effect??", local_res10);' <<< "$formatted"
    check "folds: the message's string is left out" bash -c '! grep -qE "tinyformat|operator_delete|local_38" <<< "$1"' _ "$formatted"
}


cd "$ROOT" || exit 1

if [ ! -f "$DIST/zlib1.dll" ] || [ ! -f build/libcjson_private.a ]; then
    echo "build the loader first (mingw32-make)"
    exit 1
fi

build_fixtures

test_normal_launch
test_without_loader_folder
test_disabled
test_core_missing
test_not_a_core
test_crash_streak
test_crash_without_core
test_other_exe
test_plugin_manual_install
test_plugin_other_build
test_plugin_no_mods
test_plugin_bad_manifests
test_plugin_two_copies
test_plugin_crash_streak
test_plugin_api
test_plugin_api_msvc
test_cpp_plugins
test_sdk_new_project
test_c_header_in_cpp
test_real_exe
test_sdk_research
test_ghidra_folds

echo "$passes passed, $failures failed"
[ "$failures" = 0 ]
