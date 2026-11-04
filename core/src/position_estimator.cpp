// 위치 추정 구현. 설계 근거는 position_estimator.hpp와 docs/ALGORITHM.md 6절.

#include "nota/position_estimator.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nota {

PositionEstimator::PositionEstimator(const ScoreGeometry& geometry,
                                     const PositionEstimatorParams& params)
    : geometry_(&geometry), params_(params) {
  ink_per_onset_ = std::max(1e-6, params_.initial_ink_per_onset);
}

double PositionEstimator::ClampBeat(double beat) const {
  if (!std::isfinite(beat)) return 0.0;
  if (geometry_->empty()) return beat;
  return std::min(geometry_->total_beats(), std::max(0.0, beat));
}

void PositionEstimator::Reset(double beat, double tempo_bpm) {
  beat_ = ClampBeat(beat);
  tempo_bpm_ = std::isfinite(tempo_bpm) ? std::max(0.0, tempo_bpm) : 0.0;
  since_resync_ = 0.0;
  jump_streak_ = 0;
  jump_direction_ = 0;
  has_last_resync_ = false;
  last_resync_beat_ = beat_;
}

void PositionEstimator::SetPosition(double beat) {
  beat_ = ClampBeat(beat);
  // 자리를 옮긴 것이므로 점프 연속 횟수는 지운다. 옮기기 전에 쌓인 증거는
  // 새 위치에 대해서는 증거가 아니다.
  jump_streak_ = 0;
  jump_direction_ = 0;
  // 보정 구간도 끊는다 — 건너뛴 구간의 잉크를 "지나온 잉크"로 세면 안 된다.
  has_last_resync_ = false;
  last_resync_beat_ = beat_;
}

