#!/usr/bin/env python3
"""시간↔픽셀 매핑의 오차를 측정한다.

마디선 과검출이 매핑에 얼마나 전해지는지 본다. 측정하는 양은 스크롤에 직접
영향을 주는 것이어야 한다 — 즉 "곡의 어느 지점에서 악보상 위치가 몇 박 어긋나
있는가"다.

정답은 Verovio SVG의 마디선에서 시스템별 실제 마디 수를 세어 만든다. 박자표가
고정이면 마디는 모두 같은 길이이므로, 시스템이 받아야 할 시간의 비율은
"그 시스템의 마디 수 / 전체 마디 수"다.

사용법:
    python3 prototype/evaluate_geometry.py prototype/out/scores/bach_bwv846 2
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from evaluate_page import parse_ground_truth   # noqa: E402


def detector_geometry(pgms, weight, beats_per_measure):
    cmd = ["./build/tools/nota-geometry"] + pgms + \
          ["--beats-per-measure", str(beats_per_measure), "--weight", weight]
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stderr, file=sys.stderr)
        raise SystemExit("nota-geometry 실패")
    return json.loads(out.stdout)


def truth_measures_per_system(svgs, geo):
    """검출된 시스템마다 정답 마디 수를 센다."""
    # 페이지별 시스템 목록
    by_page = {}
    for i, s in enumerate(geo["systems"]):
        by_page.setdefault(s["page"], []).append((i, s))

    counts = [0] * len(geo["systems"])
    unassigned = 0
    for p, svg_path in enumerate(svgs):
        text = Path(svg_path).read_text()
        # 페이지 이미지 크기는 geo에 없으므로 시스템 y 범위로 역산하지 않고
        # evaluate_page 와 같은 방식으로 SVG 좌표만 쓴다.
        gt = parse_ground_truth(text, PAGE_W, PAGE_H)
        systems = by_page.get(p, [])
        # 이 페이지 시스템들의 페이지 내 y 범위 (캔버스 y에서 페이지 오프셋 제거)
        offset = min(s["canvas_y"][0] for _, s in systems) - \
                 min(s["canvas_y"][0] for _, s in systems)  # 0; 아래서 직접 계산
        page_top = p * (PAGE_H)
        for x, y in gt["barlines"]:
            hit = None
            for idx, s in systems:
                lo = s["canvas_y"][0] - page_top
                hi = s["canvas_y"][1] - page_top
                span = (hi - lo) * 0.25
                if lo - span <= y <= hi + span:
                    hit = idx
                    break
            if hit is None:
                unassigned += 1
            else:
                counts[hit] += 1
    return counts, unassigned


PAGE_W, PAGE_H = 1654, 2339


def report(name, svgs, pgms, beats_per_measure):
    print("=" * 70)
    print(" %s   (%d 페이지, 마디당 %g박)" % (name, len(pgms), beats_per_measure))
    print("=" * 70)

    base = detector_geometry(pgms, "measure", beats_per_measure)
    truth_counts, unassigned = truth_measures_per_system(svgs, base)
    total_true = sum(truth_counts)
    if total_true == 0:
        print(" 정답 마디를 세지 못했다")
        return

    print(" 마디 수      정답 %d   검출 %d   (%+.1f%%)"
          % (total_true, base["measure_count"],
             100.0 * (base["measure_count"] - total_true) / total_true))
    if unassigned:
        print(" (시스템에 배정 못한 정답 마디선 %d개)" % unassigned)
    print()

    for weight in ("measure", "ink"):
        geo = detector_geometry(pgms, weight, beats_per_measure)
        total_beats_true = total_true * beats_per_measure

        # 누적 드리프트: 각 시스템 경계에서 "검출 매핑이 말하는 진행도"와
        # "실제 진행도"의 차이를 박 단위로 환산한다.
        worst = 0.0
        worst_at = 0
        cum_true = 0
        for i, s in enumerate(geo["systems"]):
            cum_true += truth_counts[i]
            frac_true = cum_true / total_true
            frac_det = s["beats"][1] / geo["total_beats"]
            drift = abs(frac_det - frac_true) * total_beats_true
            if drift > worst:
                worst, worst_at = drift, i

        # 시스템별 속도 오차: px_per_beat 가 정답 대비 몇 % 틀렸는가
        errs = []
        for i, s in enumerate(geo["systems"]):
            if truth_counts[i] == 0:
                continue
            det_beats = s["beats"][1] - s["beats"][0]
            # 같은 픽셀 구간을 몇 박에 지나가는가의 비율이 곧 속도 비율
            true_beats = truth_counts[i] * beats_per_measure
            errs.append(abs(det_beats - true_beats) / true_beats)
        mean_err = 100.0 * sum(errs) / len(errs) if errs else 0.0
        max_err = 100.0 * max(errs) if errs else 0.0

        print(" 배분기준 %-8s  속도오차 평균 %5.1f%%  최대 %5.1f%%   "
              "누적드리프트 최대 %.1f박 (시스템 #%d)"
              % (weight, mean_err, max_err, worst, worst_at))
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stem", help="예: prototype/out/scores/bach_bwv846")
    ap.add_argument("pages", type=int)
    ap.add_argument("--beats-per-measure", type=float, default=4.0)
    args = ap.parse_args()

    svgs = ["%s_p%d.svg" % (args.stem, i) for i in range(1, args.pages + 1)]
    pgms = ["%s_p%d.pgm" % (args.stem, i) for i in range(1, args.pages + 1)]
    report(Path(args.stem).name, svgs, pgms, args.beats_per_measure)
    return 0


if __name__ == "__main__":
    sys.exit(main())
