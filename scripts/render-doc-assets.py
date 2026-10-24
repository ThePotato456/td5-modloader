"""Build PNG/GIF documentation assets from the Custom Towers sample's real atlas.

Requires Pillow. Run with --check to compare without changing checked-in files.
No game launch, new artwork, or screenshot-style illustration is involved.
"""

import argparse
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
SAMPLE = ROOT / "samples/custom-tower-mod"
OUTPUT = SAMPLE / "assets/previews"


def rendered_assets():
    atlas = ET.parse(SAMPLE / "assets/textures/plasma_monkey.xml")
    cells = {cell.attrib["name"]: cell.attrib for cell in atlas.findall(".//Cell")}
    assets = {}
    with Image.open(SAMPLE / "assets/textures/plasma_monkey.png") as sheet:
        def crop(name):
            cell = cells[name]
            x, y, width, height = (int(cell[key]) for key in ("x", "y", "w", "h"))
            return sheet.crop((x, y, x + width, y + height)).convert("RGBA").resize(
                (width * 2, height * 2), Image.Resampling.NEAREST)

        for name, cell in [("plasma-monkey.png", "plasma_monkey_body"),
                           ("plasma-storm.png", "plasma_monkey_storm"),
                           ("plasma-cannon.png", "plasma_monkey_cannon")]:
            stream = BytesIO()
            crop(cell).save(stream, format="PNG", optimize=True)
            assets[OUTPUT / name] = stream.getvalue()

        frames = []
        for cell in ("plasma_monkey_body", "plasma_monkey_fire_1", "plasma_monkey_fire_2"):
            rgba = crop(cell)
            # Use palette indices 0..254 for color and reserve 255 for transparency.
            indexed = rgba.convert("RGB").quantize(colors=255)
            transparent = rgba.getchannel("A").point(lambda value: 255 if value < 128 else 0)
            indexed.paste(255, mask=transparent)
            indexed.info["transparency"] = 255
            frames.append(indexed)
        stream = BytesIO()
        frames[0].save(stream, format="GIF", save_all=True, append_images=frames[1:],
                       duration=[70, 60, 70], loop=0, transparency=255, disposal=2, optimize=False)
        assets[OUTPUT / "plasma-monkey-firing.gif"] = stream.getvalue()
    return assets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check existing assets without rewriting them")
    args = parser.parse_args()
    assets = rendered_assets()
    stale = []
    for path, contents in assets.items():
        if args.check:
            if not path.exists() or path.read_bytes() != contents:
                stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
    if stale:
        raise SystemExit("Regenerate documentation artwork with scripts/render-doc-assets.py:\n" + "\n".join(stale))
    print(f'{"Checked" if args.check else "Generated"} {len(assets)} PNG/GIF documentation assets.')


if __name__ == "__main__":
    main()
