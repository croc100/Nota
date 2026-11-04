// 시간↔픽셀 매핑 테스트.
//
// 이 모듈은 이미지를 보지 않는다. 입력이 PageAnalysis 구조체뿐이므로 정답을
// 손으로 적을 수 있고, 검출기의 오차가 섞이지 않는다. 따라서 합성 이미지가
// 아니라 구조체를 직접 만들어 쓴다.
//
// 지켜야 할 성질:
//   1. 단조성 — 박이 늘면 y가 줄지 않는다. 스크롤이 역행하면 안 된다.
//   2. 왕복   — BeatAt(CanvasY(b)) == b
//   3. 연속성 — 한 시스템을 연주하는 동안 y가 시스템 하나 높이만큼 흐른다
//               (멈춤→점프가 아니다). 이것이 설계의 핵심 결정이다.

#include <cmath>
#include <vector>

#include "nota/score_geometry.hpp"
#include "test_framework.hpp"

namespace {

using nota::GeometryMeasure;
using nota::GeometrySystem;
using nota::Measure;
using nota::PageAnalysis;
using nota::ScoreGeometry;
using nota::ScoreGeometryParams;
using nota::System;

// 균일한 가짜 페이지. 시스템 하나당 마디 measures_per_system개.
//
// system_height: 오선 상단~하단, system_pitch: 시스템 시작 y 사이의 간격.
PageAnalysis MakePage(int system_count, const std::vector<int>& measures_per_system,
                      double first_top = 100.0, double system_height = 120.0,
                      double system_pitch = 240.0, int ink_per_measure = 100) {
  PageAnalysis page;
  for (int i = 0; i < system_count; ++i) {
    System sys;
    sys.first_staff = i * 2;
    sys.staff_count = 2;
    sys.y_top = first_top + system_pitch * i;
    sys.y_bottom = sys.y_top + system_height;
    sys.x_start = 80.0;
    sys.x_end = 920.0;
    sys.staff_space = 12.0;
    page.systems.push_back(sys);

    const int n = measures_per_system[static_cast<std::size_t>(i)];
    const double width = (sys.x_end - sys.x_start) / (n > 0 ? n : 1);
    for (int m = 0; m < n; ++m) {
      Measure measure;
      measure.index = static_cast<int>(page.measures.size());
      measure.system_index = i;
      measure.x_start = sys.x_start + width * m;
      measure.x_end = measure.x_start + width;
      measure.y_top = sys.y_top;
      measure.y_bottom = sys.y_bottom;
      measure.ink_density = ink_per_measure;
      page.measures.push_back(measure);
    }
  }
  page.quality.dominant_staff_count = 2;
  page.quality.consistent_systems = system_count;
  page.quality.total_systems = system_count;
  return page;
}

}  // namespace

// 시스템마다 마디 수가 다른 악보에서, 마디 수에 비례해 시간이 배분된다.
// "초당 N픽셀" 고정 속도가 틀어지는 바로 그 상황이다 (DESIGN.md 6절).
NOTA_TEST(GeometryDistributesBeatsByMeasureCount) {
  const PageAnalysis page = MakePage(3, {2, 6, 4});
  const ScoreGeometry geom = ScoreGeometry::Build({page}, {1000});

  CHECK(!geom.empty());
  CHECK_EQ(geom.systems().size(), static_cast<std::size_t>(3));
  // 12마디 × 4박
  CHECK_NEAR(geom.total_beats(), 48.0, 1e-9);

  CHECK_NEAR(geom.systems()[0].beat_start, 0.0, 1e-9);
  CHECK_NEAR(geom.systems()[0].beat_end, 8.0, 1e-9);
  CHECK_NEAR(geom.systems()[1].beat_end, 32.0, 1e-9);
  CHECK_NEAR(geom.systems()[2].beat_end, 48.0, 1e-9);

  // 마디가 많은 시스템은 같은 높이를 더 오래 걸려 지난다 = 픽셀/박이 작다.
  CHECK(geom.PixelsPerBeat(4.0) > geom.PixelsPerBeat(20.0));
  CHECK_NEAR(geom.PixelsPerBeat(4.0), 240.0 / 8.0, 1e-9);
  CHECK_NEAR(geom.PixelsPerBeat(20.0), 240.0 / 24.0, 1e-9);
}

