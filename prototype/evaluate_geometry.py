#!/usr/bin/env python3
"""시간↔픽셀 매핑 평가 하네스.

측정하는 것: **악보상 위치를 정확히 알 때, 매핑이 올바른 시스템을 가리키는가.**

이것이 V6(위치 추정 드리프트, 1시스템 이내)의 전제 조건이다. 오디오 쪽이 완벽해도
매핑이 틀리면 스크롤은 틀린다. 그래서 오디오 없이, 박 위치를 정답으로 주고 매핑만
따로 떼어 측정한다.

정답은 Verovio SVG에서 가져온다. SVG에는 `class="system"` 그룹 안에
`class="measure"` 그룹이 들어 있으므로, **어느 마디가 어느 시스템에 있는지**를
수작업 없이 알 수 있다. 박자가 일정한 곡에서는 마디 번호가 곧 박 위치다.

알고리즘은 구현하지 않는다. `nota-geometry`를 호출하고 채점만 한다
(docs/DESIGN.md 9.3절).

사용법:
    python3 prototype/evaluate_geometry.py --beats 4 \
        out/scores/bach_bwv846_p1.svg:out/scores/bach_bwv846_p1.pgm \
        out/scores/bach_bwv846_p2.svg:out/scores/bach_bwv846_p2.pgm
"""
import argparse
import bisect
import json
import re
import subprocess
import sys
from pathlib import Path


def true_measures_per_system(svg_text):
    """SVG에서 시스템별 마디 수를 뽑는다. 위에서 아래 순서.

    Verovio는 시스템 그룹 안에 마디 그룹을 중첩해 넣는다. 그래서 중첩 구조를
    따라가지 않고 `class="system"` 등장 지점으로 문서를 자른 뒤 각 조각의
    마디 그룹을 세면 된다 — 조각 안의 마디는 전부 그 시스템의 것이다.
    """
    starts = [m.start() for m in re.finditer(r'class="system"', svg_text)]
    if not starts:
        return []
    bounds = starts + [len(svg_text)]
    out = []
    for i in range(len(starts)):
        chunk = svg_text[bounds[i]:bounds[i + 1]]
        out.append(len(re.findall(r'class="measure"', chunk)))
    return out


def system_at(systems, beat):
    """nota-geometry가 내놓은 시스템 목록에서 해당 박의 시스템을 찾는다.

    ScoreGeometry::SystemAt과 같은 규칙이다 (beat_start 기준 상한 탐색).
    """
    starts = [s["beats"][0] for s in systems]
    i = bisect.bisect_right(starts, beat) - 1
    return max(0, min(i, len(systems) - 1))


