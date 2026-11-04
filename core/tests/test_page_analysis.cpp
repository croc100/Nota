#include <algorithm>
#include <cstddef>
#include <vector>

#include "nota/binarize.hpp"
#include "nota/page_analysis.hpp"
#include "synth_score.hpp"
#include "test_framework.hpp"

namespace {

nota::PageAnalysis Analyze(const nota::GrayImage& img) {
  return nota::AnalyzePage(nota::BinarizeSauvola(img.view()));
}

}  // namespace

NOTA_TEST(StavesAreGroupedIntoGrandStaves) {
  nota_test::SynthParams params;
  params.systems = 4;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);
  const nota::PageAnalysis page = Analyze(score.image);

  CHECK_EQ(page.systems.size(), static_cast<std::size_t>(score.system_count));
  // 피아노 악보이므로 모든 시스템이 오선 2개여야 한다.
  for (const nota::System& sys : page.systems) {
    CHECK_EQ(sys.staff_count, 2);
  }
}

NOTA_TEST(SystemHorizontalExtentMatchesStaffLines) {
  nota_test::SynthParams params;
  params.systems = 2;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);
  const nota::PageAnalysis page = Analyze(score.image);

  CHECK(!page.systems.empty());
  for (const nota::System& sys : page.systems) {
    // 페이지 폭이 아니라 오선이 실제로 그려진 범위를 잡아야 한다.
    CHECK_NEAR(sys.x_start, score.x_start, 1.5);
    CHECK_NEAR(sys.x_end, score.x_end, 1.5);
  }
}

NOTA_TEST(BarlinePositionsMatchGroundTruth) {
  nota_test::SynthParams params;
  params.systems = 3;
  params.barlines_per_system = 4;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);
  const nota::PageAnalysis page = Analyze(score.image);

  CHECK_EQ(page.barlines.size(), score.barline_x.size());

  std::vector<double> detected;
  detected.reserve(page.barlines.size());
  for (const nota::Barline& bar : page.barlines) detected.push_back(bar.x);

  // 시스템별로 좌→우 정렬되어 나오므로 생성 순서와 같다.
  for (std::size_t i = 0; i < detected.size(); ++i) {
    CHECK_NEAR(detected[i], score.barline_x[i], 1.0);
  }
}

NOTA_TEST(MeasureCountMatchesBarlineLayout) {
  // 마디선 4개 중 마지막은 시스템 끝과 겹치므로 시스템당 마디는 4개다.
  nota_test::SynthParams params;
  params.systems = 3;
  params.barlines_per_system = 4;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);
  const nota::PageAnalysis page = Analyze(score.image);

  CHECK_EQ(page.measures.size(), static_cast<std::size_t>(params.systems * 4));

  // 마디는 시스템 전체 폭을 빈틈없이 덮어야 한다.
  for (std::size_t si = 0; si < page.systems.size(); ++si) {
    std::vector<const nota::Measure*> in_system;
    for (const nota::Measure& m : page.measures) {
      if (m.system_index == static_cast<int>(si)) in_system.push_back(&m);
    }
    CHECK(!in_system.empty());
    CHECK_NEAR(in_system.front()->x_start, page.systems[si].x_start, 0.01);
    CHECK_NEAR(in_system.back()->x_end, page.systems[si].x_end, 0.01);
    for (std::size_t k = 0; k + 1 < in_system.size(); ++k) {
      CHECK_NEAR(in_system[k]->x_end, in_system[k + 1]->x_start, 0.01);
    }
  }
}

NOTA_TEST(MeasureIndicesAreSequential) {
  nota_test::SynthParams params;
  params.systems = 3;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);
  const nota::PageAnalysis page = Analyze(score.image);

  for (std::size_t i = 0; i < page.measures.size(); ++i) {
    CHECK_EQ(page.measures[i].index, static_cast<int>(i));
  }
}

NOTA_TEST(InkDensityRespondsToNoteCount) {
  // 잉크 밀도는 음표 수의 대리 지표다. 음표가 많으면 커져야 한다.
  nota_test::SynthParams sparse;
  sparse.systems = 1;
  sparse.noteheads_per_staff = 2;

  nota_test::SynthParams dense = sparse;
  dense.noteheads_per_staff = 24;

  const nota::PageAnalysis a = Analyze(nota_test::GenerateScore(sparse).image);
  const nota::PageAnalysis b = Analyze(nota_test::GenerateScore(dense).image);

  CHECK(!a.measures.empty());
  CHECK(!b.measures.empty());

  int sum_a = 0;
  int sum_b = 0;
  for (const nota::Measure& m : a.measures) sum_a += m.ink_density;
  for (const nota::Measure& m : b.measures) sum_b += m.ink_density;
  CHECK(sum_b > sum_a * 2);
}

NOTA_TEST(BarlinesAreNotConfusedWithStems) {
  // 음표 기둥은 시스템 높이의 일부만 차지하므로 마디선이 아니다.
  nota_test::SynthParams params;
  params.systems = 2;
  params.noteheads_per_staff = 20;  // 기둥이 많아진다
  params.barlines_per_system = 4;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);
  const nota::PageAnalysis page = Analyze(score.image);

  CHECK_EQ(page.barlines.size(), score.barline_x.size());
}

NOTA_TEST(BlankPageYieldsNoSystems) {
  nota::GrayImage blank(800, 1000, 255);
  const nota::PageAnalysis page = Analyze(blank);
  CHECK_EQ(page.systems.size(), static_cast<std::size_t>(0));
  CHECK_EQ(page.measures.size(), static_cast<std::size_t>(0));
}
