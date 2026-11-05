#include <cmath>
#include <cstddef>
#include <vector>

#include "nota/page_analysis.hpp"
#include "nota/score_geometry.hpp"
#include "synth_score.hpp"
#include "test_framework.hpp"

namespace {

struct Fixture {
  std::vector<nota::PageAnalysis> pages;
  std::vector<int> heights;
  int systems_per_page = 0;
  int measures_per_page = 0;
};

// 동일한 합성 페이지를 n장 쌓는다. 실제 악보의 다중 페이지 구조를 흉내낸다.
Fixture MakePages(int count, int systems = 3) {
  nota_test::SynthParams p;
  p.systems = systems;
  const nota_test::SynthScore score = nota_test::GenerateScore(p);
  const nota::PageAnalysis page = nota::AnalyzePageGray(score.image.view());

  Fixture f;
  for (int i = 0; i < count; ++i) {
    f.pages.push_back(page);
    f.heights.push_back(score.image.height);
  }
  f.systems_per_page = static_cast<int>(page.systems.size());
  f.measures_per_page = static_cast<int>(page.measures.size());
  return f;
}

nota::ScoreGeometry Build(const Fixture& f, nota::ScoreGeometryParams params = {}) {
  return nota::ScoreGeometry::Build(f.pages, f.heights, params);
}

}  // namespace

NOTA_TEST(GeometryFlattensPagesInOrder) {
  const Fixture f = MakePages(3);
  const nota::ScoreGeometry g = Build(f);

  CHECK_EQ(g.systems().size(),
           static_cast<std::size_t>(f.systems_per_page * 3));
  CHECK_EQ(g.measures().size(),
           static_cast<std::size_t>(f.measures_per_page * 3));

  // 캔버스 y는 페이지를 넘어가며 계속 커져야 한다.
  double prev = -1.0;
  for (const nota::GeometrySystem& s : g.systems()) {
    CHECK(s.canvas_y_top > prev);
    prev = s.canvas_y_top;
  }
}

NOTA_TEST(SecondPageIsOffsetByFirstPageHeight) {
  const Fixture f = MakePages(2);
  const nota::ScoreGeometry g = Build(f);
  const int per_page = f.systems_per_page;

  const nota::GeometrySystem& first = g.systems()[0];
  const nota::GeometrySystem& second_page_first =
      g.systems()[static_cast<std::size_t>(per_page)];

  CHECK_EQ(second_page_first.page_index, 1);
  CHECK_EQ(second_page_first.index_in_page, 0);
  CHECK_NEAR(second_page_first.canvas_y_top - first.canvas_y_top,
             static_cast<double>(f.heights[0]), 0.01);
}

NOTA_TEST(PageGapIsApplied) {
  const Fixture f = MakePages(2);
  nota::ScoreGeometryParams params;
  params.page_gap = 120.0;
  const nota::ScoreGeometry g = Build(f, params);

  const double first_top = g.systems()[0].canvas_y_top;
  const double second_top =
      g.systems()[static_cast<std::size_t>(f.systems_per_page)].canvas_y_top;
  CHECK_NEAR(second_top - first_top,
             static_cast<double>(f.heights[0]) + 120.0, 0.01);
}

NOTA_TEST(TotalBeatsFollowsMeasureCount) {
  const Fixture f = MakePages(2);
  nota::ScoreGeometryParams params;
  params.beats_per_measure = 3.0;
  const nota::ScoreGeometry g = Build(f, params);

  CHECK_NEAR(g.total_beats(),
             static_cast<double>(f.measures_per_page * 2) * 3.0, 1e-9);
}

NOTA_TEST(CanvasYIsMonotonic) {
  const Fixture f = MakePages(2);
  const nota::ScoreGeometry g = Build(f);

  double prev = -1e9;
  for (int i = 0; i <= 400; ++i) {
    const double beat = g.total_beats() * static_cast<double>(i) / 400.0;
    const double y = g.CanvasY(beat);
    CHECK(y >= prev);
    prev = y;
  }
}

NOTA_TEST(BeatAtIsInverseOfCanvasY) {
  const Fixture f = MakePages(2);
  const nota::ScoreGeometry g = Build(f);

  for (int i = 0; i <= 200; ++i) {
    const double beat = g.total_beats() * static_cast<double>(i) / 200.0;
    const double round_trip = g.BeatAt(g.CanvasY(beat));
    CHECK_NEAR(round_trip, beat, 1e-6);
  }
}

NOTA_TEST(OneSystemOfMusicAdvancesOneSystemPitch) {
  // 이 매핑의 핵심 설계다. 한 시스템을 연주하는 동안 y는 다음 시스템이 그
  // 자리로 올라올 만큼 움직여야 한다. 멈췄다 점프하는 것이 아니라 연속으로.
  const Fixture f = MakePages(1, 4);
  const nota::ScoreGeometry g = Build(f);
  const std::vector<nota::GeometrySystem>& sys = g.systems();
  CHECK(sys.size() >= 3);

  for (std::size_t i = 0; i + 1 < sys.size(); ++i) {
    const double y_begin = g.CanvasY(sys[i].beat_start);
    const double y_end = g.CanvasY(sys[i].beat_end);
    const double pitch = sys[i + 1].canvas_y_top - sys[i].canvas_y_top;
    CHECK_NEAR(y_end - y_begin, pitch, 0.01);
  }

  // 시스템 중간에서도 연속이어야 한다 — 구간 끝에서 값이 튀면 안 된다.
  for (std::size_t i = 0; i + 1 < sys.size(); ++i) {
    const double at_end = g.CanvasY(sys[i].beat_end);
    const double at_next_start = g.CanvasY(sys[i + 1].beat_start);
    CHECK_NEAR(at_end, at_next_start, 1e-9);
  }
}

