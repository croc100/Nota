// 악보 한 페이지의 구조 분석. docs/ALGORITHM.md 5.3~5.5절, 검증 항목 V2.
//
// 오선 검출(staff_detect.hpp) 위에 세 단계를 더 쌓아 시간↔픽셀 매핑의 재료를
// 만든다:
//   1. 대보표 묶기  — 피아노 악보의 오선은 2개씩 짝을 이뤄 한 시스템이 된다
//   2. 마디선 검출  — 시스템 높이를 관통하는 수직선
//   3. 마디 상자    — 마디선 사이 구간. 이것이 매핑의 최소 단위다
//
// 여전히 음표를 인식하지 않는다. 마디에 음이 얼마나 빽빽한지는 잉크 밀도라는
// 값싼 대리 지표로 근사한다 (재동기화에 쓴다, ALGORITHM.md 6.2).
#pragma once

#include <vector>

#include "nota/binarize.hpp"
#include "nota/bitmap.hpp"
#include "nota/skew.hpp"
#include "nota/staff_detect.hpp"

namespace nota {

struct SystemDetectParams {
  // 두 오선이 한 시스템인지는 "시작부에 둘을 잇는 수직선이 있는가"로 판정한다.
  // 이 선이 대보표를 대보표이게 하는 것이므로, 간접 지표가 아니라 정의 자체다.
  //
  // 처음에는 오선 간 수직 간격을 통계로 나눠 판정했다. 실패한다. Chopin
  // 마주르카 1페이지에서 첫 시스템의 대보표 내부 간격은 123.2 px인데 시스템
  // 사이 간격은 113.4~137.3 px로, 두 분포가 겹친다(첫 시스템만 제목·나타냄말
  // 때문에 넓다). 간격만 보면 첫 대보표가 두 개로 쪼개진다.
  //
  // 연결선으로 보면 분리가 명확하다. 실측(Bach BWV846, Chopin Op.6-2):
  //   같은 시스템  1.004 ~ 1.005
  //   다른 시스템  0.351 ~ 0.542
  double connector_height_fraction = 0.85;
  // 연결선을 찾을 x 범위 (오선 간격 대비, 오선 좌측 끝 기준).
  double connector_search_before = 2.0;
  double connector_search_after = 3.0;

  // 연결선이 하나도 없는 악보(중괄호만 그리고 직선은 생략하는 판본)를 위한
  // 폴백. 간격 통계로 되돌아간다.
  double fallback_pair_ratio = 3.0;
};

struct BarlineDetectParams {
  // 마디선 판정 기준: "각 오선 안에서 오선 높이를 꽉 채우는 수직선이, 그 시스템의
  // 모든 오선에 같은 x로 동시에 존재한다."
  //
  // 처음에는 시스템 전체 높이 대비 비율 하나로 판정했다. 조판 양식에 따라
  // 무너진다. 실측:
  //   Bach BWV846 (Verovio)   마디선이 대보표를 관통        비율 0.99
  //   Chopin Op.6-2 (Verovio) 마디선이 오선마다 끊김        비율 0.575
  // 두 양식을 하나의 전역 임계로 가를 수 없다. 0.85로 두면 Chopin의 마디선을
  // 전부 놓치고, 0.55로 낮추면 Bach에서 음표 기둥(0.72)이 쏟아져 들어온다.
  //
  // 오선별로 보면 두 양식이 같아진다. 어느 쪽이든 마디선은 각 오선을 위에서
  // 아래까지 긋는다. 반대로 음표 기둥은 한 오선 안에서도 일부만 차지하고,
  // 두 오선에 동시에 같은 x로 나타나는 일은 드물다.
  double staff_height_fraction = 0.90;

  // 마디선 두께 상한(픽셀). 겹세로줄·종지선을 수용한다.
  int max_thickness = 14;

  // 가까이 붙은 선들을 하나로 합치는 간격 (오선 간격 대비). 겹세로줄, 반복
  // 기호, 종지선은 여러 수직선으로 그려지므로 묶어야 마디 수가 맞는다.
  double min_spacing_ratio = 1.5;

