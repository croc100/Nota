// 위치 추정 테스트.
//
// 온셋 검출기가 없어도 전부 확인된다. 온셋 수를 정답 위치의 잉크에서 직접
// 합성해 넣기 때문이다 — "검출기가 완벽하다면 추정기의 논리가 맞는가"를 묻는다.
// 검출기의 품질은 녹음이 와야 측정된다(V3/V4).
//
// 확인하는 성질:
//   1. 적분   — 정지 중에는 멈추고, 연주 중에는 템포대로 전진한다
//   2. 드리프트 보정 — 템포가 틀려 누적된 오차를 재동기화가 따라잡는다
//   3. 안정성 — 보정이 떨림을 만들지 않는다 (적분만 한 경우보다 나빠지지 않는다)
//   4. 급변 억제 — 단발 오추정이 악보를 날려버리지 않는다
//   5. 환산 보정 — 잉크/온셋 상수를 모르는 상태에서 스스로 찾아간다

#include <cmath>
#include <vector>

#include "nota/position_estimator.hpp"
#include "nota/score_geometry.hpp"
#include "test_framework.hpp"

namespace {

using nota::PageAnalysis;
using nota::PositionEstimator;
using nota::PositionEstimatorParams;
using nota::PositionState;
using nota::ScoreGeometry;
using nota::ScoreGeometryParams;

// 잉크가 마디마다 다른 가짜 악보. 밀도 매칭이 일할 수 있으려면 지형에 기복이
// 있어야 한다 — 균일한 악보에서는 매칭이 원리상 위치를 가리지 못한다.
PageAnalysis MakeVariedPage(int systems, int measures_per_system) {
  PageAnalysis page;
  int counter = 0;
  for (int i = 0; i < systems; ++i) {
    nota::System sys;
    sys.staff_count = 2;
    sys.y_top = 100.0 + 240.0 * i;
    sys.y_bottom = sys.y_top + 120.0;
    sys.x_start = 80.0;
    sys.x_end = 920.0;
    sys.staff_space = 12.0;
    page.systems.push_back(sys);

    const double width = (sys.x_end - sys.x_start) / measures_per_system;
    for (int m = 0; m < measures_per_system; ++m) {
      nota::Measure measure;
      measure.index = static_cast<int>(page.measures.size());
      measure.system_index = i;
      measure.x_start = sys.x_start + width * m;
      measure.x_end = measure.x_start + width;
      measure.y_top = sys.y_top;
      measure.y_bottom = sys.y_bottom;
      // 마디마다 뚜렷하게 다른 밀도. 4마디 주기로 변한다 — 실제 악보의
      // 악구 구조를 거칠게 모사한 것이다.
      static const int kPattern[] = {1200, 300, 800, 2000, 500, 1500, 200, 900};
      measure.ink_density = kPattern[counter++ % 8];
      page.measures.push_back(measure);
    }
  }
  page.quality.dominant_staff_count = 2;
  page.quality.consistent_systems = systems;
  page.quality.total_systems = systems;
  return page;
}

ScoreGeometry MakeVariedGeometry(int systems = 10, int measures = 4) {
  ScoreGeometryParams params;
  params.trim_page_margins = false;
  return ScoreGeometry::Build({MakeVariedPage(systems, measures)}, {3000}, params);
}

// 정답 위치에서 "검출된 온셋 수"를 합성한다. 완벽한 검출기를 흉내 낸다.
int SynthOnsets(const ScoreGeometry& geom, double true_beat, double window_beats,
                double ink_per_onset) {
  const double ink = geom.InkBetween(true_beat - window_beats, true_beat);
  return static_cast<int>(std::lround(ink / ink_per_onset));
}

}  // namespace

