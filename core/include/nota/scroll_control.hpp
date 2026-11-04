// 스크롤 제어. docs/ALGORITHM.md 7절.
//
// 악보상 위치(박)를 화면 오프셋(px)으로 바꾼다. 여기서 중요한 것은 정확성보다
// **부드러움**이다. 떨리거나 급변하는 스크롤은 추적 오차보다 연주를 더 방해한다.
//
// 입력은 숫자뿐이다 — 박, 템포, 경과 시간, 화면 높이. 오디오도 이미지도 보지
// 않으므로 단위 테스트로 전부 확인된다.
//
// 구조:
//   위치 추정기 ──(박, 템포)──> ScrollController ──(오프셋)──> 렌더러
//                                      │
//                                 ScoreGeometry (박↔픽셀)
//
// 왜 스프링인가. 목표 오프셋을 그대로 따르면 재동기화(ALGORITHM.md 6.2)가 위치를
// 보정할 때마다 화면이 튄다. 2차 시스템을 거치면 그 보정이 0.2초에 걸쳐 녹아든다.
// 임계 감쇠(ζ=1)를 쓰는 이유는 오버슈트가 없어야 하기 때문이다 — 악보가 지나쳤다
// 되돌아오면 연주자는 자기가 어디를 보고 있었는지 잃는다.
#pragma once

#include "nota/score_geometry.hpp"

namespace nota {

struct ScrollControlParams {
  // 읽기 선의 화면 내 위치 (화면 높이 대비). 현재 연주 위치가 이 높이에 온다.
  // 1/3인 이유: 위로 1/3은 이미 지난 마디(눈이 되돌아갈 여지), 아래로 2/3은
  // 앞으로 올 마디(미리 읽을 여지). 연주자는 앞을 더 많이 봐야 한다.
  double read_line_fraction = 1.0 / 3.0;

  // 스프링 고유 진동수(rad/s). 2π·0.8 ≈ 5.03 → 시정수 약 0.2초.
  double omega = 5.0265482457436691;  // 2 * pi * 0.8

  // 감쇠비. 1이 임계 감쇠다. 낮추면 오버슈트가 생긴다 — 7.2절의 이유로 낮추지
  // 않는다. 설정으로 열어 두는 것은 측정으로 확인하기 위해서다.
  double zeta = 1.0;

  // 속도 상한 (화면 높이 / 초). 템포 오추정이나 재동기화 점프가 화면을
  // 날려버리지 않게 한다.
  double max_speed_screens_per_sec = 1.5;

  // 손을 뗀 뒤 위치를 인수하기까지 기다리는 시간(초). 손가락이 떨어진 직후의
  // 관성 스크롤이나 미세한 흔들림을 위치로 받아들이지 않기 위한 여유다.
  double manual_settle_seconds = 0.3;

  // 한 번의 적분 부분 단계 상한(초). dt가 커도 스프링이 발산하지 않게 쪼갠다.
  // 명시적 적분의 안정 조건은 대략 ω·dt < 2이고, ω=5.03에서 dt<0.4다. 0.008초로
  // 두면 ω를 20배 올려도 안정하다.
  double max_substep_seconds = 1.0 / 120.0;

  // dt 상한(초). 이보다 긴 공백은 앱이 멈춰 있었던 것이므로 적분으로 메우지
  // 않는다. 호출자가 Reset으로 다시 맞추는 것이 맞다.
  double max_dt_seconds = 0.25;
};

// 한 프레임의 결과.
struct ScrollState {
  double offset = 0.0;    // 화면 상단이 가리키는 캔버스 y
  double velocity = 0.0;  // px/s
  bool manual = false;    // 사용자가 제어 중 (스프링 꺼짐)

  // 이 프레임에 수동 개입이 끝나 위치 인수가 일어났는가. 참이면 호출자는
  // 위치 추정기의 p를 handover_beat로 바꿔야 한다 (ALGORITHM.md 7.5).
  //
  // 제어기가 위치 추정기를 직접 건드리지 않는 이유: 코어의 각 모듈은 숫자를
  // 받아 숫자를 내놓는다. 상호 참조를 만들면 단위 테스트가 어려워진다.
  bool position_handover = false;
  double handover_beat = 0.0;
};

class ScrollController {
 public:
  // geometry와 화면 높이는 제어기보다 오래 살아야 한다. geometry가 비어 있으면
  // 모든 오프셋이 0이 된다.
  ScrollController(const ScoreGeometry& geometry, double viewport_height,
                   const ScrollControlParams& params = {});

  // 화면 높이가 바뀌면(회전, 분할 화면) 알려준다. 읽기 선과 속도 상한이 함께
  // 바뀐다. 현재 오프셋은 유지한다.
  void SetViewportHeight(double viewport_height);

  // 해당 박에 즉시 정렬한다. 스프링 상태(속도)를 0으로 지운다. 곡을 시작할 때,
  // 그리고 앱이 멈췄다 돌아왔을 때 쓴다.
  void Reset(double beat);

  // 한 프레임 전진한다.
  //   dt         경과 시간(초)
  //   beat       위치 추정기의 현재 악보상 위치
  //   tempo_bpm  현재 템포. 0이면 정지 — 피드포워드가 꺼지고 스프링만 남는다
  ScrollState Update(double dt, double beat, double tempo_bpm);

  // 수동 개입 (ALGORITHM.md 7.5).
  void BeginManual();                 // 터치 시작. 스프링 즉시 비활성화
  void SetManualOffset(double offset);  // 사용자가 끌고 있는 오프셋
  void EndManual();                   // 손 뗌. settle 시간 뒤 위치를 인수한다

  double offset() const { return offset_; }
  double velocity() const { return velocity_; }
  bool manual() const { return manual_; }

  // 읽기 선의 화면 내 y (px).
  double read_line() const { return viewport_height_ * params_.read_line_fraction; }

  // 오프셋이 움직일 수 있는 범위. 곡의 시작과 끝을 각각 읽기 선에 맞춘 지점이다.
  double min_offset() const;
  double max_offset() const;

 private:
  double TargetOffset(double beat) const;
  double Clamp(double offset) const;

  const ScoreGeometry* geometry_;
  double viewport_height_ = 0.0;
  ScrollControlParams params_;

  double offset_ = 0.0;
  double velocity_ = 0.0;

  bool manual_ = false;
  // 손을 뗀 뒤 인수까지 남은 시간. 음수면 대기 중이 아니다.
  double settle_remaining_ = -1.0;
};

}  // namespace nota
