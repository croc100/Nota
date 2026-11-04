// 시간↔픽셀 매핑. docs/DESIGN.md 6절.
//
// 여러 페이지의 분석 결과를 세로로 이어붙인 하나의 캔버스로 만들고, 악보상
// 위치(박)와 캔버스 y 좌표 사이를 양방향으로 변환한다. 스크롤 제어기가 쓰는
// 유일한 기하 정보다.
//
// 핵심 결정 — 시스템 단위로 선형 보간한다.
//
// 시스템 안에서 음악은 가로로 진행하지만 스크롤은 세로로 움직인다. 연주 위치를
// 그대로 좌표로 옮기면 한 시스템을 연주하는 내내 y가 멈춰 있다가 다음 시스템에서
// 한 번에 뛴다. 멈춤과 점프의 반복은 연주를 방해한다.
//
// 대신 한 시스템을 연주하는 동안 y가 시스템 하나 높이만큼 **연속적으로** 흐르게
// 한다. 그러면 현재 연주 중인 시스템은 읽기 선에서 시작해 그 시스템을 마칠 때쯤
// 한 칸 위로 올라가 있다. 읽기 영역이 두 시스템 높이면 연주 내내 화면에 남는다.
//
// 이 설계에서 마디는 시스템 안의 보조 눈금일 뿐이다. 마디선이 과검출되어도
// 매핑의 단조성은 깨지지 않는다 (DESIGN.md 6절).
#pragma once

#include <cstddef>
#include <vector>

#include "nota/page_analysis.hpp"

namespace nota {

struct ScoreGeometryParams {
  // 한 마디의 박 수. 박자표에서 온다 (4/4 → 4). 사용자가 지정하거나 첫
  // 시스템에서 인식한다.
  double beats_per_measure = 4.0;

  // 페이지를 이어붙일 때 페이지 사이에 두는 간격(px). 0이면 딱 붙인다.
  double page_gap = 0.0;

  // 곡 전체 길이(박). 0이면 검출된 마디 수 × beats_per_measure로 유도한다.
  //
  // 이 필드가 필요한 이유 — 검출된 마디 수로 길이를 유도하면 마디선 과검출이
  // **축척 오차**가 된다. 실측(Chopin Op.6-2 2페이지): 실제 73마디인데 89개를
  // 검출한다. 그러면 박 축이 89/73 = 1.22배로 늘어나고, 오디오가 정확히 "219박
  // 지났다"고 알려줘도 매핑은 267박 중 219박 지점을 가리킨다. 곡이 진행될수록
  // 지연이 누적되어 끝에서 2시스템까지 벌어졌다 (ALGORITHM.md 8.4).
  //
  // 단조성은 깨지지 않는다는 DESIGN.md 6절의 주장은 맞다. 다만 단조성만으로는
  // 부족했다. 축척은 분포와 따로 다뤄야 한다.
  //
  // 길이는 밖에서 싸게 얻을 수 있다 — 사용자가 총 마디 수를 넣거나(악보에 적혀
  // 있다), 템포 × 연주 시간으로 계산한다. 그 값을 여기에 주면 과검출은 눈금의
  // 간격만 거칠게 만들고 축척은 건드리지 못한다.
  double total_beats_override = 0.0;

  // 시스템에 악보상 시간을 배분하는 기준.
  enum class TimeWeight {
    // 마디 수에 비례. 박자표가 고정이면 마디는 모두 같은 길이이므로 원리상
    // 옳다. 다만 마디선 과검출이 그대로 반영된다 (정밀도 0.857 → 해당 시스템에
    // 시간이 과하게 배분되어 그만큼 느리게 흐른다).
    kMeasureCount,
    // 잉크 밀도에 비례. 과검출에 영향받지 않는다 — 경계를 몇 개 긋든 시스템
    // 안의 잉크 총량은 같기 때문이다. 다만 잉크는 음표 "수"에 비례하지 길이에
    // 비례하지 않으므로, 텍스처가 급변하는 악보에서는 어긋난다.
    kInkDensity,
  };
  TimeWeight time_weight = TimeWeight::kMeasureCount;
};

// 이어붙인 캔버스 좌표계에서의 시스템 하나.
struct GeometrySystem {
  int page_index = 0;
  int index_in_page = 0;
  double canvas_y_top = 0.0;
  double canvas_y_bottom = 0.0;
  double x_start = 0.0;
  double x_end = 0.0;
  double staff_space = 0.0;
  int measure_count = 0;
  long long ink_density = 0;
  // 이 시스템이 담당하는 악보상 시간 구간 (박).
  double beat_start = 0.0;
  double beat_end = 0.0;
};

// 캔버스 좌표계에서의 마디 하나. 시스템 안의 보조 눈금이다.
struct GeometryMeasure {
  int index = 0;         // 곡 전체 순번
  int system_index = 0;  // GeometrySystem 인덱스
  double x_start = 0.0;
  double x_end = 0.0;
  double canvas_y_top = 0.0;
  double canvas_y_bottom = 0.0;
  double beat_start = 0.0;
  double beat_end = 0.0;
  int ink_density = 0;
};

class ScoreGeometry {
 public:
  // 페이지별 분석 결과와 각 페이지의 픽셀 높이로 캔버스를 만든다.
  // 두 벡터의 길이가 다르면 빈 결과를 돌려준다.
  static ScoreGeometry Build(const std::vector<PageAnalysis>& pages,
                             const std::vector<int>& page_heights,
                             const ScoreGeometryParams& params = {});

  bool empty() const { return systems_.empty(); }
  double total_beats() const { return total_beats_; }
  double canvas_height() const { return canvas_height_; }

  const std::vector<GeometrySystem>& systems() const { return systems_; }
  const std::vector<GeometryMeasure>& measures() const { return measures_; }

  // 악보상 위치(박) → 캔버스 y. 단조 증가하며, 범위 밖은 양 끝으로 고정된다.
  double CanvasY(double beat) const;

  // 캔버스 y → 악보상 위치(박). CanvasY의 역함수다.
  // 사용자가 손으로 스크롤한 뒤 그 위치를 현재 위치로 받아들일 때 쓴다
  // (DESIGN.md 2.4).
  double BeatAt(double canvas_y) const;

  // 해당 지점에서의 전진 속도(픽셀/박). 스크롤 속도 피드포워드에 쓴다
  // (ALGORITHM.md 7.3). 시스템마다 다르므로 상수가 아니다.
  double PixelsPerBeat(double beat) const;

  // 해당 박이 속한 시스템 인덱스. 범위 밖이면 양 끝 시스템.
  int SystemAt(double beat) const;

 private:
  // 보간의 기준점. 시스템 i의 시작 y가 anchor_[i]이고, 마지막 원소는 곡의 끝
  // y다. 따라서 길이는 systems_.size() + 1이다.
  std::vector<double> anchors_;
  std::vector<GeometrySystem> systems_;
  std::vector<GeometryMeasure> measures_;
  double total_beats_ = 0.0;
  double canvas_height_ = 0.0;
};

}  // namespace nota