// 적분. 연주 중에는 템포대로 전진하고 정지 중에는 멈춘다 (6.1).
NOTA_TEST(PositionIntegratesTempoAndHoldsWhenStopped) {
  const ScoreGeometry geom = MakeVariedGeometry();
  PositionEstimator est(geom);
  est.Reset(0.0, 120.0);

  const double dt = 1.0 / 60.0;
  // 1초 연주 = 120 BPM에서 2박.
  for (int i = 0; i < 60; ++i) est.Update(dt, 120.0, true, -1);
  CHECK_NEAR(est.beat(), 2.0, 1e-6);

  // 1초 정지 — 위치가 유지된다.
  for (int i = 0; i < 60; ++i) est.Update(dt, 120.0, false, -1);
  CHECK_NEAR(est.beat(), 2.0, 1e-6);

  // 다시 연주.
  for (int i = 0; i < 60; ++i) est.Update(dt, 60.0, true, -1);
  CHECK_NEAR(est.beat(), 3.0, 1e-6);

  // 템포 0이면 전진하지 않는다.
  for (int i = 0; i < 60; ++i) est.Update(dt, 0.0, true, -1);
  CHECK_NEAR(est.beat(), 3.0, 1e-6);
}

// 범위를 벗어나지 않는다.
NOTA_TEST(PositionStaysWithinPiece) {
  const ScoreGeometry geom = MakeVariedGeometry();
  PositionEstimator est(geom);
  est.Reset(0.0, 240.0);
  const double dt = 1.0 / 60.0;
  for (int i = 0; i < 10000; ++i) {
    const PositionState s = est.Update(dt, 240.0, true, -1);
    CHECK(s.beat >= 0.0);
    CHECK(s.beat <= geom.total_beats() + 1e-9);
  }
  CHECK_NEAR(est.beat(), geom.total_beats(), 1e-6);

  est.Reset(5.0, 120.0);
  // 음수 템포는 0으로 다룬다 — 거꾸로 가지 않는다.
  for (int i = 0; i < 600; ++i) est.Update(dt, -100.0, true, -1);
  CHECK_NEAR(est.beat(), 5.0, 1e-9);
}

// 드리프트 보정. 템포 추정이 틀려 누적된 오차를 재동기화가 따라잡는다 (6.2).
//
// 이것이 V6(3분 연주 후 1시스템 이내)의 핵심이다.
NOTA_TEST(PositionResyncCorrectsTempoDrift) {
  const ScoreGeometry geom = MakeVariedGeometry(12, 4);
  const double true_tempo = 100.0;
  // 템포 추정이 3% 낮다. 적분만 하면 계속 뒤처진다.
  const double estimated_tempo = true_tempo * 0.97;
  const double ink_per_onset = 120.0;

  PositionEstimatorParams params;
  params.initial_ink_per_onset = ink_per_onset;
  PositionEstimator with_resync(geom, params);
  PositionEstimator integrate_only(geom, params);
  with_resync.Reset(0.0, estimated_tempo);
  integrate_only.Reset(0.0, estimated_tempo);

  const double dt = 1.0 / 60.0;
  double true_beat = 0.0;
  const double window_beats = (estimated_tempo / 60.0) * params.resync_window_seconds;

  // 3분 연주. 다만 악보가 짧으므로 곡 끝에 닿기 전까지만 본다.
  double worst_with = 0.0;
  double worst_without = 0.0;
  int frames = 0;
  while (true_beat < geom.total_beats() - window_beats - 1.0 && frames < 20000) {
    true_beat += (true_tempo / 60.0) * dt;
    const int onsets =
        SynthOnsets(geom, true_beat, window_beats, ink_per_onset);
    with_resync.Update(dt, estimated_tempo, true, onsets);
    integrate_only.Update(dt, estimated_tempo, true, -1);
    ++frames;
    // 초기 수렴 구간(5초)은 제외한다.
    if (frames > 300) {
      worst_with = std::fmax(worst_with, std::fabs(with_resync.beat() - true_beat));
      worst_without =
          std::fmax(worst_without, std::fabs(integrate_only.beat() - true_beat));
    }
  }

  CHECK(frames > 1000);  // 실제로 꽤 연주했다
  // 적분만 하면 3% × 경과 박만큼 뒤처진다.
  CHECK(worst_without > 3.0);
  // 재동기화가 그보다 훨씬 잘 붙어 있다.
  CHECK(worst_with < worst_without * 0.5);
  // 그리고 한 시스템(16박) 안이다 — V6 기준.
  const double system_beats =
      geom.systems()[0].beat_end - geom.systems()[0].beat_start;
  CHECK(worst_with < system_beats);
}