// 핵심 결정의 검증: 한 시스템을 연주하는 동안 y가 멈춰 있지 않고 시스템
// 하나 높이(= 시스템 피치)만큼 연속으로 흐른다.
NOTA_TEST(GeometryFlowsContinuouslyWithinSystem) {
  const PageAnalysis page = MakePage(3, {4, 4, 4});
  const ScoreGeometry geom = ScoreGeometry::Build({page}, {1000});

  const GeometrySystem& s0 = geom.systems()[0];
  const GeometrySystem& s1 = geom.systems()[1];

  // 시스템 시작에서 y는 그 시스템의 상단에 있다.
  CHECK_NEAR(geom.CanvasY(s0.beat_start), s0.canvas_y_top, 1e-9);
  // 시스템을 마칠 때 y는 다음 시스템의 상단에 와 있다 — 즉 한 칸 올라갔다.
  CHECK_NEAR(geom.CanvasY(s0.beat_end), s1.canvas_y_top, 1e-9);
  // 중간에서는 정확히 절반.
  CHECK_NEAR(geom.CanvasY(8.0), (s0.canvas_y_top + s1.canvas_y_top) / 2.0, 1e-9);

  // 점프가 없다: 촘촘히 샘플링해도 한 걸음의 변화량이 일정 범위를 넘지 않는다.
  const double step = geom.total_beats() / 2000.0;
  double prev = geom.CanvasY(0.0);
  double max_jump = 0.0;
  for (int i = 1; i <= 2000; ++i) {
    const double y = geom.CanvasY(step * i);
    max_jump = std::fmax(max_jump, y - prev);
    prev = y;
  }
  // 전체를 2000등분했으므로 균일 흐름이면 걸음당 (캔버스 진행)/2000 정도다.
  CHECK(max_jump < 2.0);
}

// 단조성. 마디선이 과검출되어도 깨지지 않아야 하는 성질이다.
NOTA_TEST(GeometryIsMonotonic) {
  // 시스템마다 마디 수가 들쭉날쭉한 경우 (3번째는 과검출을 모사해 11개).
  const PageAnalysis page = MakePage(4, {3, 5, 11, 1});
  const ScoreGeometry geom = ScoreGeometry::Build({page}, {1200});

  double prev_y = -1e18;
  const double step = geom.total_beats() / 5000.0;
  for (int i = 0; i <= 5000; ++i) {
    const double y = geom.CanvasY(step * i);
    CHECK(y >= prev_y);
    prev_y = y;
  }

  // 마디 눈금도 단조 증가하며 시스템 구간을 빈틈 없이 덮는다.
  const std::vector<GeometryMeasure>& measures = geom.measures();
  CHECK_EQ(measures.size(), static_cast<std::size_t>(20));
  for (std::size_t i = 0; i < measures.size(); ++i) {
    CHECK_EQ(measures[i].index, static_cast<int>(i));
    CHECK(measures[i].beat_end >= measures[i].beat_start);
    if (i > 0) CHECK_NEAR(measures[i].beat_start, measures[i - 1].beat_end, 1e-9);
  }
  CHECK_NEAR(measures.back().beat_end, geom.total_beats(), 1e-9);
}

