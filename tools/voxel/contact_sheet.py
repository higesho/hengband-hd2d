#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""撮った BMP を**並べて 1 枚**にする（見比べるための道具。P7）。

絵の可否は 1 枚では決まらない。「ブルームを入れた絵」を単独で見ても良いか悪いか
言えないが、**切った絵と並べれば 3 秒で言える**。P6 の GIF（`--motion-frames`）と
同じ理由の道具である。

使い方::

    python tools/voxel/contact_sheet.py shots/p7/dungeon_*.bmp -o shots/p7/effects.png
    python tools/voxel/contact_sheet.py a.bmp b.bmp -o out.png --cols 2 --crop 700x520+450+190
"""

from __future__ import annotations

import argparse
import pathlib
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover
    print("Pillow が要ります: python -m pip install pillow", file=sys.stderr)
    raise


def parse_crop(spec: str) -> tuple[int, int, int, int] | None:
    """``WxH+X+Y`` を ``(left, top, right, bottom)`` へ。"""
    if not spec:
        return None
    size, _, offset = spec.partition("+")
    w, _, h = size.partition("x")
    x, _, y = offset.partition("+")
    left = int(x)
    top = int(y)
    return (left, top, left + int(w), top + int(h))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("--cols", type=int, default=0, help="0 なら枚数から決める")
    parser.add_argument("--scale", type=float, default=0.5)
    parser.add_argument("--crop", default="", help="WxH+X+Y（切り出してから並べる）")
    parser.add_argument("--label-height", type=int, default=22)
    args = parser.parse_args()

    paths = [pathlib.Path(p) for p in args.inputs]
    images = []
    box = parse_crop(args.crop)
    for path in paths:
        image = Image.open(path).convert("RGB")
        if box is not None:
            image = image.crop(box)
        if args.scale != 1.0:
            image = image.resize((max(1, int(image.width * args.scale)),
                                  max(1, int(image.height * args.scale))), Image.LANCZOS)
        images.append((path.stem, image))

    cols = args.cols if args.cols > 0 else min(4, len(images))
    rows = (len(images) + cols - 1) // cols
    tile_w = max(i.width for _, i in images)
    tile_h = max(i.height for _, i in images) + args.label_height

    sheet = Image.new("RGB", (cols * tile_w, rows * tile_h), (18, 18, 22))
    draw = ImageDraw.Draw(sheet)
    for index, (name, image) in enumerate(images):
        cx = (index % cols) * tile_w
        cy = (index // cols) * tile_h
        draw.text((cx + 6, cy + 5), name, fill=(235, 225, 180))
        sheet.paste(image, (cx, cy + args.label_height))

    pathlib.Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)
    print(f"{len(images)} 枚を並べました: {args.output} ({sheet.width}x{sheet.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
