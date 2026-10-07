"""Writes THIRD_PARTY.txt, shipped next to golf++.exe: the data credits and
library list (third_party_header.txt), then the licence of every library
the game ships or compiles in: the native ones from the toolchain's licence
folder (MSYS2 puts them in <prefix>/share/licenses/<package>), and every
Rust crate linked into the online client, from `cargo metadata`.

    python third_party.py --licenses <toolchain>/share/licenses --out <file>
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
RUST_TARGET = "x86_64-pc-windows-gnu"

# (shown name, licence folder in the toolchain) for every native library
# the game ships as a DLL or compiles in.
NATIVE_PACKAGES = [
    ("SDL2", "SDL2"),
    ("SDL2_mixer", "SDL2_mixer"),
    ("mpg123", "mpg123"),
    ("Opus", "opus"),
    ("opusfile", "opusfile"),
    ("Ogg", "libogg"),
    ("libmodplug", "libmodplug"),
    ("GCC runtime", "gcc-libs"),
    ("MinGW-w64 winpthreads", "libwinpthread"),
    ("GLM", "glm"),
]

LICENCE_FILE = re.compile(r"^(licen[cs]e|copying|notice|unlicense)", re.IGNORECASE)
RULE = "=" * 78


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace").strip()


def native_sections(licenses_dir):
    sections = []
    for name, folder in NATIVE_PACKAGES:
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


def linked_crates():
    """The packages the bridge links on Windows: its normal dependencies,
    transitively. Build and dev dependencies are not linked, nor are
    procedural macros (they run in the compiler), nor what only they use."""
    output = subprocess.run(
        ["cargo", "metadata", "--format-version", "1", "--locked", "--filter-platform", RUST_TARGET,
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


def rust_sections():
    listing = []
    # Many crates share a licence text word for word: each text once, with
    # the crates it covers.
    by_text = {}
    for crate in linked_crates():
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
    parser.add_argument("--licenses", required=True, type=pathlib.Path, help="the toolchain's share/licenses folder")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    parts = [read_text(RELEASE_DIR / "third_party_header.txt")]
    for title, text in native_sections(args.licenses) + rust_sections():
        parts.append(f"{RULE}\n{title}\n{RULE}\n\n{text}")
    args.out.write_text("\n\n\n".join(parts) + "\n", encoding="utf-8", newline="\r\n")


if __name__ == "__main__":
    main()
