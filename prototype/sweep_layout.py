#!/usr/bin/env python3
"""레이아웃 변이에 대한 내성을 측정한다.

열화(스캔 잡음)와는 다른 축이다. 같은 악보라도 해상도, 조판 크기, 페이지 규격,
여백, 페이지 안에서의 위치가 모두 다르다. 사용자가 가져오는 PDF는 이 모든 조합
으로 온다.

코어에 절대 픽셀값으로 박힌 상수가 있으면 여기서 드러난다. 오선 간격 대비
비율로 쓴 값은 견뎌야 한다.

위치를 옮기면 정답 좌표도 함께 옮겨야 하므로, 적용한 이동량을 채점기에 넘긴다.

사용법:
    python3 prototype/sweep_layout.py
"""
import json
import pathlib
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from evaluate_page import match_1d, parse_ground_truth, prf   # noqa: E402

ANALYZE = "./build/tools/nota-analyze"
A4 = (2100, 2970)
LETTER = (2159, 2794)
B4 = (2500, 3530)
SQUARE = (2400, 2400)
LANDSCAPE = (2970, 2100)

# A4 scale 40 을 폭 1654 px로 렌더하면 오선 간격이 약 14.2 px이다.
REF_WIDTH, REF_SPACE = 1654, 14.2


class Renderer:
    def __init__(self, tmp):
        import cairosvg
        import verovio
        from music21 import corpus
        self.cairosvg = cairosvg
        self.tk = verovio.toolkit()
        self.tmp = pathlib.Path(tmp)
        self.xml = {}
        for name, path in [("bach", "bach/bwv846.mxl"),
                           ("chopin", "chopin/mazurka06-2.krn")]:
            x = self.tmp / ("%s.musicxml" % name)
            corpus.parse(path).write("musicxml", fp=str(x))
            self.xml[name] = x

    def render(self, name, stem, page=A4, scale=40, margin=None,
               px_width=REF_WIDTH, stretch=(1.0, 1.0)):
        opts = {"pageWidth": page[0], "pageHeight": page[1], "scale": scale,
                "adjustPageHeight": False, "footer": "none", "header": "none"}
        if margin is not None:
            opts.update({"pageMarginLeft": margin, "pageMarginRight": margin,
                         "pageMarginTop": margin, "pageMarginBottom": margin})
        self.tk.setOptions(opts)
        if not self.tk.loadFile(str(self.xml[name])):
            return None
        svg = self.tmp / ("%s.svg" % stem)
        png = self.tmp / ("%s.png" % stem)
        svg.write_text(self.tk.renderToSVG(1))
        h = int(round(px_width * page[1] / page[0]))
        self.cairosvg.svg2png(url=str(svg), write_to=str(png),
                              output_width=int(px_width * stretch[0]),
                              output_height=int(h * stretch[1]),
                              background_color="white")
        return svg, png


def stretch_to_pgm(png, pgm, sx, sy):
    """가로/세로를 서로 다른 비율로 늘린다.

    cairosvg에 output_width/height를 어긋나게 주면 종횡비를 보존해버려 실제로
    늘어나지 않는다(검출된 오선 간격이 그대로인 것이 증거다). 래스터를 만든
    뒤 PIL로 직접 늘려야 한다.
    """
    img = Image.open(png).convert("L")
    w, h = img.size
    nw, nh = int(round(w * sx)), int(round(h * sy))
    img.resize((nw, nh), Image.BICUBIC).save(pgm)
    return (nw, nh)


def to_pgm(png, pgm, dx=0, dy=0, pad=0):
    """위치를 옮기거나 가장자리를 잘라 PGM으로 만든다.

    dx, dy > 0 이면 내용을 오른쪽/아래로 민다 (여백 추가).
    dx, dy < 0 이면 그만큼 잘라낸다.
    pad 는 사방에 흰 여백을 더한다.
    """
    from PIL import Image
    img = Image.open(png).convert("L")
    w, h = img.size
    canvas = Image.new("L", (w + 2 * pad, h + 2 * pad), 255)
    canvas.paste(img, (pad + dx, pad + dy))
    canvas.save(pgm)
    return pad + dx, pad + dy


