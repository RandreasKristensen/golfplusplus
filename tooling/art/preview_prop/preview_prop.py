"""Photograph a JSON prop with the native game renderer; Pillow only converts
the captured BMPs to PNG and arranges the four photographs for inspection.

cmake --build build/test --target golfpp-preview-prop
python tooling/art/preview_prop/preview_prop.py hub_bus --out build/prop_preview/bus [--label TEXT]
The three 12 m views and 25 m view all retain the full camcorder/tape/TV path.
rear.png and board.png (3 m from its board, with the board text) are extra.
"""
import argparse
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
NAMES = ("side", "front", "three_quarter", "tape_25m", "rear", "board")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prop", help="Prop name or JSON path")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/test/golfpp-preview-prop.exe")
    parser.add_argument("--label", help="board text (default: the book's title, the longest course name)")
    args = parser.parse_args()
    prop = Path(args.prop)
    if not prop.is_file():
        prop = ROOT / "assets/world/props" / (args.prop + ".json")
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(args.exe), str(prop.resolve()), str(output)] + ([args.label] if args.label else []),
                   cwd=ROOT, check=True)
    sheet = Image.new("RGB", (1600, 1240), (30, 30, 30))
    labels = ImageDraw.Draw(sheet)
    for i, name in enumerate(NAMES):
        with Image.open(output / (name + ".bmp")) as capture:
            capture.convert("RGB").save(output / (name + ".png"))
            if i >= 4:
                continue
            sheet.paste(capture.resize((800, 600)), ((i % 2)*800, (i // 2)*620+20))
        labels.text(((i % 2)*800+12, (i // 2)*620+4), name, fill="white")
    sheet.save(output / "views.png")
    print(output / "views.png")


if __name__ == "__main__":
    main()
