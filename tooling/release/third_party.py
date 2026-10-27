"""Writes THIRD_PARTY.txt, shipped next to the game: the data credits
(third_party_header.txt), the list of libraries, then the licence of every
library the game ships or compiles in on that platform: the native ones from
a licence folder laid out as <licenses>/<package> (MSYS2's share/licenses on
Windows, build_deps.sh's on macOS and Linux), and every Rust crate linked
into the online client, from `cargo metadata`.

    python third_party.py --platform windows|macos|linux --licenses <dir> --out <file>
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

RELEASE_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = RELEASE_DIR.parent.parent
BRIDGE_MANIFEST = REPO_ROOT / "net" / "client_bridge" / "Cargo.toml"
# The Rust target whose linked crates each platform ships; both macOS
# architectures link the same crates.
RUST_TARGETS = {
    "windows": "x86_64-pc-windows-gnu",
    "macos": "aarch64-apple-darwin",
    "linux": "x86_64-unknown-linux-gnu",
}

ALL = ("windows", "macos", "linux")
# (shown name, licence folder, licence in short, platforms) for every native
# library the game ships as a DLL or compiles in.
NATIVE_PACKAGES = [
    ("SDL2", "SDL2", "zlib licence", ALL),
    ("SDL2_mixer", "SDL2_mixer", "zlib licence; on macOS and Linux with minimp3 (CC0) and stb_vorbis (MIT or public domain)", ALL),
    ("mpg123 (libmpg123)", "mpg123", "LGPL 2.1; source at https://www.mpg123.de", ("windows",)),
    ("Opus", "opus", "BSD 3-clause", ("windows",)),
    ("opusfile", "opusfile", "BSD 3-clause", ("windows",)),
    ("Ogg", "libogg", "BSD 3-clause", ("windows",)),
    ("libmodplug", "libmodplug", "public domain", ("windows",)),
    ("GCC runtime (libgcc, libstdc++)", "gcc-libs", "GPL 3 with the GCC Runtime Library Exception", ("windows",)),
    ("MinGW-w64 winpthreads", "libwinpthread", "MIT and others (see its licence)", ("windows",)),
    ("GLM", "glm", "MIT (or the Happy Bunny licence)", ALL),
]
# Listed after the native libraries on every platform.
COMMON_LISTING = """\
  JSON for Modern C++: MIT, (C) 2013-2023 Niels Lohmann
  The Rust crates of the online client, listed at the end with their
  licences.
  SpacetimeDB C++ bindings (the server module): Business Source License 1.1,
  compiled into the server; its text is the SpacetimeDB licence with the
  Rust crates below."""
WINDOWS_NOTE = """\
The DLLs next to golf++.exe are the unchanged libraries; replacing one with
another build of the same library works as long as it is compatible."""

LICENCE_FILE = re.compile(r"^(licen[cs]e|copying|notice|unlicense)", re.IGNORECASE)
RULE = "=" * 78


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace").strip()


def native_packages(platform):
    return [package for package in NATIVE_PACKAGES if platform in package[3]]


def library_listing(platform):
    lines = [f"  {name}: {summary}" for name, _, summary, _ in native_packages(platform)]
    listing = "\n".join(lines) + "\n" + COMMON_LISTING
    return listing + "\n\n" + WINDOWS_NOTE if platform == "windows" else listing


def native_sections(platform, licenses_dir):
    sections = []
    for name, folder, _, _ in native_packages(platform):
        directory = licenses_dir / folder
        files = sorted(p for p in directory.glob("*") if p.is_file()) if directory.is_dir() else []
        if not files:
            sys.exit(f"no licence for {name} in {directory}")
        for path in files:
            sections.append((f"{name} ({path.name})", read_text(path)))
    sections.append(("JSON for Modern C++", read_text(RELEASE_DIR / "licenses" / "nlohmann_json.txt")))
    return sections


def is_proc_macro(package):
    return any("proc-macro" in target["kind"] for target in package["targets"])


def linked_crates(rust_target):
    """The packages the bridge links for <rust_target>: its normal dependencies,
    transitively. Build and dev dependencies are not linked, nor are
    procedural macros (they run in the compiler), nor what only they use."""
    output = subprocess.run(
        ["cargo", "metadata", "--format-version", "1", "--locked", "--filter-platform", rust_target,
         "--manifest-path", str(BRIDGE_MANIFEST)],
        check=True, capture_output=True, text=True, encoding="utf-8").stdout
    metadata = json.loads(output)
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    root = metadata["resolve"]["root"]
    seen, stack = set(), [root]
    while stack:
        current = stack.pop()
        for dep in nodes[current]["deps"]:
            linked = any(kind["kind"] is None for kind in dep["dep_kinds"]) and not is_proc_macro(packages[dep["pkg"]])
            if linked and dep["pkg"] not in seen:
                seen.add(dep["pkg"])
                stack.append(dep["pkg"])
    return sorted((packages[i] for i in seen), key=lambda p: (p["name"], p["version"]))


def crate_licence_texts(package):
    directory = pathlib.Path(package["manifest_path"]).parent
    paths = {p for p in directory.iterdir() if p.is_file() and LICENCE_FILE.match(p.name)}
    if package.get("license_file"):
        paths.add(directory / package["license_file"])
    return [read_text(p) for p in sorted(paths) if p.is_file()]


def rust_sections(rust_target):
    listing = []
    # Many crates share a licence text word for word: each text once, with
    # the crates it covers.
    by_text = {}
    for crate in linked_crates(rust_target):
        texts = crate_licence_texts(crate)
        licence = crate.get("license") or "see its licence below"
        listing.append(f"  {crate['name']} {crate['version']}: {licence}" + ("" if texts else " (the crate has no licence text)"))
        for text in texts:
            by_text.setdefault(text, []).append(f"{crate['name']} {crate['version']}")
    sections = [("Rust crates of the online client", "\n".join(listing))]
    for text, names in by_text.items():
        sections.append((", ".join(names), text))
    return sections


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", required=True, choices=sorted(RUST_TARGETS))
    parser.add_argument("--licenses", required=True, type=pathlib.Path, help="the native libraries' licence folder")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    parts = [read_text(RELEASE_DIR / "third_party_header.txt") + "\n\n" + library_listing(args.platform)]
    for title, text in native_sections(args.platform, args.licenses) + rust_sections(RUST_TARGETS[args.platform]):
        parts.append(f"{RULE}\n{title}\n{RULE}\n\n{text}")
    newline = "\r\n" if args.platform == "windows" else "\n"
    args.out.write_text("\n\n\n".join(parts) + "\n", encoding="utf-8", newline=newline)


if __name__ == "__main__":
    main()