// 왕복. 손 스크롤 후 위치를 받아들일 때 쓰므로 (DESIGN.md 2.4) 역함수여야 한다.
NOTA_TEST(GeometryRoundTripsBeatAndCanvasY) {
  const PageAnalysis page = MakePage(5, {2, 7, 3, 4, 6});
  const ScoreGeometry geom = ScoreGeometry::Build({page}, {1400});

  for (int i = 0; i <= 400; ++i) {
    const double beat = geom.total_beats() * i / 400.0;
    const double y = geom.CanvasY(beat);
    CHECK_NEAR(geom.BeatAt(y), beat, 1e-6);
  }

  // 반대 방향도 성립한다 (앵커 구간 안이면).
  const double y0 = geom.systems().front().canvas_y_top;
  const double y1 = geom.systems().back().canvas_y_bottom;
  for (int i = 0; i <= 400; ++i) {
    const double y = y0 + (y1 - y0) * i / 400.0;
    CHECK_NEAR(geom.CanvasY(geom.BeatAt(y)), y, 1e-6);
  }
}

// 페이지 이어붙이기. 두 번째 페이지의 좌표에 첫 페이지 높이가 더해져야 한다.
NOTA_TEST(GeometryConcatenatesPages) {
  const PageAnalysis p1 = MakePage(2, {4, 4});
  const PageAnalysis p2 = MakePage(2, {4, 4});

  ScoreGeometryParams params;
  params.page_gap = 30.0;
  const ScoreGeometry geom = ScoreGeometry::Build({p1, p2}, {1000, 1000}, params);

  CHECK_EQ(geom.systems().size(), static_cast<std::size_t>(4));
  CHECK_EQ(geom.systems()[2].page_index, 1);
  CHECK_EQ(geom.systems()[2].index_in_page, 0);

  // 2페이지 첫 시스템 = 원래 y_top(100) + 1000 + 30
  CHECK_NEAR(geom.systems()[2].canvas_y_top, 1130.0, 1e-9);
  CHECK_NEAR(geom.canvas_height(), 2000.0 + 30.0, 1e-9);
  // 16마디 × 4박
  CHECK_NEAR(geom.total_beats(), 64.0, 1e-9);

  // 페이지 경계를 넘어도 매핑은 끊기지 않는다.
  CHECK_NEAR(geom.CanvasY(geom.systems()[1].beat_end),
             geom.systems()[2].canvas_y_top, 1e-9);

  // 마디 좌표도 같은 오프셋을 받는다.
  bool saw_page2_measure = false;
  for (const GeometryMeasure& m : geom.measures()) {
    if (m.system_index == 2) {
      CHECK_NEAR(m.canvas_y_top, 1130.0, 1e-9);
      saw_page2_measure = true;
    }
  }
  CHECK(saw_page2_measure);
}

// 범위 밖은 양 끝으로 고정된다. 오디오 추정이 음수 박이나 곡 길이 초과를
// 내놓아도 스크롤이 캔버스를 벗어나지 않아야 한다.
NOTA_TEST(GeometryClampsOutOfRange) {
  const PageAnalysis page = MakePage(3, {4, 4, 4});
  const ScoreGeometry geom = ScoreGeometry::Build({page}, {1000});

  const double y_first = geom.systems().front().canvas_y_top;
  const double y_last = geom.systems().back().canvas_y_bottom;

  CHECK_NEAR(geom.CanvasY(-100.0), y_first, 1e-9);
  CHECK_NEAR(geom.CanvasY(0.0), y_first, 1e-9);
  CHECK_NEAR(geom.CanvasY(geom.total_beats() + 500.0), y_last, 1e-9);

  CHECK_NEAR(geom.BeatAt(-5000.0), 0.0, 1e-9);
  CHECK_NEAR(geom.BeatAt(1e9), geom.total_beats(), 1e-9);

  CHECK_EQ(geom.SystemAt(-1.0), 0);
  CHECK_EQ(geom.SystemAt(1e9), 2);
}

