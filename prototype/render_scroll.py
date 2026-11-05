#!/usr/bin/env python3
"""스크롤 시뮬레이션 결과를 사람이 볼 수 있게 그린다.

nota-scroll 이 낸 추적 기록과 페이지 이미지를 받아, 연주 중 화면에 실제로
무엇이 보이는지 여러 시점의 필름스트립으로 만든다. 숫자만으로는 "읽기 선이
제자리에 있는가"를 확인할 수 없다.

사용법:
    ./build/tools/nota-scroll p1.pgm p2.pgm --csv trace.csv
    python3 prototype/render_scroll.py trace.csv p1.pgm p2.pgm -o filmstrip.png
"""
import argparse
import csv
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def stitch(paths, gap=0):
    imgs = [Image.open(p).convert("L") for p in paths]
    w = max(i.width for i in imgs)
    h = sum(i.height for i in imgs) + gap * (len(imgs) - 1)
    canvas = Image.new("L", (w, h), 255)
    y = 0
    for im in imgs:
        canvas.paste(im, (0, y))
        y += im.height + gap
    return canvas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("pages", nargs="+")
    ap.add_argument("-o", "--out", default="filmstrip.png")
    ap.add_argument("--viewport", type=float, default=900.0)
    ap.add_argument("--read-line", type=float, default=1.0 / 3.0)
    ap.add_argument("--frames", type=int, default=5)
    ap.add_argument("--scale", type=float, default=0.42)
    args = ap.parse_args()

    rows = list(csv.DictReader(open(args.trace)))
    if not rows:
        print("추적 기록이 비었다", file=sys.stderr)
        return 1

    canvas = stitch(args.pages)
    picks = [rows[int(i * (len(rows) - 1) / (args.frames - 1))]
             for i in range(args.frames)]

    vh = int(args.viewport)
    shots = []
    for r in picks:
        off = int(float(r["offset"]))
        view = Image.new("RGB", (canvas.width, vh), (255, 255, 255))
        src_top = max(0, off)
        src_bot = min(canvas.height, off + vh)
        if src_bot > src_top:
            crop = canvas.crop((0, src_top, canvas.width, src_bot)).convert("RGB")
            view.paste(crop, (0, src_top - off))
        d = ImageDraw.Draw(view)
        # 읽기 선: 지금 연주 중인 위치
        y = int(vh * args.read_line)
        d.line([(0, y), (canvas.width, y)], fill=(230, 40, 40), width=5)
        d.rectangle([0, 0, canvas.width - 1, vh - 1], outline=(150, 150, 150), width=3)
        d.text((14, 12), "t=%.0fs  beat %.1f  sys %s%s"
               % (float(r["time_s"]), float(r["beat"]), r["system"],
                  "" if r["playing"] == "1" else "  [정지]"),
               fill=(20, 20, 20))
        shots.append(view)

    sw = int(shots[0].width * args.scale)
    sh = int(shots[0].height * args.scale)
    pad = 18
    strip = Image.new("RGB", (len(shots) * sw + pad * (len(shots) + 1),
                              sh + 2 * pad), (245, 245, 245))
    for i, s in enumerate(shots):
        strip.paste(s.resize((sw, sh), Image.LANCZOS), (pad + i * (sw + pad), pad))
    strip.save(args.out)
    print("저장: %s (%dx%d)" % (args.out, strip.width, strip.height))
    return 0


if __name__ == "__main__":
    sys.exit(main())
