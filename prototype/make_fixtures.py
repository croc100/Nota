#!/usr/bin/env python3
"""V1/V2 검증용 악보를 재현 가능하게 생성한다.

music21 코퍼스의 실제 피아노 작품을 Verovio로 조판해 이미지와 정답을 만든다.
Verovio의 SVG에는 오선·마디선 요소가 들어 있으므로, 수작업 라벨링 없이
정답을 얻을 수 있다. 이것이 스캔 악보 대신 조판 엔진을 쓰는 이유다.

한계: 여기서 나오는 것은 모두 "디지털 조판" 악보다. 스캔본·사진 촬영본의
기울기, 음영, 얼룩, 번짐은 재현되지 않는다. 그 범주는 실제 스캔 표본으로
따로 검증해야 한다 (prototype/fixtures/README.md).

사용법:
    pip install verovio music21 cairosvg
    python3 prototype/make_fixtures.py --out <디렉토리>
"""
import argparse
import pathlib
import subprocess
import sys

PIECES = [
    ("bach_bwv846", "bach/bwv846.mxl", "평균율 1권 전주곡 1번 — 균일한 분산화음"),
    ("chopin_mazurka06_2", "chopin/mazurka06-2.krn", "마주르카 Op.6-2 — 양손 화음, 겹세로줄"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="prototype/out/scores")
    ap.add_argument("--pages", type=int, default=1)
    ap.add_argument("--width", type=int, default=1654)   # A4 @ 200 DPI
    ap.add_argument("--height", type=int, default=2339)
    args = ap.parse_args()

    try:
        import cairosvg
        import verovio
        from music21 import corpus
    except ImportError as e:
        print("의존성 없음: %s" % e, file=sys.stderr)
        print("  pip install verovio music21 cairosvg", file=sys.stderr)
        return 2

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    tk = verovio.toolkit()
    tk.setOptions({
        "pageWidth": 2100, "pageHeight": 2970,   # A4, 1/10 mm
        "scale": 40, "adjustPageHeight": False,
        "footer": "none", "header": "none",
    })

    made = 0
    for name, path, note in PIECES:
        try:
            score = corpus.parse(path)
            xml = out / ("%s.musicxml" % name)
            score.write("musicxml", fp=str(xml))
            if not tk.loadFile(str(xml)):
                print("  %s: verovio 로드 실패" % name, file=sys.stderr)
                continue
            for page in range(1, min(args.pages, tk.getPageCount()) + 1):
                stem = out / ("%s_p%d" % (name, page))
                svg = stem.with_suffix(".svg")
                png = stem.with_suffix(".png")
                pgm = stem.with_suffix(".pgm")
                svg.write_text(tk.renderToSVG(page))
                cairosvg.svg2png(url=str(svg), write_to=str(png),
                                 output_width=args.width, output_height=args.height,
                                 background_color="white")
                subprocess.run(["convert", str(png), "-colorspace", "Gray", str(pgm)],
                               check=True)
                made += 1
                print("  %-22s p%d  %s" % (name, page, note))
        except Exception as e:                      # noqa: BLE001
            print("  %s 실패: %s" % (name, e), file=sys.stderr)

    print("\n%d개 페이지 생성: %s" % (made, out))
    print("평가:  python3 prototype/evaluate_page.py %s/<name>.svg %s/<name>.pgm" % (out, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