  // 시스템 시작부의 연결선을 마디선으로 세지 않기 위한 여백 (오선 간격 대비).
  // 대보표 시작에는 두 오선을 잇는 수직선이 있고, 이는 모든 오선을 관통하므로
  // 위 기준을 그대로 통과한다. 그 앞에는 마디가 없으므로 위치로 걸러낸다.
  double system_start_margin_ratio = 2.0;
};

struct PageAnalysisParams {
  SauvolaParams sauvola;
  SkewParams skew;
  StaffDetectParams staff;
  SystemDetectParams system;
  BarlineDetectParams barline;
  // 기울기 보정. 끌 이유는 거의 없다 — 0.5도만 기울어도 오선 검출이 완전히
  // 무너진다(skew.hpp). 이미 보정된 입력에서만 끈다.
  bool deskew = true;
};

// 대보표 하나. 피아노에서는 오선 2개지만, 한 개짜리도 허용한다(표지, 단선 악보).
struct System {
  int first_staff = 0;  // StaffDetectResult::staves 인덱스
  int staff_count = 0;
  double y_top = 0.0;     // 첫 오선의 첫 선
  double y_bottom = 0.0;  // 마지막 오선의 마지막 선
  double x_start = 0.0;   // 오선이 실제로 그려진 좌측 끝
  double x_end = 0.0;
  double staff_space = 0.0;
};

struct Barline {
  double x = 0.0;
  int thickness = 0;
  int system_index = 0;
};

// 마디 하나. 시간↔픽셀 매핑의 최소 단위다.
struct Measure {
  int index = 0;         // 페이지 내 순번 (좌→우, 위→아래)
  int system_index = 0;
  double x_start = 0.0;
  double x_end = 0.0;
  double y_top = 0.0;
  double y_bottom = 0.0;
  // 오선 획을 제외한 잉크 픽셀 수. 음표 수의 대리 지표이며, 재동기화에서
  // "이 마디에 음이 많은가 적은가"를 판단하는 데 쓴다. 음표 인식이 아니다.
  int ink_density = 0;
};

// 검출 결과를 스스로 얼마나 믿을 수 있는지에 대한 평가.
//
// 조용히 틀린 답을 내놓는 것이 깨끗하게 실패하는 것보다 나쁘다. 심한 흐림에서
// 검출기는 오선을 전부 못 찾거나(명백한 실패), 일부만 찾아 시스템 구조를
// 잘못 만든다(조용한 실패). 후자를 호출자가 알아챌 수 있어야 등급 C(사용자
// 캘리브레이션)로 넘어갈 수 있다.
//
// 피아노 악보에서 모든 시스템은 같은 수의 오선을 가진다(보통 2). 이 규칙이
// 깨지면 검출이 틀린 것이다. 음악적 사전지식으로 얻는 값싼 검사다.
struct PageQuality {
  int dominant_staff_count = 0;   // 가장 흔한 시스템당 오선 수
  int consistent_systems = 0;     // 그 수를 따르는 시스템 개수
  int total_systems = 0;

  // 모든 시스템이 같은 구조를 가지는가. 거짓이면 매핑을 신뢰하면 안 된다.
  bool is_consistent() const {
    return total_systems > 0 && consistent_systems == total_systems;
  }
  double consistency() const {
    return total_systems > 0
               ? static_cast<double>(consistent_systems) /
                     static_cast<double>(total_systems)
               : 0.0;
  }
};

struct PageAnalysis {
  PageQuality quality;
  // 적용한 보정 각도(도). 좌표는 모두 보정 후 기준이므로, 원본 이미지 위에
  // 그리려면 호출자가 이 각도로 역변환하거나 화면용 이미지도 같이 회전해야
  // 한다.
  double deskew_angle_deg = 0.0;
  StaffDetectResult staves;
  std::vector<System> systems;
  std::vector<Barline> barlines;
  std::vector<Measure> measures;
};

// 이진 이미지에서 바로 분석한다. 기울기 보정을 하지 않는다.
PageAnalysis AnalyzePage(const BinaryImage& image,
                         const PageAnalysisParams& params = {});

// 회색조에서 시작하는 전체 경로. 권장 진입점이다.
//
// 이진화 → 기울기 추정 → 회색조 회전 → 재이진화 → 분석.
// 이진 이미지를 회전시키지 않고 회색조로 되돌아가는 이유는, 이진 영상의
// 최근접 회전이 2~3 px 두께의 오선을 끊어 놓기 때문이다. 회색조는 쌍선형
// 보간이 가능하고 이후 Sauvola가 다시 임계를 잡으므로 획이 보존된다.
PageAnalysis AnalyzePageGray(const GrayView& image,
                             const PageAnalysisParams& params = {});

}  // namespace nota