def score(svg, pgm, base_size, off=(0, 0)):
    """채점한다.

    base_size 는 변형 전 렌더 크기다. 정답 좌표는 그 크기를 기준으로 환산한
    뒤 이동량을 더해야 한다. 패딩된 이미지 크기로 환산하면 축척이 틀어진다
    (이 실수로 위치 축의 F가 전부 0 근처로 나왔던 적이 있다).

    가로/세로로 늘린 경우도 base_size 에 늘린 크기를 넣으면 정답이 같은 비율로
    늘어나므로 그대로 비교할 수 있다.
    """
    out = subprocess.run([ANALYZE, str(pgm)], capture_output=True, text=True)
    if out.returncode != 0:
        return None
    det = json.loads(out.stdout)

    gt = parse_ground_truth(pathlib.Path(svg).read_text(), base_size[0], base_size[1])
    space = det["median_staff_space"] or 10.0
    tol = max(4.0, space * 0.8)
    systems = det.get("systems", [])
    gt_by, det_by = {}, {}
    for x, y in gt["barlines"]:
        x += off[0]
        y += off[1]
        for i, s in enumerate(systems):
            if s["y_top"] - space * 2 <= y <= s["y_bottom"] + space * 2:
                gt_by.setdefault(i, []).append(x)
                break
    for m in det.get("measures", []):
        det_by.setdefault(m["system"], []).append(m["x"][1])
    tp = fp = fn = 0
    for i in range(len(systems)):
        a, b, c, _ = match_1d(gt_by.get(i, []), det_by.get(i, []), tol)
        tp += a; fp += b; fn += c
    _, _, f = prf(tp, fp, fn)
    return dict(f=f, space=space, systems=det["system_count"],
                gt_systems=gt["system_count"], staves=det["staff_count"],
                gt_staves=len(gt["staff_line_y"]) // 5,
                consistency=det["quality"]["consistency"])


def cell(r):
    if r is None:
        return "  실패       "
    ok = (r["systems"] == r["gt_systems"] and r["staves"] == r["gt_staves"])
    return "%5.1f %5.3f%s" % (r["space"], r["f"], " " if ok else "!")