// 보정이 떨림을 만들지 않는다. 템포가 정확할 때 재동기화를 켜도 적분만 한
// 경우보다 나빠지지 않아야 한다 — 상보 필터가 해를 끼치지 않는다는 조건이다.
NOTA_TEST(PositionResyncDoesNotAddJitterWhenTempoIsCorrect) {
  const ScoreGeometry geom = MakeVariedGeometry(12, 4);
  const double tempo = 100.0;
  const double ink_per_onset = 120.0;
  PositionEstimatorParams params;
  params.initial_ink_per_onset = ink_per_onset;

  PositionEstimator est(geom, params);
  est.Reset(0.0, tempo);
  const double dt = 1.0 / 60.0;
  double true_beat = 0.0;
  const double window_beats = (tempo / 60.0) * params.resync_window_seconds;

  double worst = 0.0;
  int frames = 0;
  while (true_beat < geom.total_beats() - window_beats - 1.0 && frames < 20000) {
    true_beat += (tempo / 60.0) * dt;
    const int onsets = SynthOnsets(geom, true_beat, window_beats, ink_per_onset);
    est.Update(dt, tempo, true, onsets);
    ++frames;
    if (frames > 300) worst = std::fmax(worst, std::fabs(est.beat() - true_beat));
  }
  // 1박 미만. 템포가 맞는데 보정이 위치를 흔들면 이 값이 커진다.
  CHECK(worst < 1.0);
}

// 급변 억제 (6.3)를 위한 악보. 한 곳만 아주 빽빽하다 — 그 밀도에 해당하는
// 온셋 수를 주면 매칭이 그 자리를 유일하게 가리킨다.
namespace {

ScoreGeometry MakeSpikeGeometry(int spike_system) {
  PageAnalysis page;
  const int systems = 12;
  for (int i = 0; i < systems; ++i) {
    nota::System sys;
    sys.staff_count = 2;
    sys.y_top = 100.0 + 240.0 * i;
    sys.y_bottom = sys.y_top + 120.0;
    sys.x_start = 80.0;
    sys.x_end = 920.0;
    sys.staff_space = 12.0;
    page.systems.push_back(sys);
    for (int m = 0; m < 4; ++m) {
      nota::Measure measure;
      measure.index = static_cast<int>(page.measures.size());
      measure.system_index = i;
      measure.x_start = sys.x_start + 210.0 * m;
      measure.x_end = measure.x_start + 210.0;
      measure.y_top = sys.y_top;
      measure.y_bottom = sys.y_bottom;
      measure.ink_density = (i == spike_system) ? 6000 : 60;
      page.measures.push_back(measure);
    }
  }
  page.quality.dominant_staff_count = 2;
  page.quality.consistent_systems = systems;
  page.quality.total_systems = systems;
  ScoreGeometryParams gp;
  gp.trim_page_margins = false;
  return ScoreGeometry::Build({page}, {3000}, gp);
}

// 넓은 탐색을 쓰는 설정. 기본값(±2마디)으로는 점프 임계에 닿을 수 없으므로
// (position_estimator.hpp), 이 경로를 시험하려면 범위를 넓혀야 한다.
PositionEstimatorParams WideSearchParams() {
  PositionEstimatorParams params;
  params.search_measures = 24.0;  // 곡 전체를 본다
  params.search_samples = 201;
  params.resync_interval_seconds = 0.0;
  params.initial_ink_per_onset = 120.0;
  params.calibration_warmup_onsets = 1000000;  // 보정 끔 — 점프 논리만 본다
  return params;
}

}  // namespace

