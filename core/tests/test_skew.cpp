#include <cmath>
#include <cstddef>

#include "nota/binarize.hpp"
#include "nota/page_analysis.hpp"
#include "nota/skew.hpp"
#include "synth_score.hpp"
#include "test_framework.hpp"

NOTA_TEST(EstimatesZeroSkewOnStraightScore) {
  nota_test::SynthParams p;
  p.systems = 3;
  const nota::BinaryImage bin =
      nota::BinarizeSauvola(nota_test::GenerateScore(p).image.view());
  CHECK_NEAR(nota::EstimateSkewDegrees(bin), 0.0, 0.05);
}

NOTA_TEST(RecoversKnownSkewAngles) {
  nota_test::SynthParams p;
  p.systems = 3;
  const nota::GrayImage clean = nota_test::GenerateScore(p).image;

  for (const double angle : {-2.5, -1.0, -0.4, 0.4, 1.0, 2.5}) {
    // 이미지를 angle 만큼 기울인다. RotateGray의 부호 규약상 +angle로 돌린
    // 이미지는 EstimateSkew가 +angle을 돌려줘야 한다.
    const nota::GrayImage skewed = nota::RotateGray(clean.view(), -angle);
    const nota::BinaryImage bin = nota::BinarizeSauvola(skewed.view());
    const double est = nota::EstimateSkewDegrees(bin);
    CHECK_NEAR(est, angle, 0.10);
  }
}

NOTA_TEST(DeskewRestoresStaffDetection) {
  nota_test::SynthParams p;
  p.systems = 3;
  const nota_test::SynthScore score = nota_test::GenerateScore(p);

  for (const double angle : {0.5, 1.2, 2.5}) {
    const nota::GrayImage skewed = nota::RotateGray(score.image.view(), -angle);
    const nota::PageAnalysis page = nota::AnalyzePageGray(skewed.view());

    CHECK_EQ(page.staves.staves.size(),
             static_cast<std::size_t>(score.staff_count));
    CHECK_EQ(page.systems.size(), static_cast<std::size_t>(score.system_count));
    CHECK_NEAR(page.deskew_angle_deg, angle, 0.15);
  }
}

NOTA_TEST(WithoutDeskewDetectionCollapsesOnRealisticPage) {
  // 기울기 민감도는 오선의 가로 길이에 비례한다. 길이 L, 각도 t에서 투영이
  // 번지는 양은 L*tan(t)이고, 이것이 오선 간격을 넘으면 피크가 사라진다.
  //
  // 기본 합성 악보는 오선 길이가 840 px라 0.5도에서 번짐이 7.3 px로 오선
  // 간격 12 px보다 작아 아직 버틴다. 실제 A4 200 DPI 악보는 오선 길이가
  // 1500 px에 가까우므로 같은 각도에서 번짐이 13 px가 되어 무너진다.
  // 이 테스트는 그 실제 비율을 재현한다.
  nota_test::SynthParams p;
  p.width = 1654;
  p.margin_x = 80;
  p.staff_space = 14.0;
  p.intra_system_gap = 56.0;
  p.inter_system_gap = 150.0;
  p.systems = 3;
  const nota_test::SynthScore score = nota_test::GenerateScore(p);

  const nota::GrayImage skewed = nota::RotateGray(score.image.view(), -1.5);

  nota::PageAnalysisParams off;
  off.deskew = false;
  const nota::PageAnalysis without = nota::AnalyzePageGray(skewed.view(), off);
  const nota::PageAnalysis with = nota::AnalyzePageGray(skewed.view());

  // 보정 없이는 검출이 무너지고, 보정하면 온전히 돌아와야 한다.
  CHECK(without.staves.staves.size() < static_cast<std::size_t>(score.staff_count));
  CHECK_EQ(with.staves.staves.size(), static_cast<std::size_t>(score.staff_count));
  CHECK_EQ(with.systems.size(), static_cast<std::size_t>(score.system_count));
}

NOTA_TEST(DeskewIsHarmlessOnStraightInput) {
  nota_test::SynthParams p;
  p.systems = 3;
  const nota_test::SynthScore score = nota_test::GenerateScore(p);
  const nota::PageAnalysis page = nota::AnalyzePageGray(score.image.view());

  CHECK_EQ(page.staves.staves.size(), static_cast<std::size_t>(score.staff_count));
  CHECK_EQ(page.systems.size(), static_cast<std::size_t>(score.system_count));
  CHECK_NEAR(page.deskew_angle_deg, 0.0, 0.05);
}

NOTA_TEST(RotateBinaryPreservesInkRoughly) {
  nota_test::SynthParams p;
  p.systems = 2;
  const nota::BinaryImage bin =
      nota::BinarizeSauvola(nota_test::GenerateScore(p).image.view());
  const nota::BinaryImage rot = nota::RotateBinary(bin, 1.0);

  int a = 0;
  int b = 0;
  for (std::size_t i = 0; i < bin.ink.size(); ++i) a += bin.ink[i];
  for (std::size_t i = 0; i < rot.ink.size(); ++i) b += rot.ink[i];
  // 회전으로 잉크가 크게 늘거나 줄면 안 된다 (가장자리 잘림은 허용).
  CHECK(b > a * 0.85);
  CHECK(b < a * 1.15);
}

NOTA_TEST(EmptyImageSkewIsZero) {
  const nota::BinaryImage empty;
  CHECK_NEAR(nota::EstimateSkewDegrees(empty), 0.0, 1e-9);
}