// 두 가지 시간 배분 방식.
//
// 마디 수 기준은 과검출을 그대로 반영하고, 잉크 기준은 반영하지 않는다
// (경계를 몇 개 긋든 시스템 안의 잉크 총량은 같다). 그 차이를 고정한다.
NOTA_TEST(GeometryTimeWeightModes) {
  // 두 시스템의 잉크 총량은 같지만(600), 마디선 검출 결과가 다르다.
  // 시스템 0: 2마디 × 300,  시스템 1: 6마디 × 100
  PageAnalysis page;
  for (int i = 0; i < 2; ++i) {
    System sys;
    sys.staff_count = 2;
    sys.y_top = 100.0 + 240.0 * i;
    sys.y_bottom = sys.y_top + 120.0;
    sys.x_start = 80.0;
    sys.x_end = 920.0;
    sys.staff_space = 12.0;
    page.systems.push_back(sys);

    const int n = (i == 0) ? 2 : 6;
    const int ink = (i == 0) ? 300 : 100;
    const double width = (sys.x_end - sys.x_start) / n;
    for (int m = 0; m < n; ++m) {
      Measure measure;
      measure.index = static_cast<int>(page.measures.size());
      measure.system_index = i;
      measure.x_start = sys.x_start + width * m;
      measure.x_end = measure.x_start + width;
      measure.y_top = sys.y_top;
      measure.y_bottom = sys.y_bottom;
      measure.ink_density = ink;
      page.measures.push_back(measure);
    }
  }

  ScoreGeometryParams by_measure;
  by_measure.time_weight = ScoreGeometryParams::TimeWeight::kMeasureCount;
  const ScoreGeometry gm = ScoreGeometry::Build({page}, {1000}, by_measure);

  ScoreGeometryParams by_ink;
  by_ink.time_weight = ScoreGeometryParams::TimeWeight::kInkDensity;
  const ScoreGeometry gi = ScoreGeometry::Build({page}, {1000}, by_ink);

  // 총 길이는 기준과 무관하게 같다 — 기준은 배분만 바꾼다.
  CHECK_NEAR(gm.total_beats(), 32.0, 1e-9);
  CHECK_NEAR(gi.total_beats(), 32.0, 1e-9);

  // 마디 수 기준: 2 : 6 으로 갈린다.
  CHECK_NEAR(gm.systems()[0].beat_end, 8.0, 1e-9);
  // 잉크 기준: 잉크가 같으므로 반씩 갈린다.
  CHECK_NEAR(gi.systems()[0].beat_end, 16.0, 1e-9);

  // 어느 쪽이든 단조성과 왕복은 유지된다.
  for (const ScoreGeometry* g : {&gm, &gi}) {
    double prev = -1e18;
    for (int i = 0; i <= 500; ++i) {
      const double beat = g->total_beats() * i / 500.0;
      const double y = g->CanvasY(beat);
      CHECK(y >= prev);
      prev = y;
      CHECK_NEAR(g->BeatAt(y), beat, 1e-6);
    }
  }
}

// 곡 길이를 외부에서 주면 축척이 검출 결과와 무관해진다.
//
// 마디선 과검출은 축척 오차가 된다 — 실제 73마디를 89개로 검출하면 박 축이
// 1.22배 늘어난다 (ALGORITHM.md 8.4). 과검출된 매핑과 정확한 매핑이 같은 박에서
// 같은 y를 내놓는지로 그 분리를 고정한다.
NOTA_TEST(GeometryTotalBeatsOverrideFixesScale) {
  // 같은 악보를 두 번 해석한다. 두 번째는 각 시스템의 마디가 두 개로 쪼개져
  // 검출된 경우(과검출)다. 조판은 동일하므로 y 좌표는 같다.
  const PageAnalysis clean = MakePage(3, {4, 4, 4});
  const PageAnalysis split = MakePage(3, {8, 8, 8});

  const ScoreGeometry good = ScoreGeometry::Build({clean}, {1000});
  CHECK_NEAR(good.total_beats(), 48.0, 1e-9);

  // 보정 없이: 축척이 2배로 늘어난다.
  const ScoreGeometry naive = ScoreGeometry::Build({split}, {1000});
  CHECK_NEAR(naive.total_beats(), 96.0, 1e-9);
  // 같은 박(32박 = 참값으로는 마지막 시스템)을 전혀 다른 y로 보낸다.
  CHECK(std::fabs(naive.CanvasY(32.0) - good.CanvasY(32.0)) > 100.0);

  // 길이를 주면: 눈금은 두 배로 촘촘하지만 축척은 정확히 복원된다.
  ScoreGeometryParams params;
  params.total_beats_override = 48.0;
  const ScoreGeometry fixed = ScoreGeometry::Build({split}, {1000}, params);
  CHECK_NEAR(fixed.total_beats(), 48.0, 1e-9);
  CHECK_EQ(fixed.measures().size(), static_cast<std::size_t>(24));
  for (int i = 0; i <= 200; ++i) {
    const double beat = 48.0 * i / 200.0;
    CHECK_NEAR(fixed.CanvasY(beat), good.CanvasY(beat), 1e-6);
  }

  // 0은 "유도하라"는 뜻이고, 음수도 같게 다룬다.
  ScoreGeometryParams negative;
  negative.total_beats_override = -10.0;
  CHECK_NEAR(ScoreGeometry::Build({clean}, {1000}, negative).total_beats(), 48.0,
             1e-9);
}