void PositionEstimator::MatchDensity(int onsets, double window_beats,
                                     double* best_beat,
                                     double* confidence) const {
  *best_beat = beat_;
  *confidence = 0.0;
  if (geometry_->empty() || geometry_->measures().empty()) return;
  if (!(window_beats > 0.0) || onsets < 0) return;

  const double beats_per_measure = geometry_->BeatsPerMeasureAt(beat_);
  const double radius = params_.search_measures * beats_per_measure;
  if (!(radius > 0.0)) return;

  const int samples = std::max(3, params_.search_samples | 1);  // 홀수로
  const double observed = static_cast<double>(onsets);

  std::vector<double> costs(static_cast<std::size_t>(samples));
  std::vector<double> candidates(static_cast<std::size_t>(samples));
  double best_cost = 0.0;
  std::size_t best_index = 0;
  for (int i = 0; i < samples; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
    const double candidate = beat_ - radius + 2.0 * radius * t;
    // 후보 위치에서 창 뒤쪽으로 window_beats만큼의 잉크가 기대 온셋 수가 된다.
    const double ink = geometry_->InkBetween(candidate - window_beats, candidate);
    const double expected = ink / ink_per_onset_;
    const double cost = std::fabs(observed - expected);
    candidates[static_cast<std::size_t>(i)] = candidate;
    costs[static_cast<std::size_t>(i)] = cost;
    if (i == 0 || cost < best_cost) {
      best_cost = cost;
      best_index = static_cast<std::size_t>(i);
    }
  }

  *best_beat = candidates[best_index];

  // 신뢰도. 두 가지를 함께 본다.
  //
  // 1. 절대 일치도 — 최선의 후보에서도 많이 어긋나면 환산이 틀렸거나 온셋
  //    검출이 깨진 것이다. 그런 보정은 적용하면 안 된다.
  //
  // 2. **최소값이 유일한가.** 처음에는 "최선 대 평균"으로 뚜렷함을 쟀다.
  //    실측에서 그것이 틀렸다 — 신뢰도가 0.9~0.99로 나오는데도 목표가 ±5박씩
  //    헤맸다. 이유: 창 잉크는 위치의 단조 함수가 아니다. 지형의 극값 근처에서는
  //    **양쪽 위치가 같은 잉크를 가진다.** 그러면 최선 대 평균은 좋게 나오는데
  //    argmin은 두 지점 중 하나를 임의로 고른다.
  //
  //    그래서 최선과 비슷하게 맞는 후보들이 **박 축에서 얼마나 넓게 퍼져 있는지**
  //    를 본다. 넓으면 위치가 결정되지 않은 것이므로 믿지 않는다. 이것이 모호함을
  //    직접 재는 유일한 방법이다 (ALGORITHM.md 8.5).
  const double scale = std::max(1.0, observed);
  const double absolute = std::max(0.0, 1.0 - best_cost / scale);

  // 구별 불가 범위. 관측량에 비례한다 — 온셋이 50개 들어오는 악보에서 반 개
  // 차이로 위치를 가리는 것은 측정 정밀도를 넘어선 요구다
  // (position_estimator.hpp, ALGORITHM.md 8.5).
  const double resolution =
      std::max(0.5, params_.onset_count_relative_uncertainty * observed);
  const double tolerance = best_cost + resolution;
  double lo_beat = candidates[best_index];
  double hi_beat = candidates[best_index];
  for (std::size_t i = 0; i < costs.size(); ++i) {
    if (costs[i] <= tolerance) {
      lo_beat = std::min(lo_beat, candidates[i]);
      hi_beat = std::max(hi_beat, candidates[i]);
    }
  }
  const double ambiguity = hi_beat - lo_beat;
  // 모호 구간이 탐색 범위의 절반을 넘으면 0. 한 표본 간격이면 1.
  const double uniqueness = std::max(0.0, 1.0 - ambiguity / radius);

  // 3. **최소값이 범위 안쪽에 있는가.** 끝에 있으면 진짜 최소는 범위 밖이므로
  //    이 매칭은 "더 가라"는 방향만 말하고 위치는 말하지 못한다.
  //
  //    이 검사가 없으면 폭주한다. 실측(Bach BWV846): 재동기화를 켜자 박 오차가
  //    4.08박에서 87.37박(7시스템)으로 커졌다. 원인은 환산 보정의 작은 오차였다 —
  //    참값 120인데 109로 추정하면 기대 온셋 수가 전체적으로 10% 높게 나오고,
  //    그러면 argmin이 **언제나 잉크가 가장 적은 쪽 끝**을 고른다. 균일한
  //    악보에서는 그 편향이 매번 같은 방향이므로 0.5초마다 0.8박씩 한쪽으로
  //    밀려 81초 동안 133박까지 밀 수 있다 (ALGORITHM.md 8.5).
  //
  //    한쪽 끝이 뽑히는 것은 "환산이 틀렸다"는 신호이지 "위치가 저기다"라는
  //    신호가 아니다. 그래서 버린다. 환산 보정은 계속 돌아가므로, 환산이 맞아
  //    들어가면 최소값이 안쪽으로 들어오고 매칭이 다시 일한다.
  const bool interior = best_index > 0 && best_index + 1 < costs.size();

  *confidence = interior ? std::min(1.0, absolute * uniqueness) : 0.0;
}

