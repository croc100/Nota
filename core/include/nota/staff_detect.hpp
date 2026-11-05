// 오선 검출. docs/ALGORITHM.md 5.2절, 검증 항목 V1.
//
// 음표를 인식하지 않는다(OMR 아님). 악보의 구조 — 오선, 대보표, 마디선 — 만
// 찾는다. 오선은 페이지 폭 전체에 걸친 검은 수평선이므로 행별 잉크 투영에서
// 뚜렷한 피크로 나타난다. 음표 머리나 기둥은 폭이 좁아 같은 수준의 투영값을
// 만들지 못하므로 자연히 걸러진다.
#pragma once

#include <vector>

#include "nota/bitmap.hpp"

namespace nota {

struct StaffDetectParams {
  // 오선 판정 임계. 행 투영의 99퍼센타일 대비 비율.
  // 상대값을 쓰는 이유: 악보를 잘라낸 정도와 여백에 따라 유효 폭이 달라지므로
  // 절대 픽셀 수로는 일반화되지 않는다.
  double row_peak_fraction = 0.5;
  // 빈 페이지에서 잡음이 오선으로 잡히는 것을 막는 하한 (이미지 폭 대비).
  double min_row_fraction = 0.20;
  // 오선 두께 상한. **오선 간격 대비 비율**이다.
  //
  // 절대 픽셀로 두면 해상도에 따라 깨진다. 실측: 200 DPI에서 오선 런은 1~2 px
  // 이지만 400 DPI에서는 중앙 2 px, 최대 10 px이 된다. 상한이 8이던 시절
  // 400 DPI에서 런 하나가 버려져 수평선이 70개에서 69개가 되었고, 5선 묶음
  // 하나가 깨져 오선이 14개에서 13개로 줄었다.
  //
  // 오선 간격은 검출 후에야 알 수 있으므로 2단계로 추정한다. 먼저 두께 제한
  // 없이 런을 모아 중심 간 간격의 중위수로 오선 간격을 잡고, 그 다음 이 비율로
  // 다시 거른다. 한 페이지의 연속 간격은 대부분 오선 내부 간격이므로
  // (14개 오선 기준 56:13) 중위수가 곧 오선 간격이다.
  double max_line_thickness_ratio = 0.6;
  // 비율이 너무 작아지는 저해상도를 위한 절대 하한(픽셀).
  int min_line_thickness_limit = 3;

  // 쪼개진 오선 하나를 다시 합치는 간격 상한. 오선 간격 대비 비율이다.
  //
  // 투영값이 선 중간에서 임계 아래로 잠깐 내려가면 한 오선이 두 개의 런으로
  // 검출된다. 그러면 5선 묶기가 6개를 보게 되어 그 오선 전체가 깨진다.
  //
  // 실측 — 쪼개진 짝의 간격은 오선 간격의 0.19~0.21배였고, 진짜 오선 사이
  // 간격은 1.0배다. 둘은 명확히 분리된다.
  //   오선간격 36.0  쪼개짐 7.00 px (0.19배)
  //   오선간격 11.9  쪼개짐 2.50 px (0.21배)
  double merge_split_line_ratio = 0.4;
  // 5선 간격의 일관성 허용 오차. 중위 간격 대비 비율.
  double spacing_tolerance = 0.30;
  // 오선 간격 하한(픽셀). 이보다 좁으면 같은 선이 쪼개진 것으로 본다.
  double min_staff_space = 4.0;
};

// 검출된 수평선 하나.
struct StaffLine {
  double y_center = 0.0;
  int thickness = 0;
  int ink_count = 0;  // 해당 런에서 가장 잉크가 많았던 행의 값
};

// 5선 한 묶음.
struct Staff {
  double line_y[5] = {0, 0, 0, 0, 0};
  double staff_space = 0.0;  // 선 간 평균 간격
  double y_top = 0.0;        // 첫 선
  double y_bottom = 0.0;     // 마지막 선
};

struct StaffDetectResult {
  std::vector<StaffLine> lines;  // 5선 묶음을 이루지 못한 선도 포함
  std::vector<Staff> staves;
  double median_staff_space = 0.0;
  // 디버깅·시각화용. prototype/ 의 Python이 이걸 플롯한다.
  std::vector<int> row_projection;
  int row_threshold = 0;
};

StaffDetectResult DetectStaves(const BinaryImage& image,
                               const StaffDetectParams& params = {});

}  // namespace nota