// 기본 설정에서는 급변 억제가 발동하지 않는다. 6.2(±2마디 탐색)와 6.3(1시스템
// 임계)이 서로 닿지 않기 때문이다. 측정하다 드러난 설계 모순을 고정한다.
NOTA_TEST(PositionJumpSuppressionIsUnreachableWithDefaultSearch) {
  const ScoreGeometry geom = MakeSpikeGeometry(6);
  PositionEstimatorParams params;  // 기본값
  params.resync_interval_seconds = 0.0;
  params.initial_ink_per_onset = 120.0;
  params.calibration_warmup_onsets = 1000000;
  PositionEstimator est(geom, params);

  const double system_beats =
      geom.systems()[0].beat_end - geom.systems()[0].beat_start;
  const double beats_per_measure = geom.BeatsPerMeasureAt(0.0);
  // 탐색 반경이 점프 임계보다 작다는 것이 모순의 정체다.
  CHECK(params.search_measures * beats_per_measure <
        params.jump_threshold_systems * system_beats);

  est.Reset(system_beats * 2.0, 0.0);
  const double tempo = 100.0;
  const double window_beats = (tempo / 60.0) * params.resync_window_seconds;
  const int far_onsets =
      SynthOnsets(geom, system_beats * 6.5, window_beats, 120.0);

  // 아무리 멀리 가리켜도 jump_pending이 서지 않는다 — 가리킬 수 있는 거리가
  // 임계보다 짧다.
  for (int i = 0; i < 50; ++i) {
    const PositionState s = est.Update(1e-6, tempo, true, far_onsets);
    CHECK(!s.jump_pending);
    CHECK_EQ(s.jump_streak, 0);
  }
}

// 탐색이 넓을 때, 단발 큰 점프는 보류된다 (6.3).
NOTA_TEST(PositionSuppressesSingleLargeJump) {
  const ScoreGeometry geom = MakeSpikeGeometry(6);
  const PositionEstimatorParams params = WideSearchParams();
  PositionEstimator est(geom, params);

  const double system_beats =
      geom.systems()[0].beat_end - geom.systems()[0].beat_start;
  const double start = system_beats * 2.0;
  est.Reset(start, 0.0);
  const double tempo = 100.0;
  const double window_beats = (tempo / 60.0) * params.resync_window_seconds;

  // 멀리(스파이크)와 가까이(평탄)를 번갈아 준다 — 방향 일관성이 깨지므로
  // 연속 증거가 쌓이지 않는다.
  const int far = SynthOnsets(geom, system_beats * 6.5, window_beats, 120.0);
  const int near = SynthOnsets(geom, start, window_beats, 120.0);
  bool applied_jump = false;
  for (int i = 0; i < 40; ++i) {
    const PositionState s = est.Update(1e-6, tempo, true, (i % 2 == 0) ? far : near);
    if (s.resynced &&
        std::fabs(s.resync_target - start) >= system_beats) {
      applied_jump = true;
    }
  }
  CHECK(!applied_jump);
  // 위치는 거의 그대로다 — 단발 오추정이 악보를 날려버리지 않았다.
  CHECK(std::fabs(est.beat() - start) < system_beats);
}

// 같은 방향으로 연속 3회 나타나면 허용된다 (6.3).
NOTA_TEST(PositionAllowsConsistentJumpAfterThreeObservations) {
  const ScoreGeometry geom = MakeSpikeGeometry(6);
  const PositionEstimatorParams params = WideSearchParams();
  PositionEstimator est(geom, params);

  const double system_beats =
      geom.systems()[0].beat_end - geom.systems()[0].beat_start;
  const double start = system_beats * 2.0;
  est.Reset(start, 0.0);
  const double tempo = 100.0;
  const double window_beats = (tempo / 60.0) * params.resync_window_seconds;
  const int far = SynthOnsets(geom, system_beats * 6.5, window_beats, 120.0);

  int max_streak = 0;
  int updates_until_applied = 0;
  double shift = 0.0;
  bool applied = false;
  for (int i = 0; i < 20 && !applied; ++i) {
    const PositionState s = est.Update(1e-6, tempo, true, far);
    max_streak = std::max(max_streak, s.jump_streak);
    ++updates_until_applied;
    if (s.resynced) {
      applied = true;
      shift = s.resync_shift;
    }
  }
  CHECK(applied);
  // 2회까지는 보류되고 3회째에 적용된다.
  CHECK_EQ(updates_until_applied, params.jump_consistency_required);
  CHECK(max_streak >= params.jump_consistency_required - 1);
  // 보정은 스파이크 쪽(앞)으로 향하고, β 혼합이므로 한 번에 다 가지 않는다.
  CHECK(shift > 0.0);
  CHECK(shift < (system_beats * 6.5 - start) * 0.5);
}

