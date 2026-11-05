#include "nota/scroll_controller.hpp"

#include <algorithm>
#include <cmath>

namespace nota {
namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

ScrollController::ScrollController(const ScoreGeometry* geometry,
                                   ScrollControllerParams params)
    : geometry_(geometry), params_(params) {}

void ScrollController::SetViewportHeight(double px) {
  viewport_height_ = std::max(0.0, px);
}

double ScrollController::ReadLineOffset() const {
  return viewport_height_ * params_.read_line_fraction;
}

double ScrollController::TargetFor(double beat) const {
  if (geometry_ == nullptr || geometry_->empty()) return 0.0;
  return geometry_->CanvasY(beat) - ReadLineOffset();
}

void ScrollController::SnapTo(double beat) {
  target_ = TargetFor(beat);
  offset_ = target_;
  velocity_ = 0.0;
  waiting_to_resume_ = false;
  since_manual_release_s_ = 0.0;
}

void ScrollController::Update(const ScrollInput& input) {
  if (geometry_ == nullptr || geometry_->empty()) return;

  target_ = TargetFor(input.beat);

  // 사용자가 손을 대고 있으면 제어기는 물러난다. 자동 기능이 사용자와
  // 싸우면 안 된다 (DESIGN.md 2.4).
  if (manual_) return;

  // 손을 뗀 직후의 대기. 손가락이 잠깐 떨어진 것과 조작을 마친 것을 구별한다.
  if (waiting_to_resume_) {
    since_manual_release_s_ += std::max(0.0, input.dt_s);
    if (since_manual_release_s_ < params_.manual_resume_delay_s) return;
    waiting_to_resume_ = false;
    // 재개 시 속도를 0에서 시작한다. 직전 손동작의 관성을 물려받지 않는다.
    velocity_ = 0.0;
  }

  // 앱이 백그라운드에 다녀오면 dt가 수 초로 들어온다. 그대로 적분하면
  // 화면이 날아가므로 한 걸음의 크기를 제한한다.
  const double dt = std::clamp(input.dt_s, 0.0, params_.max_time_step_s);
  if (dt <= 0.0) return;

  // --- 속도 피드포워드 (ALGORITHM.md 7.3) ------------------------------------
  // 스프링만 쓰면 등속 구간에서 항상 뒤처진다(정상상태 오차). 기대 속도를
  // 미리 넣으면 스프링은 오차 보정만 담당한다.
  double feedforward = 0.0;
  if (input.playing && input.tempo_bpm > 0.0) {
    feedforward =
        geometry_->PixelsPerBeat(input.beat) * (input.tempo_bpm / 60.0);
  }

  // --- 임계 감쇠 스프링 (ALGORITHM.md 7.2) -----------------------------------
  const double omega = 2.0 * kPi * params_.natural_frequency_hz;
  const double accel = -2.0 * params_.damping * omega * (velocity_ - feedforward) -
                       omega * omega * (offset_ - target_);

  // 준음해적 오일러. 같은 비용에서 명시적 오일러보다 안정적이다.
  velocity_ += accel * dt;

  // --- 속도 제한 (ALGORITHM.md 7.4) ------------------------------------------
  // 템포 오추정이나 재동기화 점프가 화면을 날려버리지 않게 한다.
  if (viewport_height_ > 0.0) {
    const double limit =
        viewport_height_ * params_.max_velocity_screens_per_second;
    velocity_ = std::clamp(velocity_, -limit, limit);
  }

  offset_ += velocity_ * dt;
}

void ScrollController::BeginManualScroll() {
  manual_ = true;
  waiting_to_resume_ = false;
  since_manual_release_s_ = 0.0;
  velocity_ = 0.0;
}

void ScrollController::SetManualOffset(double canvas_offset) {
  if (!manual_) return;
  offset_ = canvas_offset;
  velocity_ = 0.0;
}

void ScrollController::EndManualScroll() {
  if (!manual_) return;
  manual_ = false;
  waiting_to_resume_ = true;
  since_manual_release_s_ = 0.0;
  velocity_ = 0.0;
}

double ScrollController::BeatAtCurrentOffset() const {
  if (geometry_ == nullptr || geometry_->empty()) return 0.0;
  return geometry_->BeatAt(offset_ + ReadLineOffset());
}

}  // namespace nota
