#!/usr/bin/env python3
"""V1/V2 평가 하네스.

Verovio가 렌더한 SVG에서 정답(오선 위치, 마디선 위치)을 추출하고, nota-analyze의
검출 결과와 대조해 precision/recall을 계산한다.

알고리즘은 전혀 구현하지 않는다. 코어 CLI를 호출하고 채점만 한다
(docs/DESIGN.md 9.3절). 검증하는 코드와 배포하는 코드가 같아야 하기 때문이다.

마디선은 시스템별로 나누어 비교한다. 서로 다른 시스템의 마디선이 같은 x를
가지는 일이 흔하므로 전역으로 묶으면 정답 개수부터 틀린다.

사용법:
    python3 prototype/evaluate_page.py <page.svg> <page.pgm>
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def parse_ground_truth(svg_text, img_w, img_h):
    """Verovio SVG에서 정답을 뽑아 픽셀 좌표로 환산한다.

    주의할 점 두 가지:
      - page-margin 그룹에 translate 변환이 걸려 있다. 적용하지 않으면 모든
        좌표가 어긋난다.
      - 마디선 하나가 path 여러 개로 그려진다(위 오선 구간, 아래 오선 구간,
        둘을 잇는 구간). 그룹당 하나로 합쳐야 개수가 맞는다.
    """
    vb = re.search(r'viewBox="([\d.\-]+) ([\d.\-]+) ([\d.\-]+) ([\d.\-]+)"', svg_text)
    if not vb:
        raise SystemExit("viewBox를 찾을 수 없다")
    sx = img_w / float(vb.group(3))
    sy = img_h / float(vb.group(4))

    tm = re.search(r'class="page-margin"[^>]*transform="translate\(([\d.\-]+),\s*([\d.\-]+)\)"',
                   svg_text)
    tx, ty = (float(tm.group(1)), float(tm.group(2))) if tm else (0.0, 0.0)

    def px(x):
        return (x + tx) * sx

    def py(y):
        return (y + ty) * sy

    staff_lines = []
    for block in re.findall(
            r'<g[^>]*class="staff"[^>]*>(.*?)(?=<g[^>]*class="staff"|\Z)', svg_text, re.S):
        found = []
        for m in re.finditer(
                r'<path d="M([\d.\-]+) ([\d.\-]+) L([\d.\-]+) ([\d.\-]+)"', block):
            x1, y1, x2, y2 = (float(g) for g in m.groups())
            if abs(y1 - y2) < 1.0 and (x2 - x1) > 500.0:
                found.append(py(y1))
        staff_lines.extend(found[:5])

    barlines = []
    for block in re.findall(r'<g[^>]*class="barLine"[^>]*>(.*?)</g>', svg_text, re.S):
        xs, ys = [], []
        for m in re.finditer(
                r'<path d="M([\d.\-]+) ([\d.\-]+) L([\d.\-]+) ([\d.\-]+)"', block):
            x1, y1, x2, y2 = (float(g) for g in m.groups())
            if abs(x1 - x2) < 30.0 and abs(y2 - y1) > 50.0:
                xs.append((x1 + x2) / 2.0)
                ys += [y1, y2]
        if xs:  # 그룹당 하나
            barlines.append((px(sum(xs) / len(xs)), py((min(ys) + max(ys)) / 2.0)))

    return {
        "staff_line_y": sorted({round(y, 1) for y in staff_lines}),
        "barlines": barlines,
        "system_count": len(re.findall(r'class="system"', svg_text)),
        "measure_count": len(re.findall(r'class="measure"', svg_text)),
    }


def match_1d(truth, detected, tol):
    used = [False] * len(detected)
    errs, tp = [], 0
    for t in sorted(truth):
        best, best_d = -1, tol + 1e-9
        for i, d in enumerate(detected):
            if used[i]:
                continue
            dist = abs(d - t)
            if dist < best_d:
                best, best_d = i, dist
        if best >= 0:
            used[best] = True
            tp += 1
            errs.append(best_d)
    return tp, len(detected) - tp, len(truth) - tp, errs


def prf(tp, fp, fn):
    p = tp / (tp + fp) if tp + fp else 0.0
    r = tp / (tp + fn) if tp + fn else 0.0
    f = 2 * p * r / (p + r) if p + r else 0.0
    return p, r, f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("svg")
    ap.add_argument("pgm")
    ap.add_argument("--analyze", default="./build/tools/nota-analyze")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    out = subprocess.run([args.analyze, args.pgm], capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stderr, file=sys.stderr)
        raise SystemExit("nota-analyze 실패")
    det = json.loads(out.stdout)

    img_w, img_h = det["image"]["width"], det["image"]["height"]
    gt = parse_ground_truth(Path(args.svg).read_text(), img_w, img_h)
    space = det["median_staff_space"] or 10.0
    tol = max(4.0, space * 0.8)

    systems = det.get("systems", [])
    # 정답 마디선을 검출된 시스템의 수직 범위에 배정한다.
    gt_by_sys = {i: [] for i in range(len(systems))}
    unassigned = 0
    for x, y in gt["barlines"]:
        hit = None
        for i, s in enumerate(systems):
            if s["y_top"] - space * 2 <= y <= s["y_bottom"] + space * 2:
                hit = i
                break
        if hit is None:
            unassigned += 1
        else:
            gt_by_sys[hit].append(x)

    # 검출 마디 경계: 각 마디의 우측 끝 = 그 마디를 닫는 마디선.
    # Verovio의 barLine 의미(마디마다 닫는 선 하나)와 같다.
    det_by_sys = {i: [] for i in range(len(systems))}
    for m in det.get("measures", []):
        det_by_sys.setdefault(m["system"], []).append(m["x"][1])

    tot_tp = tot_fp = tot_fn = 0
    all_errs = []
    rows = []
    for i in range(len(systems)):
        tp, fp, fn, errs = match_1d(gt_by_sys[i], det_by_sys.get(i, []), tol)
        tot_tp += tp; tot_fp += fp; tot_fn += fn
        all_errs += errs
        rows.append((i, len(gt_by_sys[i]), len(det_by_sys.get(i, [])), tp, fp, fn))

    p, r, f = prf(tot_tp, tot_fp, tot_fn)

    print("=" * 66)
    print(" %s" % Path(args.pgm).name)
    print("=" * 66)
    print(" 이미지 %d x %d   오선간격 %.2f px   허용오차 %.1f px"
          % (img_w, img_h, space, tol))
    print()
    print(" %-16s %8s %8s" % ("", "정답", "검출"))
    print(" %-16s %8d %8d" % ("시스템", gt["system_count"], det["system_count"]))
    print(" %-16s %8d %8d" % ("마디", gt["measure_count"], det["measure_count"]))
    print(" %-16s %8d %8d" % ("오선 묶음", len(gt["staff_line_y"]) // 5, det["staff_count"]))
    if unassigned:
        print(" (시스템에 배정 못한 정답 마디선: %d)" % unassigned)
    print()
    if args.verbose:
        print(" 시스템별  정답 검출  TP FP FN")
        for i, g, d, tp, fp, fn in rows:
            flag = "" if fp == 0 and fn == 0 else "   <-- 불일치"
            print("   #%-2d     %4d %4d  %3d %2d %2d%s" % (i, g, d, tp, fp, fn, flag))
        print()
    print(" 마디 경계:  TP %d  FP %d  FN %d" % (tot_tp, tot_fp, tot_fn))
    print("   정밀도 %.3f   재현율 %.3f   F %.3f" % (p, r, f))
    if all_errs:
        print("   위치오차  평균 %.2f px  최대 %.2f px"
              % (sum(all_errs) / len(all_errs), max(all_errs)))
    print()
    print(" 판정: %s  (V2 기준 F >= 0.90)" % ("통과" if f >= 0.90 else "실패"))
    return 0 if f >= 0.90 else 1


if __name__ == "__main__":
    sys.exit(main())