// 환산 보정. 잉크/온셋 상수를 모르는 상태에서 스스로 찾아간다.
//
// 이 상수는 설계에 비어 있던 부분이다 (position_estimator.hpp).
NOTA_TEST(PositionCalibratesInkPerOnset) {
  const ScoreGeometry geom = MakeVariedGeometry(14, 4);
  const double tempo = 100.0;
  const double true_ink_per_onset = 300.0;  // 실제 악보는 조판이 커서 잉크가 많다

  PositionEstimatorParams params;
  params.initial_ink_per_onset = 120.0;  // 초기값은 2.5배 작다
  PositionEstimator est(geom, params);
  est.Reset(0.0, tempo);
  CHECK_NEAR(est.ink_per_onset(), 120.0, 1e-9);

  const double dt = 1.0 / 60.0;
  const double window_beats = (tempo / 60.0) * params.resync_window_seconds;
  double true_beat = 0.0;
  int frames = 0;
  while (true_beat < geom.total_beats() - window_beats - 1.0 && frames < 20000) {
    true_beat += (tempo / 60.0) * dt;
    const int onsets = SynthOnsets(geom, true_beat, window_beats, true_ink_per_onset);
    est.Update(dt, tempo, true, onsets);
    ++frames;
  }

  // 참값 쪽으로 분명히 움직였다.
  CHECK(est.ink_per_onset() > 200.0);
  // 그리고 참값을 크게 넘지 않았다.
  CHECK(est.ink_per_onset() < true_ink_per_onset * 1.5);
  // 위치도 유지된다 — 환산이 틀린 동안에도 드리프트하지 않았다.
  CHECK(std::fabs(est.beat() - true_beat) < 4.0);
}

// 환산 보정이 범위를 벗어나지 않는다. 한 번의 이상 관측이 날려버리면 안 된다.
NOTA_TEST(PositionCalibrationIsBounded) {
  const ScoreGeometry geom = MakeVariedGeometry(14, 4);
  PositionEstimatorParams params;
  params.initial_ink_per_onset = 120.0;
  params.calibration_warmup_onsets = 1;
  PositionEstimator est(geom, params);
  est.Reset(0.0, 100.0);

  const double dt = 1.0 / 60.0;
  // 온셋이 미친 수로 들어온다 (검출기 폭주).
  for (int i = 0; i < 3000; ++i) est.Update(dt, 100.0, true, 100000);
  CHECK(est.ink_per_onset() >= 120.0 * params.calibration_min_ratio - 1e-9);
  CHECK(est.ink_per_onset() <= 120.0 * params.calibration_max_ratio + 1e-9);

  // 반대로 온셋이 전혀 없다고 들어와도 범위 안이다.
  PositionEstimator est2(geom, params);
  est2.Reset(0.0, 100.0);
  for (int i = 0; i < 3000; ++i) est2.Update(dt, 100.0, true, 0);
  CHECK(est2.ink_per_onset() >= 120.0 * params.calibration_min_ratio - 1e-9);
  CHECK(est2.ink_per_onset() <= 120.0 * params.calibration_max_ratio + 1e-9);
}

// 정지 중에는 재동기화하지 않는다. 창 안의 온셋 수가 0으로 떨어지므로 "잉크가
// 없는 곳"으로 끌려가기 때문이다.
NOTA_TEST(PositionDoesNotResyncWhileStopped) {
  const ScoreGeometry geom = MakeVariedGeometry();
  PositionEstimatorParams params;
  params.resync_interval_seconds = 0.0;
  PositionEstimator est(geom, params);
  est.Reset(30.0, 100.0);
  const double before = est.beat();

  const double dt = 1.0 / 60.0;
  for (int i = 0; i < 300; ++i) {
    const PositionState s = est.Update(dt, 100.0, false, 0);
    CHECK(!s.resynced);
  }
  CHECK_NEAR(est.beat(), before, 1e-9);
}

