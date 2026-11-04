// 위치 추정. docs/ALGORITHM.md 6절.
//
// 악보상 어디를 연주하고 있는지를 추정한다. 상태는 위치 `p`(박)와 템포 `T`(BPM)
// 두 개뿐이다.
//
// 구조:
//   온셋 검출 ─(최근 2초의 온셋 수)─┐
//   템포 추정 ─(BPM)───────────────┤
//   정지 감지 ─(연주 중인가)────────┴─> PositionEstimator ─(박)─> ScrollController
//                                             │
//                                        ScoreGeometry (마디 잉크)
//
// 입력이 숫자뿐이므로 온셋 검출기 없이 전부 테스트된다. 오디오가 와야 하는 것은
// 이 모듈의 **입력 품질**이고(V3/V4), 이 모듈의 **논리**는 지금 측정된다.
//
// 왜 적분만으로는 안 되는가. 템포를 적분하면 오차가 누적된다. 템포 추정이 1%
// 틀리면 3분 연주 후 1.8초, 90 BPM에서 2.7박이 어긋난다. 한 시스템이 16박인
// 악보에서는 아직 괜찮지만, 2% 틀리면 5.4박으로 한 시스템의 1/3이다. 그래서
// 주기적으로 독립 추정을 섞는다 — 상보 필터다. 적분은 단기적으로 정확하고
// 장기적으로 드리프트하며, 밀도 매칭은 단기적으로 거칠고 장기적으로 안정하다.
#pragma once

#include "nota/score_geometry.hpp"

namespace nota {

struct PositionEstimatorParams {
  // 최근 온셋을 세는 창(초). 호출자가 이 길이로 센 수를 넣어야 한다.
  double resync_window_seconds = 2.0;

  // 재동기화 주기(초). 매 프레임 할 이유가 없다 — 창이 2초이므로 그보다 훨씬
  // 자주 해도 같은 정보를 다시 보는 것이다.
  double resync_interval_seconds = 0.5;

  // 탐색 범위(마디). ±2마디를 본다. 마디 길이는 시스템마다 다르므로 기하에서
  // 얻는다 (ScoreGeometry::BeatsPerMeasureAt).
  double search_measures = 2.0;

  // 탐색 표본 수. 범위를 이만큼으로 나눠 본다. 홀수로 두어 중앙(현재 추정)이
  // 반드시 후보에 들어가게 한다.
  int search_samples = 41;

  // 혼합 계수. p ← (1-β·conf)·p + β·conf·p*.
  //
  // 0.15는 설계값이다(6.2절). 재동기화 주기 0.5초에서 시정수는 약 3.3초가 된다 —
  // 한 번의 오추정이 위치를 끌고 가지 못하면서도 드리프트는 따라잡는 값이다.
  double blend_beta = 0.15;

  // 급변 억제(6.3). 보정 후보가 현재 추정에서 이 거리(시스템 단위) 이상
  // 떨어져 있으면 즉시 적용하지 않고, 같은 방향으로 연속
  // jump_consistency_required회 나타날 때만 허용한다.
  //
  // **등급 B에서는 이 경로가 작동하지 않는다.** 탐색 범위가 ±2마디이므로
  // argmin이 돌려줄 수 있는 최대 거리는 2마디다. 시스템이 4마디면 임계는
  // 4마디이고, 탐색 범위가 그 절반이라 임계에 닿을 수 없다. 6.2와 6.3을 같이
  // 읽으면 모순이다 — 측정하다 드러났다(ALGORITHM.md 8.5).
  //
  // 그래도 남겨 둔다. 두 가지 이유다.
  //   1. 등급 A(크로마 + 온라인 DTW)는 임의의 거리로 뛸 수 있다. 그때 필요하다.
  //   2. 탐색 범위를 넓히면 바로 필요해진다.
  // 등급 B에서는 β 혼합이 이미 같은 일을 한다 — 2마디 오추정도 한 번에 0.3마디만
  // 움직인다. 그래서 등급 B의 안전은 이 규칙이 아니라 β와 신뢰도가 맡는다.
  double jump_threshold_systems = 1.0;
  int jump_consistency_required = 3;

  // 관측 온셋 수의 상대 불확실성. 매칭의 분해 한계를 정한다.
  //
  // **이 값이 없으면 폭주한다.** 처음에는 "온셋 반 개"를 분해 한계로 썼다.
  // 실측(Bach BWV846, 템포 오차 3%)에서 박 오차가 적분만 한 4.08박보다 나쁜
  // 27.8박(2.25 시스템)이 됐다. 이유: Bach는 2초 창에 온셋이 약 50개 들어온다.
  // 거기서 반 개는 1% 정밀도이고, 온셋 **수** 하나가 그만한 정밀도를 가질 수
  // 없다. 검출 누락, 페달, 반올림이 모두 몇 개 단위로 들어온다. 결국 노이즈
  // 수준의 차이로 위치를 결정하고, 그 결정이 매번 같은 방향으로 틀리면 누적된다.
  //
  // 그래서 분해 한계를 관측량에 비례시킨다. 온셋이 50개면 ±7.5개, 20개면 ±3개가
  // 구별 불가 범위다. 이 범위가 탐색 범위를 덮으면 신뢰도가 0이 되고, 추정기는
  // 적분으로 되돌아간다 — 균일한 악보에서 일어나야 하는 일이 바로 그것이다.
  //
  // 0.15는 "온셋 검출이 15% 정도는 틀린다"는 가정이다. V3/V4 측정이 오면 실제
  // F-measure에서 다시 정해야 한다.
  double onset_count_relative_uncertainty = 0.15;