NOTA_TEST(PixelsPerBeatMatchesLocalSlope) {
  const Fixture f = MakePages(1, 4);
  const nota::ScoreGeometry g = Build(f);

  for (const nota::GeometrySystem& s : g.systems()) {
    // 시스템 안쪽 한 점에서 유한차분과 비교한다. 구간 안에서는 선형이므로
    // 정확히 일치해야 한다.
    const double mid = 0.5 * (s.beat_start + s.beat_end);
    const double h = 1e-4;
    const double slope = (g.CanvasY(mid + h) - g.CanvasY(mid - h)) / (2 * h);
    CHECK_NEAR(g.PixelsPerBeat(mid), slope, 1e-3);
    CHECK(g.PixelsPerBeat(mid) > 0.0);
  }
}

NOTA_TEST(SystemAtFindsTheRightSystem) {
  const Fixture f = MakePages(2);
  const nota::ScoreGeometry g = Build(f);

  for (std::size_t i = 0; i < g.systems().size(); ++i) {
    const nota::GeometrySystem& s = g.systems()[i];
    const double mid = 0.5 * (s.beat_start + s.beat_end);
    CHECK_EQ(g.SystemAt(mid), static_cast<int>(i));
  }
}

NOTA_TEST(OutOfRangeBeatsAreClamped) {
  const Fixture f = MakePages(1);
  const nota::ScoreGeometry g = Build(f);

  CHECK_NEAR(g.CanvasY(-100.0), g.CanvasY(0.0), 1e-9);
  CHECK_NEAR(g.CanvasY(g.total_beats() + 100.0), g.CanvasY(g.total_beats()),
             1e-9);
  // 곡이 끝난 뒤에도 계속 흘러내려가면 안 된다.
  CHECK(g.CanvasY(g.total_beats() + 1000.0) <= g.canvas_height());
}

NOTA_TEST(OutOfRangeCanvasYIsClamped) {
  const Fixture f = MakePages(1);
  const nota::ScoreGeometry g = Build(f);

  CHECK_NEAR(g.BeatAt(-5000.0), 0.0, 1e-9);
  CHECK_NEAR(g.BeatAt(1e9), g.total_beats(), 1e-6);
}

NOTA_TEST(InkDensityWeightingKeepsTotalButShiftsDistribution) {
  const Fixture f = MakePages(1, 4);

  nota::ScoreGeometryParams by_measure;
  nota::ScoreGeometryParams by_ink;
  by_ink.time_weight = nota::ScoreGeometryParams::TimeWeight::kInkDensity;

  const nota::ScoreGeometry a = Build(f, by_measure);
  const nota::ScoreGeometry b = Build(f, by_ink);

  // 전체 길이는 같아야 한다. 배분 비율만 다르다.
  CHECK_NEAR(a.total_beats(), b.total_beats(), 1e-9);
  CHECK_EQ(a.systems().size(), b.systems().size());

  // 두 방식 모두 단조성과 역함수를 유지해야 한다.
  for (int i = 0; i <= 100; ++i) {
    const double beat = b.total_beats() * static_cast<double>(i) / 100.0;
    CHECK_NEAR(b.BeatAt(b.CanvasY(beat)), beat, 1e-6);
  }
}

NOTA_TEST(MeasuresTileTheirSystemWithoutGaps) {
  const Fixture f = MakePages(1, 3);
  const nota::ScoreGeometry g = Build(f);

  for (std::size_t s = 0; s < g.systems().size(); ++s) {
    std::vector<const nota::GeometryMeasure*> in_system;
    for (const nota::GeometryMeasure& m : g.measures()) {
      if (static_cast<std::size_t>(m.system_index) == s) in_system.push_back(&m);
    }
    CHECK(!in_system.empty());
    CHECK_NEAR(in_system.front()->beat_start, g.systems()[s].beat_start, 1e-9);
    CHECK_NEAR(in_system.back()->beat_end, g.systems()[s].beat_end, 1e-6);
    for (std::size_t k = 0; k + 1 < in_system.size(); ++k) {
      CHECK_NEAR(in_system[k]->beat_end, in_system[k + 1]->beat_start, 1e-9);
    }
  }
}

NOTA_TEST(EmptyInputYieldsEmptyGeometry) {
  const nota::ScoreGeometry g =
      nota::ScoreGeometry::Build({}, {}, nota::ScoreGeometryParams());
  CHECK(g.empty());
  CHECK_NEAR(g.CanvasY(10.0), 0.0, 1e-9);
  CHECK_NEAR(g.BeatAt(10.0), 0.0, 1e-9);
  CHECK_NEAR(g.PixelsPerBeat(10.0), 0.0, 1e-9);
}

NOTA_TEST(MismatchedInputSizesYieldEmptyGeometry) {
  const Fixture f = MakePages(2);
  std::vector<int> wrong = {f.heights[0]};
  const nota::ScoreGeometry g =
      nota::ScoreGeometry::Build(f.pages, wrong, nota::ScoreGeometryParams());
  CHECK(g.empty());
}

NOTA_TEST(BlankPageYieldsEmptyGeometry) {
  nota::GrayImage blank(800, 1000, 255);
  const nota::PageAnalysis page = nota::AnalyzePageGray(blank.view());
  const nota::ScoreGeometry g =
      nota::ScoreGeometry::Build({page}, {1000}, nota::ScoreGeometryParams());
  CHECK(g.empty());
}