// 온셋 수를 모르면(-1) 재동기화를 건너뛴다. 온셋 검출기가 아직 없는 단계에서도
// 적분만으로 동작해야 한다.
NOTA_TEST(PositionSkipsResyncWhenOnsetsUnknown) {
  const ScoreGeometry geom = MakeVariedGeometry();
  PositionEstimatorParams params;
  params.resync_interval_seconds = 0.0;
  PositionEstimator est(geom, params);
  est.Reset(0.0, 120.0);
  const double dt = 1.0 / 60.0;
  for (int i = 0; i < 600; ++i) {
    const PositionState s = est.Update(dt, 120.0, true, -1);
    CHECK(!s.resynced);
    CHECK_NEAR(s.confidence, 0.0, 1e-12);
  }
  CHECK_NEAR(est.beat(), 20.0, 1e-6);
}

// 마디가 검출되지 않은 악보에서는 재동기화가 성립하지 않는다. 그래도 적분은
// 돌아야 한다 — 시스템 골격만으로도 스크롤은 된다 (DESIGN.md 6절).
NOTA_TEST(PositionWorksWithoutMeasures) {
  PageAnalysis page;
  for (int i = 0; i < 6; ++i) {
    nota::System sys;
    sys.staff_count = 2;
    sys.y_top = 100.0 + 240.0 * i;
    sys.y_bottom = sys.y_top + 120.0;
    sys.x_start = 80.0;
    sys.x_end = 920.0;
    sys.staff_space = 12.0;
    page.systems.push_back(sys);
  }
  ScoreGeometryParams gp;
  gp.trim_page_margins = false;
  const ScoreGeometry geom = ScoreGeometry::Build({page}, {2000}, gp);
  CHECK(geom.measures().empty());

  PositionEstimatorParams params;
  params.resync_interval_seconds = 0.0;
  PositionEstimator est(geom, params);
  est.Reset(0.0, 120.0);
  const double dt = 1.0 / 60.0;
  for (int i = 0; i < 300; ++i) {
    const PositionState s = est.Update(dt, 120.0, true, 20);
    CHECK(!s.resynced);
  }
  CHECK_NEAR(est.beat(), 10.0, 1e-6);
}

// 수동 개입으로 위치를 인수한다 (7.5 → ScrollState::position_handover).
NOTA_TEST(PositionAcceptsManualHandover) {
  const ScoreGeometry geom = MakeVariedGeometry();
  PositionEstimator est(geom);
  est.Reset(0.0, 120.0);
  const double dt = 1.0 / 60.0;
  for (int i = 0; i < 300; ++i) est.Update(dt, 120.0, true, -1);
  CHECK_NEAR(est.beat(), 10.0, 1e-6);

  est.SetPosition(60.0);
  CHECK_NEAR(est.beat(), 60.0, 1e-9);
  // 템포는 유지된다 — 자리를 옮긴 것이 템포를 바꾼 것은 아니다.
  CHECK_NEAR(est.tempo_bpm(), 120.0, 1e-9);
  // 그 자리에서 적분이 이어진다.
  for (int i = 0; i < 60; ++i) est.Update(dt, 120.0, true, -1);
  CHECK_NEAR(est.beat(), 62.0, 1e-6);
}

// 비정상 dt에서 상태가 깨지지 않는다.
NOTA_TEST(PositionIgnoresInvalidDt) {
  const ScoreGeometry geom = MakeVariedGeometry();
  PositionEstimator est(geom);
  est.Reset(8.0, 120.0);
  for (double dt : {0.0, -1.0, std::nan("")}) {
    const PositionState s = est.Update(dt, 120.0, true, 20);
    CHECK_NEAR(s.beat, 8.0, 1e-12);
    CHECK(!s.resynced);
  }
}