// 퇴화 입력에서 조용히 터지지 않는다.
NOTA_TEST(GeometryHandlesDegenerateInput) {
  // 입력 길이 불일치 → 빈 결과
  const PageAnalysis page = MakePage(2, {4, 4});
  CHECK(ScoreGeometry::Build({page}, {}).empty());
  CHECK(ScoreGeometry::Build({}, {}).empty());

  // 박자 0 → 빈 결과 (0으로 나누기 방지)
  ScoreGeometryParams bad;
  bad.beats_per_measure = 0.0;
  CHECK(ScoreGeometry::Build({page}, {1000}, bad).empty());

  // 마디선을 하나도 못 찾은 페이지: 시스템 골격만으로 매핑이 성립해야 한다.
  const PageAnalysis no_bars = MakePage(3, {0, 0, 0});
  const ScoreGeometry geom = ScoreGeometry::Build({no_bars}, {1000});
  CHECK(!geom.empty());
  CHECK(geom.measures().empty());
  CHECK_NEAR(geom.total_beats(), 12.0, 1e-9);  // 시스템 하나를 한 마디로 센다
  CHECK_NEAR(geom.CanvasY(geom.systems()[0].beat_end),
             geom.systems()[1].canvas_y_top, 1e-9);
  for (int i = 0; i <= 200; ++i) {
    const double beat = geom.total_beats() * i / 200.0;
    CHECK_NEAR(geom.BeatAt(geom.CanvasY(beat)), beat, 1e-6);
  }
}

// 분석기가 시스템을 y 순서로 주지 않아도 매핑은 위에서 아래로 정렬된다.
// 매핑의 단조성이 이 순서에 의존하므로 믿지 않고 정렬한다.
NOTA_TEST(GeometrySortsSystemsByY) {
  PageAnalysis page = MakePage(3, {4, 4, 4});
  // 시스템 순서를 뒤집는다. measures의 system_index는 원래 인덱스를 가리키므로
  // 그대로 둔다 — Build가 index_in_page로 되짚어야 한다.
  std::swap(page.systems[0], page.systems[2]);
  for (nota::Measure& m : page.measures) {
    if (m.system_index == 0) {
      m.system_index = 2;
      m.y_top = page.systems[2].y_top;
      m.y_bottom = page.systems[2].y_bottom;
    } else if (m.system_index == 2) {
      m.system_index = 0;
      m.y_top = page.systems[0].y_top;
      m.y_bottom = page.systems[0].y_bottom;
    }
  }

  const ScoreGeometry geom = ScoreGeometry::Build({page}, {1000});
  CHECK_EQ(geom.systems().size(), static_cast<std::size_t>(3));
  for (std::size_t i = 1; i < geom.systems().size(); ++i) {
    CHECK(geom.systems()[i].canvas_y_top > geom.systems()[i - 1].canvas_y_top);
  }
  // 각 시스템이 자기 마디를 제대로 끌어왔다.
  for (const GeometrySystem& s : geom.systems()) CHECK_EQ(s.measure_count, 4);
}