def main():
    try:
        import cairosvg  # noqa: F401
        import verovio   # noqa: F401
        from music21 import corpus  # noqa: F401
    except ImportError as e:
        print("의존성 없음: %s" % e, file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as td:
        R = Renderer(td)
        tmp = pathlib.Path(td)
        print("표기: 검출 오선간격 / 마디 F   (! = 구조 불일치)\n")

        # --- 1. 악보 크기 (오선 간격) -----------------------------------------
        print("악보 크기 — 목표 오선간격 px (해상도·조판크기를 아우르는 실질 축)")
        targets = [8, 10, 14, 21, 28, 36]
        print("  %-8s %s" % ("", "  ".join("%-12s" % ("%dpx" % t) for t in targets)))
        for name in ("bach", "chopin"):
            cells = []
            for t in targets:
                px = int(REF_WIDTH * t / REF_SPACE)
                r = R.render(name, "%s_sz%d" % (name, t), px_width=px)
                if r is None:
                    cells.append("%-12s" % "렌더실패"); continue
                svg, png = r
                pgm = tmp / ("%s_sz%d.pgm" % (name, t))
                base = Image.open(png).size
                to_pgm(png, pgm)
                cells.append("%-12s" % cell(score(svg, pgm, base)))
            print("  %-8s %s" % (name, "  ".join(cells)))

        # --- 2. 페이지 안에서의 위치 (내용 손실 없음) -------------------------
        print("\n페이지 안 위치 — 캔버스를 넓혀 내용 손실 없이 이동")
        shifts = [("원본", 0, 0), ("우하 +80", 80, 80), ("좌상 -80", -80, -80),
                  ("우측 +200", 200, 0), ("하단 +200", 0, 200)]
        print("  %-8s %s" % ("", "  ".join("%-14s" % s[0] for s in shifts)))
        for name in ("bach", "chopin"):
            r0 = R.render(name, "%s_pos" % name)
            base = Image.open(r0[1]).size
            cells = []
            for label, dx, dy in shifts:
                pad = max(abs(dx), abs(dy))   # 잘리지 않도록 캔버스를 넓힌다
                pgm = tmp / ("%s_pos_%d_%d.pgm" % (name, dx, dy))
                off = to_pgm(r0[1], pgm, dx, dy, pad)
                cells.append("%-14s" % cell(score(r0[0], pgm, base, off)))
            print("  %-8s %s" % (name, "  ".join(cells)))

        # --- 2b. 가장자리 잘림 (내용 손실) ------------------------------------
        # 정답에는 잘려나간 마디선이 남으므로 F를 그대로 비교할 수 없다.
        # 구조(시스템·오선)가 유지되는지만 본다.
        print("\n가장자리 잘림 — 내용이 실제로 사라진다 (구조만 확인)")
        crops = [("없음", 0, 0, 0, 0), ("좌 60", 60, 0, 0, 0),
                 ("우 60", 0, 60, 0, 0), ("상 60", 0, 0, 60, 0),
                 ("사방 60", 60, 60, 60, 60)]
        print("  %-8s %s" % ("", "  ".join("%-16s" % c[0] for c in crops)))
        for name in ("bach", "chopin"):
            r0 = R.render(name, "%s_crop" % name)
            img0 = Image.open(r0[1]).convert("L")
            w0, h0 = img0.size
            cells = []
            for label, l, rgt, t, b in crops:
                pgm = tmp / ("%s_crop_%s.pgm" % (name, label.replace(" ", "")))
                img0.crop((l, t, w0 - rgt, h0 - b)).save(pgm)
                res = score(r0[0], pgm, (w0, h0), (-l, -t))
                if res is None:
                    cells.append("%-16s" % "  실패")
                else:
                    ok = (res["systems"] == res["gt_systems"] and
                          res["staves"] == res["gt_staves"])
                    cells.append("%-16s" % ("시스템%d/%d 오선%d/%d %s" % (
                        res["systems"], res["gt_systems"],
                        res["staves"], res["gt_staves"], "" if ok else "!")))
            print("  %-8s %s" % (name, "  ".join(cells)))

        # --- 3. 페이지 규격 (픽셀 폭 고정 → 오선 크기가 실제로 달라진다) -------
        print("\n페이지 규격 — 폭 1654px 고정 (큰 판형일수록 오선이 작아진다)")
        sizes = [("A4", A4), ("Letter", LETTER), ("B4", B4),
                 ("정사각", SQUARE), ("가로", LANDSCAPE)]
        print("  %-8s %s" % ("", "  ".join("%-12s" % n for n, _ in sizes)))
        for name in ("bach", "chopin"):
            cells = []
            for label, pg in sizes:
                r = R.render(name, "%s_%s" % (name, label), page=pg)
                if r is None:
                    cells.append("%-12s" % "렌더실패"); continue
                pgm = tmp / ("%s_%s.pgm" % (name, label))
                base = Image.open(r[1]).size
                to_pgm(r[1], pgm)
                cells.append("%-12s" % cell(score(r[0], pgm, base)))
            print("  %-8s %s" % (name, "  ".join(cells)))

        # --- 4. 여백 ----------------------------------------------------------
        print("\n여백 (A4, 폭 1654px)")
        margins = [("좁음 50", 50), ("기본 100", 100), ("넓음 250", 250),
                   ("매우넓음 400", 400)]
        print("  %-8s %s" % ("", "  ".join("%-14s" % n for n, _ in margins)))
        for name in ("bach", "chopin"):
            cells = []
            for label, m in margins:
                r = R.render(name, "%s_m%d" % (name, m), margin=m)
                pgm = tmp / ("%s_m%d.pgm" % (name, m))
                base = Image.open(r[1]).size
                to_pgm(r[1], pgm)
                cells.append("%-14s" % cell(score(r[0], pgm, base)))
            print("  %-8s %s" % (name, "  ".join(cells)))

        # --- 5. 종횡비 왜곡 ----------------------------------------------------
        print("\n종횡비 왜곡 — 가로/세로 비율이 어긋나게 스캔된 경우")
        stretches = [("정상", 1.0, 1.0), ("가로 +10%", 1.10, 1.0),
                     ("세로 +10%", 1.0, 1.10), ("가로 +25%", 1.25, 1.0),
                     ("세로 +25%", 1.0, 1.25)]
        print("  %-8s %s" % ("", "  ".join("%-14s" % n for n, _, _ in stretches)))
        for name in ("bach", "chopin"):
            r = R.render(name, "%s_st" % name)
            cells = []
            for label, sx, sy in stretches:
                pgm = tmp / ("%s_st_%s.pgm" % (name, label.replace(" ", "").replace("%", "")))
                base = stretch_to_pgm(r[1], pgm, sx, sy)
                cells.append("%-14s" % cell(score(r[0], pgm, base)))
            print("  %-8s %s" % (name, "  ".join(cells)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