// 빈 기하에서 조용히 돈다.
NOTA_TEST(PositionHandlesEmptyGeometry) {
  const ScoreGeometry empty;
  PositionEstimator est(empty);
  est.Reset(0.0, 120.0);
  const double dt = 1.0 / 60.0;
  for (int i = 0; i < 120; ++i) {
    const PositionState s = est.Update(dt, 120.0, true, 20);
    CHECK(std::isfinite(s.beat));
    CHECK(!s.resynced);
  }
  // 기하가 없으면 범위 제한이 없으므로 적분만 한 값이다.
  CHECK_NEAR(est.beat(), 4.0, 1e-6);
}

// 균일한 악보에서는 매칭이 스스로 기권한다.
//
// 상보 필터의 조건이다 — 정보가 없을 때 아무 말도 하지 않아야 적분을 해치지
// 않는다. 실측에서 이것이 깨졌을 때 Bach BWV846의 박 오차가 적분만 한 4.08박
// 에서 87.37박(7시스템)으로 폭주했다 (ALGORITHM.md 8.5).
NOTA_TEST(PositionAbstainsOnUniformScore) {
  // 모든 마디의 잉크가 같은 악보. 균일한 분산화음(Bach BWV846)이 이렇다.
  PageAnalysis page;
  const int systems = 12;
  for (int i = 0; i < systems; ++i) {
    nota::System sys;
    sys.staff_count = 2;
    sys.y_top = 100.0 + 240.0 * i;
    sys.y_bottom = sys.y_top + 120.0;
    sys.x_start = 80.0;
    sys.x_end = 920.0;
    sys.staff_space = 12.0;
    page.systems.push_back(sys);
    for (int m = 0; m < 4; ++m) {
      nota::Measure measure;
      measure.index = static_cast<int>(page.measures.size());
      measure.system_index = i;
      measure.x_start = sys.x_start + 210.0 * m;
      measure.x_end = measure.x_start + 210.0;
      measure.y_top = sys.y_top;
      measure.y_bottom = sys.y_bottom;
      measure.ink_density = 6000;  // 전부 같다
      page.measures.push_back(measure);
    }
  }
  page.quality.dominant_staff_count = 2;
  page.quality.consistent_systems = systems;
  page.quality.total_systems = systems;
  ScoreGeometryParams gp;
  gp.trim_page_margins = false;
  const ScoreGeometry geom = ScoreGeometry::Build({page}, {3000}, gp);

  const double tempo = 100.0;
  const double ink_per_onset = 120.0;
  PositionEstimatorParams params;
  params.initial_ink_per_onset = ink_per_onset;

  PositionEstimator est(geom, params);
  est.Reset(0.0, tempo);
  const double dt = 1.0 / 60.0;
  const double window_beats = (tempo / 60.0) * params.resync_window_seconds;
  double true_beat = 0.0;
  int resyncs = 0;
  int frames = 0;
  while (true_beat < geom.total_beats() - window_beats - 1.0 && frames < 20000) {
    true_beat += (tempo / 60.0) * dt;
    const int onsets = SynthOnsets(geom, true_beat, window_beats, ink_per_onset);
    if (est.Update(dt, tempo, true, onsets).resynced) ++resyncs;
    ++frames;
  }
  // 거의 전부 기권한다. 재동기화 주기 0.5초이므로 기회는 frames/30회 있었다.
  const int opportunities = frames / 30;
  CHECK(opportunities > 50);
  CHECK(resyncs * 10 < opportunities);  // 10% 미만
  // 그리고 위치가 밀려나지 않았다 — 템포가 맞으므로 적분만으로 정확하다.
  CHECK(std::fabs(est.beat() - true_beat) < 1.0);
}

