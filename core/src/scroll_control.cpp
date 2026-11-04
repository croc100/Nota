// 스크롤 제어 구현. 설계 근거는 scroll_control.hpp와 docs/ALGORITHM.md 7절.

#include "nota/scroll_control.hpp"

#include <algorithm>
#include <cmath>

namespace nota {

ScrollController::ScrollController(const ScoreGeometry& geometry,
                                   double viewport_height,
                                   const ScrollControlParams& params)
    : geometry_(&geometry),
      viewport_height_(std::max(0.0, viewport_height)),
      params_(params) {
  offset_ = min_offset();
}

void ScrollController::SetViewportHeight(double viewport_height) {
  viewport_height_ = std::max(0.0, viewport_height);
  // 읽기 선이 바뀌었으므로 범위도 바뀐다. 현재 오프셋을 새 범위로 들여놓는다.
  offset_ = Clamp(offset_);
}

double ScrollController::TargetOffset(double beat) const {
  if (geometry_->empty()) return 0.0;
  return geometry_->CanvasY(beat) - read_line();
}

double ScrollController::min_offset() const {
  if (geometry_->empty()) return 0.0;
  // 곡의 시작이 읽기 선에 온 상태. 읽기 선 위쪽은 비어 보인다 — 맞는 모습이다.
  return geometry_->CanvasY(0.0) - read_line();
}

double ScrollController::max_offset() const {
  if (geometry_->empty()) return 0.0;
  return geometry_->CanvasY(geometry_->total_beats()) - read_line();
}

double ScrollController::Clamp(double offset) const {
  const double lo = min_offset();
  const double hi = max_offset();
  if (hi <= lo) return lo;
  return std::min(hi, std::max(lo, offset));
}

void ScrollController::Reset(double beat) {
  offset_ = Clamp(TargetOffset(beat));
  velocity_ = 0.0;
  manual_ = false;
  settle_remaining_ = -1.0;
}

void ScrollController::BeginManual() {
  manual_ = true;
  // 스프링이 쌓아 둔 속도를 버린다. 손을 올린 순간 화면은 손만 따라야 한다.
  velocity_ = 0.0;
  settle_remaining_ = -1.0;
}

void ScrollController::SetManualOffset(double offset) {
  if (!manual_) return;
  offset_ = Clamp(offset);
}

void ScrollController::EndManual() {
  if (!manual_) return;
  manual_ = false;
  velocity_ = 0.0;
  settle_remaining_ = std::max(0.0, params_.manual_settle_seconds);
}

ScrollState ScrollController::Update(double dt, double beat, double tempo_bpm) {
  ScrollState out;
  out.manual = manual_;

  if (geometry_->empty()) {
    offset_ = 0.0;
    velocity_ = 0.0;
    return out;
  }

  // dt가 비정상이면(음수, NaN) 아무것도 하지 않는다. 시계는 가끔 거꾸로 간다.
  if (!(dt > 0.0)) {
    out.offset = offset_;
    out.velocity = velocity_;
    return out;
  }
  // 긴 공백은 적분으로 메우지 않는다 — 앱이 멈춰 있었던 것이다.
  dt = std::min(dt, params_.max_dt_seconds);

  // 1. 사용자가 제어 중이면 스프링은 돌지 않는다. 오프셋은 SetManualOffset이
  //    정한다.
  if (manual_) {
    velocity_ = 0.0;
    out.offset = offset_;
    out.velocity = 0.0;
    return out;
  }

  // 2. 손을 뗀 직후의 대기. 이 사이에도 스프링을 돌리지 않는다 — 돌리면 사용자가
  //    맞춰 둔 위치에서 원래 위치로 끌려가 버리고, 그러면 수동 개입이 무의미해진다.
  if (settle_remaining_ >= 0.0) {
    settle_remaining_ -= dt;
    out.offset = offset_;
    out.velocity = 0.0;
    if (settle_remaining_ < 0.0) {
      settle_remaining_ = -1.0;
      // 사용자가 맞춘 오프셋을 악보상 위치로 되짚어 인수한다.
      out.position_handover = true;
      out.handover_beat = geometry_->BeatAt(offset_ + read_line());
    }
    return out;
  }

  // 3. 임계 감쇠 스프링 + 속도 피드포워드 (7.2, 7.3).
  //
  // 피드포워드가 기본 전진을 맡고 스프링은 오차 보정만 한다. 이 항이 없으면
  // 등속 구간에서 정상상태 오차가 남는다 — 스프링은 변위에 비례한 힘만 내므로,
  // 일정한 속도로 전진하려면 일정한 오차를 유지해야 하기 때문이다.
  const double beats_per_second = tempo_bpm > 0.0 ? tempo_bpm / 60.0 : 0.0;
  const double v_max =
      viewport_height_ * std::max(0.0, params_.max_speed_screens_per_sec);

  // dt를 쪼개 적분한다. 프레임이 한 번 길어지는 것(GC, 화면 전환)이 스프링을
  // 발산시키지 않게 한다.
  const double substep = std::max(1e-6, params_.max_substep_seconds);
  int steps = static_cast<int>(std::ceil(dt / substep));
  steps = std::max(1, steps);
  const double h = dt / static_cast<double>(steps);

  const double two_zeta_omega = 2.0 * params_.zeta * params_.omega;
  const double omega_sq = params_.omega * params_.omega;

  for (int i = 0; i < steps; ++i) {
    // 부분 단계마다 목표를 다시 구한다. beat는 이 프레임 **끝**의 위치이므로,
    // 중간 시점의 위치는 템포로 거꾸로 보간한다.
    //
    // 이 보간이 없으면 프레임이 길어질 때 오차가 v·dt/2만큼 남는다. 실측:
    // dt=0.2초, 25 px/s에서 2.5 px. 60 FPS에서는 보이지 않지만 프레임이 드롭되면
    // 그만큼 어긋나고, dt에 따라 결과가 달라지는 제어기는 측정이 어렵다.
    const double time_left = dt - static_cast<double>(i + 1) * h;
    const double beat_i = beat - beats_per_second * time_left;
    const double y_target = TargetOffset(beat_i);
    const double v_ff = beats_per_second * geometry_->PixelsPerBeat(beat_i);

    const double accel =
        -two_zeta_omega * (velocity_ - v_ff) - omega_sq * (offset_ - y_target);
    velocity_ += accel * h;
    // 7.4 속도 제한. 가속도가 아니라 속도를 자르는 이유는, 제한의 목적이
    // "화면이 날아가지 않는 것"이지 "부드러운 가속"이 아니기 때문이다.
    if (v_max > 0.0) velocity_ = std::min(v_max, std::max(-v_max, velocity_));
    offset_ += velocity_ * h;

    // 범위 밖으로 나가면 속도도 지운다. 그러지 않으면 벽에 붙어 있는 동안
    // 속도가 계속 쌓여 벽에서 떨어질 때 튀어나간다.
    const double clamped = Clamp(offset_);
    if (clamped != offset_) {
      offset_ = clamped;
      velocity_ = 0.0;
    }
  }

  out.offset = offset_;
  out.velocity = velocity_;
  return out;
}

}  // namespace nota
