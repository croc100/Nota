#!/usr/bin/env python3
"""열화 요인별로 검출이 어디서 무너지는지 측정한다.

각 요인을 하나씩 독립적으로 올려가며 본다. 한꺼번에 섞으면 무엇이 원인인지
알 수 없다. 프리셋(실제 스캔에 가까운 조합)은 마지막에 따로 본다.

구조 지표(시스템·오선·마디 개수)를 먼저 본다. 기울기가 들어가면 좌표계가
회전하므로 위치 기반 F는 기울기 보정이 구현된 뒤에야 의미가 있다.

사용법:
    python3 prototype/sweep_robustness.py
"""
import json
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from evaluate_page import match_1d, parse_ground_truth, prf   # noqa: E402

from PIL import Image                                          # noqa: E402
from degrade import PRESETS, degrade                           # noqa: E402

SCORES = ["bach_bwv846", "chopin_mazurka06_2"]
SRC = pathlib.Path("prototype/out/scores")
ANALYZE = "./build/tools/nota-analyze"

SWEEPS = [
    ("기울기(도)",  "skew",  [0.0, 0.25, 0.5, 1.0, 2.0, 3.0]),
    ("음영",        "shade", [0.0, 0.2, 0.4, 0.6, 0.8]),
    ("잡음(sigma)", "noise", [0, 4, 8, 12, 20]),
    ("흐림(px)",    "blur",  [0.0, 0.5, 1.0, 1.5, 2.5]),
    ("JPEG 품질",   "jpeg",  [0, 85, 70, 50, 30]),
    ("뒷면비침",    "bleed", [0.0, 0.1, 0.2, 0.3]),
]


def analyze(pgm_path):
    out = subprocess.run([ANALYZE, str(pgm_path)], capture_output=True, text=True)
    if out.returncode != 0:
        return None
    try:
        return json.loads(out.stdout)
    except json.JSONDecodeError:
        return None


def score_once(name, tmp, **kw):
    """한 조건에서 검출을 돌리고 지표를 돌려준다."""
    src_pgm = SRC / ("%s_p1.pgm" % name)
    img = Image.open(src_pgm).convert("L")
    dst = tmp / "d.pgm"
    degrade(img, seed=7, **kw).save(dst)

    det = analyze(dst)
    if det is None:
        return None

    svg = (SRC / ("%s_p1.svg" % name)).read_text()
    gt = parse_ground_truth(svg, det["image"]["width"], det["image"]["height"])

    space = det["median_staff_space"] or 10.0
    tol = max(4.0, space * 0.8)
    systems = det.get("systems", [])
    gt_by, det_by = {}, {}
    for x, y in gt["barlines"]:
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

    return dict(
        systems=det["system_count"], staves=det["staff_count"],
        measures=det["measure_count"], f=f,
        gt_systems=gt["system_count"], gt_measures=gt["measure_count"],
        gt_staves=len(gt["staff_line_y"]) // 5,
    )


def fmt(r):
    if r is None:
        return "  실패   "
    ok_sys = r["systems"] == r["gt_systems"]
    ok_st = r["staves"] == r["gt_staves"]
    mark = "" if (ok_sys and ok_st) else "!"
    return "%2d/%-2d %2d/%-2d %5.3f%s" % (
        r["systems"], r["gt_systems"], r["staves"], r["gt_staves"], r["f"], mark)


def main():
    if not pathlib.Path(ANALYZE).exists():
        print("먼저 빌드하라: cmake --build build", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        print("표기: 시스템검출/정답  오선검출/정답  마디F   (! = 구조 불일치)")
        for label, key, levels in SWEEPS:
            print("\n%s" % label)
            print("  %-22s %s" % ("", "  ".join("%-16s" % str(v) for v in levels)))
            for name in SCORES:
                cells = []
                for v in levels:
                    kw = {key: v} if v else {}
                    cells.append("%-16s" % fmt(score_once(name, tmp, **kw)))
                print("  %-22s %s" % (name[:20], "  ".join(cells)))

        print("\n프리셋 (실제 스캔에 가까운 조합)")
        print("  %-22s %s" % ("", "  ".join("%-16s" % p for p in ["원본"] + sorted(PRESETS))))
        for name in SCORES:
            cells = ["%-16s" % fmt(score_once(name, tmp))]
            for p in sorted(PRESETS):
                cells.append("%-16s" % fmt(score_once(name, tmp, **PRESETS[p])))
            print("  %-22s %s" % (name[:20], "  ".join(cells)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