// 텍스처가 다양한 악보에서는 매칭이 일한다. 위 테스트의 대조군이다 —
// 기권이 "항상 기권"이면 그것도 고장이다.
NOTA_TEST(PositionResyncsOnVariedScore) {
  const ScoreGeometry geom = MakeVariedGeometry(12, 4);
  const double tempo = 100.0;
  const double ink_per_onset = 120.0;
  PositionEstimatorParams params;
  params.initial_ink_per_onset = ink_per_onset;

  PositionEstimator est(geom, params);
  est.Reset(0.0, tempo);
  const double dt = 1.0 / 60.0;
  const double window_beats = (tempo / 60.0) * params.resync_window_seconds;
  double true_beat = 0.0;
  int resyncs = 0;
  int frames = 0;
  while (true_beat < geom.total_beats() - window_beats - 1.0 && frames < 20000) {
    true_beat += (tempo / 60.0) * dt;
    const int onsets = SynthOnsets(geom, true_beat, window_beats, ink_per_onset);
    if (est.Update(dt, tempo, true, onsets).resynced) ++resyncs;
    ++frames;
  }
  const int opportunities = frames / 30;
  CHECK(opportunities > 50);
  // 기회의 20% 이상에서 보정이 일어난다.
  CHECK(resyncs * 5 > opportunities);
}

// 탐색 범위의 끝이 뽑히면 믿지 않는다.
//
// 끝이 뽑히는 것은 "진짜 최소는 범위 밖"이라는 뜻이고, 환산이 조금만 틀려도
// 매번 같은 쪽 끝이 뽑혀 편향이 누적된다 (ALGORITHM.md 8.5).
NOTA_TEST(PositionRejectsEdgeMinimum) {
  const ScoreGeometry geom = MakeVariedGeometry(12, 4);
  PositionEstimatorParams params;
  params.resync_interval_seconds = 0.0;
  params.initial_ink_per_onset = 120.0;
  params.calibration_warmup_onsets = 1000000;  // 환산을 일부러 틀린 채 둔다
  PositionEstimator est(geom, params);
  est.Reset(geom.total_beats() * 0.5, 0.0);

  // 어떤 후보와도 맞지 않는 아주 큰 온셋 수 → 최소는 범위 끝에서 잡힌다.
  for (int i = 0; i < 50; ++i) {
    const PositionState s = est.Update(1e-6, 100.0, true, 100000);
    CHECK_NEAR(s.confidence, 0.0, 1e-12);
    CHECK(!s.resynced);
  }
  CHECK_NEAR(est.beat(), geom.total_beats() * 0.5, 1e-3);
}

// 박은 **항상** 곡 범위 안에 있다. Reset/SetPosition 직후도 포함한다.
//
// 퍼징이 잡은 버그의 회귀 테스트다. 처음에는 Update 안에서만 범위를 묶었고,
// Reset으로 범위 밖 값을 넣은 뒤 비정상 dt로 Update가 조기 반환하면 그 값이
// 그대로 나왔다.
NOTA_TEST(PositionClampsOnResetAndSetPosition) {
  const ScoreGeometry geom = MakeVariedGeometry();
  PositionEstimator est(geom);

  est.Reset(1e6, 120.0);
  CHECK_NEAR(est.beat(), geom.total_beats(), 1e-9);
  // 비정상 dt로 조기 반환해도 범위 안이다.
  CHECK_NEAR(est.Update(-1.0, 120.0, true, 20).beat, geom.total_beats(), 1e-9);

  est.Reset(-1e6, 120.0);
  CHECK_NEAR(est.beat(), 0.0, 1e-9);
  CHECK_NEAR(est.Update(0.0, 120.0, true, 20).beat, 0.0, 1e-9);

  est.SetPosition(1e9);
  CHECK_NEAR(est.beat(), geom.total_beats(), 1e-9);
  est.SetPosition(-1e9);
  CHECK_NEAR(est.beat(), 0.0, 1e-9);

  // NaN도 받아낸다.
  est.SetPosition(std::nan(""));
  CHECK(std::isfinite(est.beat()));
  est.Reset(std::nan(""), std::nan(""));
  CHECK(std::isfinite(est.beat()));
  CHECK(std::isfinite(est.tempo_bpm()));
  CHECK_NEAR(est.Update(1.0 / 60.0, std::nan(""), true, 20).beat, 0.0, 1e-9);
}