def canvas_y(systems, beat):
    """ScoreGeometry::CanvasY와 같은 보간. 채점을 위해 파이썬에서 되짚는다."""
    i = system_at(systems, beat)
    s = systems[i]
    b0, b1 = s["beats"]
    y0 = s["canvas_y"][0]
    y1 = systems[i + 1]["canvas_y"][0] if i + 1 < len(systems) else s["canvas_y"][1]
    if b1 <= b0:
        return y0
    t = min(1.0, max(0.0, (beat - b0) / (b1 - b0)))
    return y0 + t * (y1 - y0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pairs", nargs="+", metavar="page.svg:page.pgm",
                    help="페이지 순서대로. 곡의 순서가 곧 이 순서다")
    ap.add_argument("--beats", type=float, default=4.0, help="한 마디의 박 수")
    ap.add_argument("--weight", default="measures", choices=["measures", "ink"])
    ap.add_argument("--bin", default="build/tools/nota-geometry")
    ap.add_argument("--true-length", action="store_true",
                    help="곡 길이를 정답에서 가져와 매핑에 준다. 축척 오차를 빼고 "
                         "배분만 평가한다 (ScoreGeometryParams::total_beats_override)")
    args = ap.parse_args()

    svgs, pgms = [], []
    for pair in args.pairs:
        if ":" not in pair:
            print("error: %s — svg:pgm 형식이어야 한다" % pair, file=sys.stderr)
            return 2
        svg, pgm = pair.rsplit(":", 1)
        svgs.append(Path(svg))
        pgms.append(Path(pgm))

    # 정답: 페이지를 이어붙인 하나의 시스템 목록
    truth = []
    for svg in svgs:
        truth.extend(true_measures_per_system(svg.read_text()))
    if not truth:
        print("error: SVG에서 시스템을 찾지 못했다", file=sys.stderr)
        return 1

    cmd = [args.bin, *[str(p) for p in pgms],
           "--beats", str(args.beats), "--weight", args.weight]
    if args.true_length:
        cmd += ["--total-beats", str(sum(truth) * args.beats)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        return 1
    geom = json.loads(proc.stdout)
    systems = geom["systems"]

    print("정답 시스템 %d개 (마디 %d개), 검출 시스템 %d개 (마디 %d개)" %
          (len(truth), sum(truth), len(systems), geom["measure_count"]))
    if len(truth) != len(systems):
        print("  주의: 시스템 개수가 다르다 — 아래 점수는 의미가 약하다")

    # 각 정답 마디의 중간 지점을 물어본다. 연주자는 마디 안에 있지 경계에 있지
    # 않으므로 중간이 현실적인 질의점이다.
    n_sys = min(len(truth), len(systems))
    hits = 0
    total = 0
    err_hist = {}
    worst = []
    measure_index = 0
    for true_sys in range(len(truth)):
        for _ in range(truth[true_sys]):
            beat = (measure_index + 0.5) * args.beats
            measure_index += 1
            if true_sys >= n_sys:
                continue
            got = system_at(systems, beat)
            err = got - true_sys
            err_hist[err] = err_hist.get(err, 0) + 1
            total += 1
            if err == 0:
                hits += 1
            else:
                worst.append((abs(err), measure_index - 1, true_sys, got))

    if total == 0:
        print("error: 채점할 마디가 없다", file=sys.stderr)
        return 1

    print("\n시스템 적중률  %d/%d = %.3f" % (hits, total, hits / total))
    within1 = sum(c for e, c in err_hist.items() if abs(e) <= 1)
    print("±1 시스템 이내  %d/%d = %.3f" % (within1, total, within1 / total))
    print("오차 분포      %s" %
          ", ".join("%+d: %d" % (e, err_hist[e]) for e in sorted(err_hist)))

    # 픽셀 오차도 본다. 시스템이 맞아도 시스템 안의 위치가 많이 어긋나면
    # 스프링이 매 시스템마다 크게 당겨져 흔들린다 (ALGORITHM.md 7.2).
    pitch = ((systems[-1]["canvas_y"][0] - systems[0]["canvas_y"][0]) /
             max(1, len(systems) - 1))
    diffs = []
    measure_index = 0
    for true_sys in range(min(len(truth), n_sys)):
        count = truth[true_sys]
        for k in range(count):
            beat = (measure_index + 0.5) * args.beats
            measure_index += 1
            # 정답 y: 그 시스템을 연주하는 동안 한 칸만큼 연속으로 흐른다는
            # 설계 규칙을 정답에도 똑같이 적용한다 (score_geometry.hpp).
            frac = (k + 0.5) / count
            y_true = systems[true_sys]["canvas_y"][0] + frac * pitch
            diffs.append(abs(canvas_y(systems, beat) - y_true))
    if diffs:
        diffs.sort()
        print("\n시스템 피치     %.1f px" % pitch)
        print("y 오차 중앙값   %.1f px (%.2f 시스템)" %
              (diffs[len(diffs) // 2], diffs[len(diffs) // 2] / pitch))
        print("y 오차 최대     %.1f px (%.2f 시스템)" % (diffs[-1], diffs[-1] / pitch))

    if worst:
        worst.sort(reverse=True)
        print("\n틀린 마디 (상위 5):")
        for _, m, t, g in worst[:5]:
            print("  마디 %3d: 정답 시스템 %2d → 매핑 %2d" % (m, t, g))

    return 0


if __name__ == "__main__":
    sys.exit(main())
