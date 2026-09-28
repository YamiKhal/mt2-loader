cmake_minimum_required(VERSION 3.21)

# The mod's id: its folder name in the game's mod folder, and the DLL's name. Also "id" in mod/manifest.json.
set(MOD_ID quest_markers)

project(${MOD_ID} CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Off: the mod's .win file holds only what it adds, which MT2 Mod Manager merges into the game's own. Copied as
# they are, they would replace the game's windows. Add mod/ in the manager instead.
option(COPY_TO_GAME "After each build, copy mod/ into the game's mod folder" OFF)
set(GAME_MOD_FOLDER "" CACHE PATH "The game's mod folder. Found by itself when empty")

# mt2loader.hpp: in this project's include folder, or in the loader's own source tree for its examples.
find_path(MT2LOADER_INCLUDE mt2loader.hpp
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include"
    NO_DEFAULT_PATH REQUIRED)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The game is 64-bit, so the plugin must be too. In Visual Studio pick an x64 configuration")
endif()

# Every .cpp file in src/ is part of the plugin: add files there and build again.
file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(${MOD_ID} SHARED ${PLUGIN_SOURCES})
target_include_directories(${MOD_ID} PRIVATE "${MT2LOADER_INCLUDE}")

# The DLL goes straight into the mod, as manifest.json lists it: mod/native/<id>.dll.
set_target_properties(${MOD_ID} PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/mod/native>"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# No runtime DLLs to ship: the plugin carries what it uses.
if(MSVC)
    target_compile_options(${MOD_ID} PRIVATE /utf-8 /permissive-)
elseif(MINGW)
    target_link_options(${MOD_ID} PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(COPY_TO_GAME AND NOT GAME_MOD_FOLDER)
    file(TO_CMAKE_PATH "$ENV{APPDATA}" APPDATA_FOLDER)
    file(GLOB PROFILE_FOLDERS LIST_DIRECTORIES true "${APPDATA_FOLDER}/VectorStorm/MMORPG Tycoon 2/*")
    list(FILTER PROFILE_FOLDERS INCLUDE REGEX "/[0-9]+$")
    list(LENGTH PROFILE_FOLDERS PROFILE_COUNT)

    if(PROFILE_COUNT EQUAL 1)
        set(GAME_MOD_FOLDER "${PROFILE_FOLDERS}/mod" CACHE PATH "The game's mod folder" FORCE)
    else()
        message(WARNING "The game's mod folder wasn't found (${PROFILE_COUNT} Steam profiles). Set GAME_MOD_FOLDER, or COPY_TO_GAME to OFF")
    endif()
endif()

if(COPY_TO_GAME AND GAME_MOD_FOLDER)
    message(STATUS "Each build copies mod/ to ${GAME_MOD_FOLDER}/${MOD_ID}")

    add_custom_command(TARGET ${MOD_ID} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/mod" "${GAME_MOD_FOLDER}/${MOD_ID}"
        COMMENT "Copying the mod into the game's mod folder (close the game first)")
endif()
