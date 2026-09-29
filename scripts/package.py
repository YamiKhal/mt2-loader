import os
import re
import shutil
import sys
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
VERSION_HEADER = ROOT / "src" / "include" / "mt2loader_version.h"
INSTALL_TEMPLATE = ROOT / "package" / "INSTALL.txt"
SDK_README_TEMPLATE = ROOT / "package" / "SDK_README.txt"
MODDING_GUIDE = ROOT.parent / "LOADER_MODDING.md"
LICENSE = ROOT / "LICENSE"
EXAMPLES = ROOT / "examples"
LICENSE_FOLDER = Path("share") / "licenses"
MSYS_LICENSES = {"zlib": "zlib", "cJSON": "cjson"}
LOCAL_LICENSES = {
    "MinHook": ROOT / "third_party" / "minhook" / "LICENSE.txt",
    "libiberty C++ demangler (cp-demangle.c)": ROOT / "third_party" / "libiberty" / "cp-demangle-LICENSE.txt",
    "Zydis": ROOT / "third_party" / "zydis" / "LICENSE.txt",
    "Zycore": ROOT / "third_party" / "zydis" / "Zycore-LICENSE.txt",
}
LOADER_THIRD_PARTY = ["zlib", "cJSON", "MinHook", "libiberty C++ demangler (cp-demangle.c)"]
SDK_THIRD_PARTY = ["cJSON", "libiberty C++ demangler (cp-demangle.c)", "Zydis", "Zycore"]
SKIPPED_IN_EXAMPLES = {"build", "out", ".vs"}


def loader_version() -> str:
    match = re.search(r'#define MT2LOADER_VERSION "([^"]+)"', VERSION_HEADER.read_text())

    if match is None:
        sys.exit(f"no version in {VERSION_HEADER}")

    return match.group(1)


def msys_prefix() -> Path:
    gcc = shutil.which("gcc")

    if gcc is None:
        sys.exit("gcc isn't on PATH: run this from the MSYS2 MINGW64 shell, or put C:\\msys64\\mingw64\\bin first on PATH")

    # gcc is in <prefix>/bin, and the licenses of MSYS2's libraries are in <prefix>/share/licenses.
    return Path(gcc).resolve().parent.parent


def license_of(name: str, prefix: Path) -> str:
    if name in MSYS_LICENSES:
        return (prefix / LICENSE_FOLDER / MSYS_LICENSES[name] / "LICENSE").read_text(encoding="utf-8")

    return LOCAL_LICENSES[name].read_text(encoding="utf-8-sig")


def third_party_text(names: list[str], prefix: Path) -> str:
    sections = []

    for name in names:
        sections.append(f"{name}\n{'=' * len(name)}\n\n{license_of(name, prefix).strip()}\n")

    return "\n\n".join(sections)


def windows_text(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\n", "\r\n")


def require(paths: list[Path]) -> None:
    for path in paths:
        if not path.exists():
            sys.exit(f"{path} is missing: build the loader first (mingw32-make)")


def write_loader_zip(version: str, prefix: Path) -> Path:
    package = DIST / f"MT2Loader-{version}.zip"
    files = {
        "zlib1.dll": DIST / "zlib1.dll",
        "mt2loader/mt2loader.dll": DIST / "mt2loader" / "mt2loader.dll",
    }

    require(list(files.values()))

    with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, path in files.items():
            archive.write(path, name)

        archive.writestr("LICENSE.txt", windows_text(LICENSE.read_text(encoding="utf-8")))
        archive.writestr("INSTALL.txt", windows_text(INSTALL_TEMPLATE.read_text(encoding="utf-8").replace("{version}", version)))
        archive.writestr("THIRD_PARTY.txt", windows_text(third_party_text(LOADER_THIRD_PARTY, prefix)))

    return package


def add_folder(archive: zipfile.ZipFile, folder: Path, name: str) -> None:
    for path in sorted(folder.rglob("*")):
        # os.path.relpath: MSYS2's Python mixes / and \ in these paths, which Path.relative_to doesn't accept.
        rel = PurePosixPath(os.path.relpath(path, folder).replace("\\", "/"))

        if path.is_file() and not SKIPPED_IN_EXAMPLES.intersection(rel.parts):
            archive.write(path, f"{name}/{rel}")


# Every project in the SDK builds on its own: each gets its own copy of the headers.
HEADERS = ["mt2loader.hpp", "mt2loader.h"]


def write_sdk_zip(version: str, prefix: Path) -> Path:
    sdk = DIST / "sdk"
    headers = [sdk / "include" / name for name in HEADERS]
    root = f"MT2LoaderSDK-{version}"
    package = DIST / f"{root}.zip"

    require([sdk / "mt2sdk.exe", *headers, sdk / "template", sdk / "ghidra", MODDING_GUIDE])

    with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(sdk / "mt2sdk.exe", f"{root}/mt2sdk.exe")
        add_folder(archive, sdk / "template", f"{root}/template")
        add_folder(archive, sdk / "ghidra", f"{root}/ghidra")

        for header in headers:
            archive.write(header, f"{root}/include/{header.name}")

        for example in sorted(path for path in EXAMPLES.iterdir() if path.is_dir()):
            add_folder(archive, example, f"{root}/examples/{example.name}")

            for header in headers:
                archive.write(header, f"{root}/examples/{example.name}/include/{header.name}")

        archive.write(MODDING_GUIDE, f"{root}/LOADER_MODDING.md")
        archive.writestr(f"{root}/LICENSE.txt", windows_text(LICENSE.read_text(encoding="utf-8")))
        archive.writestr(f"{root}/README.txt", windows_text(SDK_README_TEMPLATE.read_text(encoding="utf-8").replace("{version}", version)))
        archive.writestr(f"{root}/THIRD_PARTY.txt", windows_text(third_party_text(SDK_THIRD_PARTY, prefix)))

    return package


def main() -> None:
    version = loader_version()
    prefix = msys_prefix()

    print(write_loader_zip(version, prefix))
    print(write_sdk_zip(version, prefix))


if __name__ == "__main__":
    main()