  // 잉크 → 기대 온셋 수의 환산 계수(잉크 픽셀 / 온셋 1개)의 초기값.
  //
  // **설계에 비어 있던 부분이다.** 6.2절은 "inkDensity에서 기대되는 온셋 수와
  // 비교한다"고만 적었다. 그런데 잉크 픽셀 수와 온셋 수 사이의 비례 상수는
  // 악보마다 다르다 — 조판 크기, 해상도, 음표 머리의 두께, 꼬리와 가로줄의 양에
  // 따라 달라진다. 미리 알 수 있는 값이 아니다.
  //
  // 비교가 절대량 비교이므로 이 상수가 없으면 argmin이 성립하지 않는다. 한
  // 시점에서 관측 온셋 수는 모든 후보에 대해 같고, 후보마다 다른 것은 기대값뿐이
  // 므로, 환산이 k배 틀리면 "잉크가 obs·k에 가까운 후보"가 뽑힌다.
  //
  // 그래서 **연주 중에 스스로 보정한다** — 지나온 구간의 잉크 총량을 그 사이
  // 관측한 온셋 총수로 나눈다. 아래 값은 그 추정이 자리잡기 전까지 쓰는 초기값일
  // 뿐이고, 200 dpi 조판에서 16분음표 1개가 대략 이 정도 잉크를 가진다는
  // 어림이다. 실제 녹음이 오면 이 수와 보정의 수렴 속도를 측정해야 한다.
  double initial_ink_per_onset = 120.0;
  // 보정의 평활 계수(지수이동평균). 작으면 느리고 안정하다.
  double calibration_alpha = 0.1;
  // 보정이 자리잡기까지 필요한 최소 관측 온셋 수. 그 전에는 초기값을 쓴다.
  int calibration_warmup_onsets = 20;
  // 보정값이 벗어나지 못하는 범위 (초기값 대비 배수). 한 번의 이상 관측이
  // 환산을 날려버리지 않게 한다.
  double calibration_min_ratio = 0.1;
  double calibration_max_ratio = 10.0;
};

struct PositionState {
  double beat = 0.0;
  double tempo_bpm = 0.0;

  // 이 프레임에 재동기화가 적용됐는가.
  bool resynced = false;
  // 밀도 매칭이 가리킨 위치와, 실제로 적용한 보정량(박).
  double resync_target = 0.0;
  double resync_shift = 0.0;
  // 매칭을 얼마나 믿는지 (0~1). 0이면 보정하지 않았다.
  double confidence = 0.0;

  // 큰 점프 후보가 쌓여 있는가(6.3). 참인 동안은 보정이 보류된다.
  bool jump_pending = false;
  int jump_streak = 0;

  // 현재 쓰고 있는 잉크/온셋 환산값. 보정 상태를 밖에서 볼 수 있게 한다.
  double ink_per_onset = 0.0;
};

class PositionEstimator {
 public:
  // geometry는 추정기보다 오래 살아야 한다.
  explicit PositionEstimator(const ScoreGeometry& geometry,
                             const PositionEstimatorParams& params = {});

  // 위치와 템포를 즉시 맞춘다. 보정 상태(점프 연속 횟수)도 지운다.
  // 곡을 시작할 때, 그리고 사용자가 손으로 스크롤해 위치를 인수할 때 쓴다
  // (ALGORITHM.md 7.5 → ScrollState::position_handover).
  void Reset(double beat, double tempo_bpm);

  // 위치만 받아들인다. 템포와 환산 보정은 유지한다 — 사용자가 자리를 옮긴 것이
  // 템포를 바꾼 것은 아니다.
  void SetPosition(double beat);

  // 한 프레임 전진한다.
  //   dt         경과 시간(초)
  //   tempo_bpm  템포 추정값
  //   playing    연주 중인가 (정지 감지, ALGORITHM.md 4절)
  //   onsets_in_window  최근 resync_window_seconds 동안 검출된 온셋 수.
  //                     음수면 "알 수 없음" — 재동기화를 건너뛴다.
  PositionState Update(double dt, double tempo_bpm, bool playing,
                       int onsets_in_window);

  double beat() const { return beat_; }
  double tempo_bpm() const { return tempo_bpm_; }
  double ink_per_onset() const { return ink_per_onset_; }

 private:
  // 박을 곡 범위로 묶는다. 불변식은 Update 뒤에만이 아니라 **항상** 성립해야
  // 한다 — Reset 직후 비정상 dt로 Update가 조기 반환하면 범위 밖 값이 그대로
  // 나왔다. 퍼징이 잡은 버그다.
  double ClampBeat(double beat) const;

  // 밀도 매칭. 가장 잘 맞는 위치와 신뢰도를 돌려준다. 마디가 없으면 신뢰도 0.
  void MatchDensity(int onsets, double window_beats, double* best_beat,
                    double* confidence) const;

  const ScoreGeometry* geometry_;
  PositionEstimatorParams params_;

  double beat_ = 0.0;
  double tempo_bpm_ = 0.0;
  double since_resync_ = 0.0;

  // 환산 보정 상태.
  double ink_per_onset_ = 0.0;
  double calib_ink_ = 0.0;
  double calib_onsets_ = 0.0;

  // 급변 억제 상태 (6.3).
  int jump_streak_ = 0;
  int jump_direction_ = 0;

  // 보정에 쓸 "지나온 잉크"를 재기 위한 직전 재동기화 시점의 위치.
  double last_resync_beat_ = 0.0;
  bool has_last_resync_ = false;
};

}  // namespace nota