PositionState PositionEstimator::Update(double dt, double tempo_bpm, bool playing,
                                        int onsets_in_window) {
  PositionState out;

  // 비정상 dt는 무시한다. 시계는 가끔 거꾸로 간다.
  if (!(dt > 0.0)) {
    out.beat = beat_;
    out.tempo_bpm = tempo_bpm_;
    out.ink_per_onset = ink_per_onset_;
    out.jump_streak = jump_streak_;
    out.jump_pending = jump_streak_ > 0;
    return out;
  }

  tempo_bpm_ = std::isfinite(tempo_bpm) ? std::max(0.0, tempo_bpm) : 0.0;

  // 1. 적분 (6.1). 정지 중에는 위치를 유지한다.
  if (playing) {
    beat_ += (tempo_bpm_ / 60.0) * dt;
  }
  beat_ = ClampBeat(beat_);

  since_resync_ += dt;

  out.beat = beat_;
  out.tempo_bpm = tempo_bpm_;
  out.ink_per_onset = ink_per_onset_;
  out.jump_streak = jump_streak_;
  out.jump_pending = jump_streak_ > 0;

  // 2. 재동기화 (6.2). 주기가 되지 않았거나 온셋 수를 모르면 건너뛴다.
  if (since_resync_ < params_.resync_interval_seconds) return out;
  if (!playing || onsets_in_window < 0) {
    // 정지 중에는 재동기화하지 않는다 — 창 안의 온셋 수가 0으로 떨어지므로
    // "잉크가 없는 곳"으로 끌려간다. 창도 끊어 둔다.
    since_resync_ = 0.0;
    has_last_resync_ = false;
    last_resync_beat_ = beat_;
    return out;
  }
  since_resync_ = 0.0;

  const double window_beats = (tempo_bpm_ / 60.0) * params_.resync_window_seconds;

  // 3. 환산 보정. 직전 재동기화 이후 지나온 잉크를 그 사이 관측한 온셋 수로
  //    나눈다. 보정 자체가 위치 추정에 의존하므로 순환이지만, 틀림의 방향이
  //    서로 곱해지지 않는다 — 위치가 조금 틀려도 지나온 잉크의 총량은 비슷하다.
  if (has_last_resync_) {
    const double traversed = geometry_->InkBetween(last_resync_beat_, beat_);
    calib_ink_ += traversed;
    calib_onsets_ += static_cast<double>(onsets_in_window) *
                     (params_.resync_interval_seconds /
                      std::max(1e-6, params_.resync_window_seconds));
    if (calib_onsets_ >= static_cast<double>(params_.calibration_warmup_onsets) &&
        calib_ink_ > 0.0) {
      const double observed_ratio = calib_ink_ / calib_onsets_;
      const double lo =
          params_.initial_ink_per_onset * params_.calibration_min_ratio;
      const double hi =
          params_.initial_ink_per_onset * params_.calibration_max_ratio;
      const double clamped = std::min(hi, std::max(lo, observed_ratio));
      const double a = std::min(1.0, std::max(0.0, params_.calibration_alpha));
      ink_per_onset_ = (1.0 - a) * ink_per_onset_ + a * clamped;
      ink_per_onset_ = std::max(1e-6, ink_per_onset_);
    }
  }
  last_resync_beat_ = beat_;
  has_last_resync_ = true;
  out.ink_per_onset = ink_per_onset_;

  // 4. 밀도 매칭.
  double target = beat_;
  double confidence = 0.0;
  MatchDensity(onsets_in_window, window_beats, &target, &confidence);
  out.resync_target = target;
  out.confidence = confidence;
  if (!(confidence > 0.0)) {
    // 못 믿을 매칭은 점프 증거로도 세지 않는다.
    jump_streak_ = 0;
    jump_direction_ = 0;
    out.jump_streak = 0;
    out.jump_pending = false;
    return out;
  }

  // 5. 급변 억제 (6.3). 한 시스템 이상 떨어진 보정은 연속 3회 같은 방향으로
  //    나타날 때만 허용한다. 단발 오추정이 악보를 날려버리는 것을 막는다.
  const double delta = target - beat_;
  const double system_beats = [&] {
    const GeometrySystem& s =
        geometry_->systems()[static_cast<std::size_t>(geometry_->SystemAt(beat_))];
    return std::max(1e-9, s.beat_end - s.beat_start);
  }();
  const bool is_jump =
      std::fabs(delta) >= params_.jump_threshold_systems * system_beats;

  if (is_jump) {
    const int dir = delta > 0.0 ? 1 : -1;
    if (dir == jump_direction_) {
      ++jump_streak_;
    } else {
      jump_direction_ = dir;
      jump_streak_ = 1;
    }
    out.jump_streak = jump_streak_;
    if (jump_streak_ < params_.jump_consistency_required) {
      // 아직 증거가 모자라다. 보류한다.
      out.jump_pending = true;
      return out;
    }
    // 증거가 모였다. 점프를 허용하고 증거를 비운다.
    jump_streak_ = 0;
    jump_direction_ = 0;
    out.jump_streak = 0;
  } else {
    // 작은 보정이 들어오면 큰 점프 증거는 사라진다 — 서로 모순되는 관측이다.
    jump_streak_ = 0;
    jump_direction_ = 0;
    out.jump_streak = 0;
  }
  out.jump_pending = false;

  // 6. 상보 필터로 혼합. 보정을 급격히 적용하지 않는다.
  const double beta =
      std::min(1.0, std::max(0.0, params_.blend_beta)) * confidence;
  beat_ += beta * delta;
  beat_ = ClampBeat(beat_);

  out.beat = beat_;
  out.resynced = true;
  out.resync_shift = beta * delta;
  // 혼합 뒤의 위치가 다음 보정 구간의 시작이다.
  last_resync_beat_ = beat_;
  return out;
}

}  // namespace nota
