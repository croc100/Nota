#include <algorithm>
#include <cstddef>
#include <vector>

#include "nota/binarize.hpp"
#include "nota/staff_detect.hpp"
#include "synth_score.hpp"
#include "test_framework.hpp"

namespace {

nota::BinaryImage Binarize(const nota::GrayImage& img) {
  return nota::BinarizeSauvola(img.view());
}

}  // namespace

NOTA_TEST(DetectsAllStavesInSyntheticScore) {
  nota_test::SynthParams params;
  params.systems = 4;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);

  const nota::BinaryImage bin = Binarize(score.image);
  const nota::StaffDetectResult result = nota::DetectStaves(bin);

  CHECK_EQ(result.staves.size(), static_cast<std::size_t>(score.staff_count));
  CHECK_NEAR(result.median_staff_space, params.staff_space, 0.6);
}

NOTA_TEST(DetectedLinePositionsMatchGroundTruth) {
  nota_test::SynthParams params;
  params.systems = 3;
  const nota_test::SynthScore score = nota_test::GenerateScore(params);

  const nota::BinaryImage bin = Binarize(score.image);
  const nota::StaffDetectResult result = nota::DetectStaves(bin);

  CHECK_EQ(result.staves.size(), static_cast<std::size_t>(score.staff_count));

  // 검출된 5선 묶음의 선 위치가 정답과 일치하는지 본다.
  std::vector<double> detected;
  for (const nota::Staff& staff : result.staves) {
    for (int k = 0; k < 5; ++k) detected.push_back(staff.line_y[k]);
  }
  CHECK_EQ(detected.size(), score.line_centers.size());
  for (std::size_t i = 0; i < detected.size(); ++i) {
    CHECK_NEAR(detected[i], score.line_centers[i], 1.0);
  }
}

NOTA_TEST(ScaleInvariance) {
  // 조판 크기가 달라도 동작해야 한다. 절대 픽셀 임계를 쓰지 않는지 확인한다.
  for (const double space : {8.0, 12.0, 20.0}) {
    nota_test::SynthParams params;
    params.systems = 2;
    params.staff_space = space;
    params.intra_system_gap = 3.5 * space;
    params.inter_system_gap = 10.0 * space;
    const nota_test::SynthScore score = nota_test::GenerateScore(params);

    const nota::BinaryImage bin = Binarize(score.image);
    const nota::StaffDetectResult result = nota::DetectStaves(bin);

    CHECK_EQ(result.staves.size(), static_cast<std::size_t>(score.staff_count));
    CHECK_NEAR(result.median_staff_space, space, 0.6);
  }
}

NOTA_TEST(NoteheadsDoNotCreateFalseStaves) {
  // 음표 머리와 기둥이 오선으로 오검출되지 않아야 한다.
  nota_test::SynthParams with_notes;
  with_notes.systems = 2;
  with_notes.draw_noteheads = true;
  with_notes.noteheads_per_staff = 16;

  nota_test::SynthParams without_notes = with_notes;
  without_notes.draw_noteheads = false;

  const nota::StaffDetectResult a =
      nota::DetectStaves(Binarize(nota_test::GenerateScore(with_notes).image));
  const nota::StaffDetectResult b =
      nota::DetectStaves(Binarize(nota_test::GenerateScore(without_notes).image));

  CHECK_EQ(a.staves.size(), b.staves.size());
}

NOTA_TEST(BlankPageYieldsNoStaves) {
  nota::GrayImage blank(800, 1000, 255);
  const nota::BinaryImage bin = Binarize(blank);
  const nota::StaffDetectResult result = nota::DetectStaves(bin);
  CHECK_EQ(result.staves.size(), static_cast<std::size_t>(0));
}

NOTA_TEST(EmptyImageIsHandled) {
  const nota::BinaryImage empty;
  const nota::StaffDetectResult result = nota::DetectStaves(empty);
  CHECK_EQ(result.staves.size(), static_cast<std::size_t>(0));
  CHECK_EQ(result.lines.size(), static_cast<std::size_t>(0));
}
