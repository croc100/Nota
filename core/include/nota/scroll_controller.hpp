// 스크롤 제어. docs/ALGORITHM.md 7절.
//
// 악보상 위치(박)를 화면 오프셋으로 바꾼다. 여기서 중요한 것은 정확성보다
// **부드러움**이다. 떨리거나 급변하는 스크롤은 추적 오차보다 연주를 더 방해한다.
//
// 제어기가 직접 푸는 문제는 세 가지다.
//
//   1. 목표를 그대로 따르면 재동기화마다 화면이 튄다 → 2차 시스템으로 평활화
//   2. 스프링만 쓰면 등속 구간에서 항상 뒤처진다 → 속도 피드포워드
//   3. 오추정이 화면을 날려버릴 수 있다 → 속도 제한
//
// 그리고 사용자가 손으로 스크롤하면 즉시 양보한다 (DESIGN.md 2.4).
#pragma once

#include "nota/score_geometry.hpp"

namespace nota {

struct ScrollControllerParams {
  // 현재 연주 위치를 화면 어디에 둘지. 화면 높이 대비 비율.
  // 아래쪽은 앞으로 연주할 부분이므로 1/3이면 두 배 넓게 미리 읽을 수 있다.
  // 사용자가 조절할 수 있어야 한다 (DESIGN.md 2.2).
  double read_line_fraction = 1.0 / 3.0;

  // 감쇠비. 1.0이 임계 감쇠다.
  //
  // 오버슈트가 없으면서 가장 빠르게 수렴한다. 악보가 지나쳤다 되돌아오면
  // 연주자가 자기 위치를 잃는다. 1 미만으로 내리면 더 빨리 반응하지만
  // 그 대가로 되돌아오는 움직임이 생기므로, 기본값에서 내리지 않는다.
  double damping = 1.0;

  // 고유 진동수(Hz). 시정수는 대략 1/(2*pi*f) = 0.2초.
  double natural_frequency_hz = 0.8;

  // 속도 상한. 화면 높이 대비 초당 배수.
  // 템포 오추정이나 재동기화 점프가 화면을 날려버리지 않게 한다.
  double max_velocity_screens_per_second = 1.5;

  // 손을 뗀 뒤 자동 추적을 재개하기까지의 대기(초). 손가락이 잠깐 떨어진 것과
  // 조작을 마친 것을 구별한다.
  double manual_resume_delay_s = 0.3;

  // 한 번의 Update가 처리할 최대 시간(초). 앱이 백그라운드에 다녀오면 dt가
  // 수 초로 들어올 수 있고, 그대로 적분하면 화면이 날아간다.
  double max_time_step_s = 0.1;
};

// 호출자가 매 프레임 넘기는 입력.
struct ScrollInput {
  double dt_s = 0.0;
  // 위치 추정기가 말하는 현재 악보상 위치(박).
  double beat = 0.0;
  // 템포 추정치. 피드포워드 항에 쓴다.
  double tempo_bpm = 0.0;
  // 연주 중인가. 거짓이면 전진하지 않는다 (DESIGN.md 2.3).
  bool playing = false;
};

class ScrollController {
 public:
  // geometry는 소유하지 않는다. 호출자가 수명을 보장해야 한다.
  // 널이면 제어기는 아무것도 하지 않는다 (악보를 아직 읽지 않은 상태).
  explicit ScrollController(const ScoreGeometry* geometry,
                            ScrollControllerParams params = {});

  void SetViewportHeight(double px);
  double viewport_height() const { return viewport_height_; }

  // 현재 위치로 즉시 점프한다. 곡을 처음 열거나 사용자가 구간을 건너뛸 때.
  void SnapTo(double beat);

  void Update(const ScrollInput& input);

  // --- 수동 개입 (DESIGN.md 2.4) ---------------------------------------------
  // 사용자가 손을 대면 자동 추적은 즉시 멈추고 제어를 넘긴다.
  void BeginManualScroll();
  // 사용자가 끄는 동안의 오프셋. 그대로 반영한다.
  void SetManualOffset(double canvas_offset);
  void EndManualScroll();
  bool manual_active() const { return manual_; }

  // 손을 뗀 위치가 가리키는 악보상 위치. 호출자는 이 값을 위치 추정기에
  // 반영해 "사용자가 스크롤한 곳이 지금 위치"가 되게 해야 한다.
  double BeatAtCurrentOffset() const;

  // --- 상태 ------------------------------------------------------------------
  // 화면 맨 위에 해당하는 캔버스 y.
  double offset() const { return offset_; }
  double velocity() const { return velocity_; }
  // 현재 목표 오프셋. 디버깅과 시각화용.
  double target_offset() const { return target_; }

 private:
  double ReadLineOffset() const;
  double TargetFor(double beat) const;

  const ScoreGeometry* geometry_ = nullptr;
  ScrollControllerParams params_;

  double viewport_height_ = 0.0;
  double offset_ = 0.0;
  double velocity_ = 0.0;
  double target_ = 0.0;

  bool manual_ = false;
  // 손을 뗀 뒤 흐른 시간. params_.manual_resume_delay_s 를 넘으면 재개한다.
  double since_manual_release_s_ = 0.0;
  bool waiting_to_resume_ = false;
};

}  // namespace nota
