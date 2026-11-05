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
  //
  // 범위는 실제로 겪는 폭이다. 200 DPI A4에서 오선 간격은 약 14 px이고,
  // 150 DPI에서 10 px, 500 DPI에서 35 px가 된다. 판형이 크면 같은 해상도에서도
  // 작아진다.
  for (const double space : {8.0, 12.0, 20.0, 28.0, 36.0}) {
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

NOTA_TEST(SplitStaffLineIsMergedBack) {
  // 투영이 선 중간에서 임계 아래로 내려가면 오선 하나가 두 런으로 검출된다.
  // 그러면 5선 묶기가 6개를 보게 되어 그 오선 전체가 깨진다.
  //
  // 실측에서 조판 크기 양 끝(오선 간격 11.9 px, 36 px)에서 실제로 발생했고,
  // 쪼개진 짝의 간격은 오선 간격의 0.19~0.21배였다.
  nota_test::SynthParams p;
  p.systems = 2;
  p.split_one_staff_line = true;
  const nota_test::SynthScore score = nota_test::GenerateScore(p);
  const nota::BinaryImage bin = Binarize(score.image);

  // 병합을 끄면 그 오선이 깨진다.
  nota::StaffDetectParams off;
  off.merge_split_line_ratio = 0.0;
  const nota::StaffDetectResult without = nota::DetectStaves(bin, off);
  CHECK(without.staves.size() <
        static_cast<std::size_t>(score.staff_count));

  // 기본값(병합 켬)이면 전부 찾아야 한다.
  const nota::StaffDetectResult with = nota::DetectStaves(bin);
  CHECK_EQ(with.staves.size(), static_cast<std::size_t>(score.staff_count));
}

NOTA_TEST(PositionInvariance) {
  // 악보가 페이지 안 어디에 있든 결과가 같아야 한다.
  nota_test::SynthParams p;
  p.systems = 3;
  const nota_test::SynthScore score = nota_test::GenerateScore(p);
  const nota::StaffDetectResult base = nota::DetectStaves(Binarize(score.image));

  for (const int shift : {-40, 60, 150}) {
    // 캔버스를 넓혀 내용 손실 없이 옮긴다.
    const int pad = 200;
    nota::GrayImage moved(score.image.width + 2 * pad,
                          score.image.height + 2 * pad, 255);
    for (int y = 0; y < score.image.height; ++y) {
      for (int x = 0; x < score.image.width; ++x) {
        moved.at(x + pad + shift, y + pad + shift) = score.image.at(x, y);
      }
    }
    const nota::StaffDetectResult r = nota::DetectStaves(Binarize(moved));
    CHECK_EQ(r.staves.size(), base.staves.size());
    CHECK_NEAR(r.median_staff_space, base.median_staff_space, 0.2);
    for (std::size_t i = 0; i < r.staves.size(); ++i) {
      CHECK_NEAR(r.staves[i].y_top - static_cast<double>(pad + shift),
                 base.staves[i].y_top, 1.0);
    }
  }
}
