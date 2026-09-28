"""Compiles the mod's vertex shaders with the game's fragment shader, as the engine would, for every variant.

    python check_shaders.py <GameData folder>

Needs glslangValidator (MSYS2: pacman -S mingw-w64-x86_64-glslang). The engine puts its defines (LIT, TEXTURE and
the variants a fragment shader lists) after the #version line and fills #include lines from the shaders folder.
"""
import itertools
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SHADERS = HERE.parent / "mod" / "shaders"
PAIRS = [("questmarker_v.glsl", "tint_f.glsl", False), ("questmarker_sprite_v.glsl", "tint_f.glsl", True)]


def expanded(path: Path, game_shaders: Path) -> str:
    text = path.read_text(encoding="utf-8")

    def include(match):
        return expanded(game_shaders / match.group(1), game_shaders)

    return re.sub(r'^#include "([^"]+)"', include, text, flags=re.MULTILINE)


def with_defines(text: str, defines) -> str:
    lines = text.splitlines()
    added = [f"#define {name}" for name in defines]

    return "\n".join(lines[:1] + added + lines[1:]) + "\n"


def variants_of(fragment_text: str):
    return re.findall(r"^// variant (\w+)", fragment_text, flags=re.MULTILINE)


def main():
    game_shaders = Path(sys.argv[1]) / "shaders"
    validator = shutil.which("glslangValidator")

    if validator is None:
        sys.exit("glslangValidator not found")

    failures = 0

    for vertex_name, fragment_name, textured in PAIRS:
        vertex = expanded(SHADERS / vertex_name, game_shaders)
        fragment = expanded(game_shaders / fragment_name, game_shaders)
        options = ["LIT"] + variants_of(fragment)

        for count in range(len(options) + 1):
            for chosen in itertools.combinations(options, count):
                defines = list(chosen) + (["TEXTURE"] if textured else [])

                with tempfile.TemporaryDirectory() as folder:
                    vertex_file = Path(folder) / "marker.vert"
                    fragment_file = Path(folder) / "marker.frag"
                    vertex_file.write_text(with_defines(vertex, defines), encoding="utf-8")
                    fragment_file.write_text(with_defines(fragment, defines), encoding="utf-8")
                    result = subprocess.run([validator, "-l", str(vertex_file), str(fragment_file)], capture_output=True, text=True)

                if result.returncode != 0:
                    failures += 1
                    print(f"{vertex_name} + {fragment_name} with {defines}:\n{result.stdout}{result.stderr}")

        print(f"{vertex_name} + {fragment_name}: checked {2 ** len(options)} variants")

    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
