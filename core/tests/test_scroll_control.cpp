// 스크롤 제어 테스트.
//
// 입력이 숫자뿐이므로(박, 템포, dt) 오디오도 이미지도 없이 전부 확인된다.
// 확인하는 성질:
//   1. 등속 추적  — 피드포워드가 정상상태 오차를 없앤다
//   2. 오버슈트 없음 — 재동기화 점프에서 지나쳤다 되돌아오지 않는다 (ζ=1)
//   3. 속도 제한  — 큰 점프에서도 화면이 날아가지 않는다
//   4. 수동 개입  — 스프링이 손을 이기지 않고, 손이 맞춘 위치를 인수한다
//   5. dt 견고성  — 프레임이 길어지거나 들쭉날쭉해도 발산하지 않는다

#include <cmath>
#include <vector>

#include "nota/score_geometry.hpp"
#include "nota/scroll_control.hpp"
#include "test_framework.hpp"

namespace {

using nota::PageAnalysis;
using nota::ScoreGeometry;
using nota::ScoreGeometryParams;
using nota::ScrollControlParams;
using nota::ScrollController;
using nota::ScrollState;

constexpr double kViewport = 900.0;

// 균일한 가짜 페이지. test_score_geometry.cpp와 같은 모양이되, 여기서는 기하가
// 아니라 제어기를 보는 것이므로 시스템당 마디 수를 고정한다.
PageAnalysis MakePage(int system_count, int measures_per_system) {
  PageAnalysis page;
  for (int i = 0; i < system_count; ++i) {
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
      measure.ink_density = 100;
      page.measures.push_back(measure);
    }
  }
  page.quality.dominant_staff_count = 2;
  page.quality.consistent_systems = system_count;
  page.quality.total_systems = system_count;
  return page;
}

ScoreGeometry MakeGeometry(int systems = 12, int measures_per_system = 4) {
  // 여백 잘라내기는 기본이 켜져 있다. 여기서는 제어기만 보려고 끈다 — 캔버스
  // 좌표를 손으로 따라가기 쉽게 하기 위해서다.
  ScoreGeometryParams params;
  params.trim_page_margins = false;
  return ScoreGeometry::Build({MakePage(systems, measures_per_system)}, {3000},
                              params);
}

}  // namespace

// 등속 연주를 추적한다. 피드포워드가 있으면 정상상태 오차가 사라진다.
NOTA_TEST(ScrollTracksSteadyTempoWithoutLag) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);

  const double tempo = 90.0;  // BPM
  const double dt = 1.0 / 60.0;
  double beat = 0.0;
  ctl.Reset(beat);

  // 10초 연주. 60 FPS.
  double max_error_after_settle = 0.0;
  for (int frame = 0; frame < 600; ++frame) {
    beat += (tempo / 60.0) * dt;
    const ScrollState s = ctl.Update(dt, beat, tempo);
    const double target = geom.CanvasY(beat) - ctl.read_line();
    // 처음 1초는 수렴 구간이므로 제외한다.
    if (frame > 60) {
      max_error_after_settle = std::fmax(max_error_after_settle,
                                         std::fabs(s.offset - target));
    }
  }
  // 1 px 미만. 읽기 선이 300 px인 화면에서 눈에 보이지 않는 수준이다.
  CHECK(max_error_after_settle < 1.0);
}

// 피드포워드를 빼면 정상상태 오차가 남는다. 7.3절이 그 항을 두는 이유를 고정한다.
NOTA_TEST(ScrollWithoutFeedforwardLagsBehind) {
  const ScoreGeometry geom = MakeGeometry();

  // tempo_bpm을 0으로 주면 피드포워드가 꺼진다 — 스프링만 남는 경우와 같다.
  ScrollController no_ff(geom, kViewport);
  ScrollController with_ff(geom, kViewport);
  const double tempo = 90.0;
  const double dt = 1.0 / 60.0;
  double beat = 0.0;
  no_ff.Reset(0.0);
  with_ff.Reset(0.0);

  for (int frame = 0; frame < 600; ++frame) {
    beat += (tempo / 60.0) * dt;
    no_ff.Update(dt, beat, 0.0);
    with_ff.Update(dt, beat, tempo);
  }
  const double target = geom.CanvasY(beat) - with_ff.read_line();
  const double lag_without = std::fabs(no_ff.offset() - target);
  const double lag_with = std::fabs(with_ff.offset() - target);

  // 뒤처짐의 해석값은 2ζv/ω다. 목표가 등속 v로 움직일 때 스프링이 그 속도를
  // 내려면 ω²·e = 2ζω·v를 만족하는 변위 e를 유지해야 하기 때문이다.
  // 이 기하에서 시스템 피치 240 px, 시스템당 16박 → 15 px/박.
  const double v = (tempo / 60.0) * 15.0;
  const double analytic_lag = 2.0 * 1.0 * v / ScrollControlParams().omega;
  CHECK_NEAR(lag_without, analytic_lag, 0.5);
  CHECK(lag_with < 1.0);
  CHECK(lag_without > lag_with * 10.0);
}

// 재동기화 점프에서 오버슈트가 없다. ζ=1을 두는 이유다 (7.2).
NOTA_TEST(ScrollDoesNotOvershootOnResyncJump) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);

  // 10박 지점에 정렬해 둔 뒤, 위치 추정이 갑자기 30박으로 점프한다.
  ctl.Reset(10.0);
  const double jump_beat = 30.0;
  const double target = geom.CanvasY(jump_beat) - ctl.read_line();
  CHECK(target > ctl.offset());

  const double dt = 1.0 / 60.0;
  double max_offset = ctl.offset();
  for (int frame = 0; frame < 600; ++frame) {
    // 템포 0 — 피드포워드 없이 순수한 스프링 응답을 본다.
    const ScrollState s = ctl.Update(dt, jump_beat, 0.0);
    max_offset = std::fmax(max_offset, s.offset);
  }
  // 목표를 넘어서지 않는다. 수치 적분 오차만 허용한다.
  CHECK(max_offset <= target + 0.5);
  // 그리고 수렴한다.
  CHECK_NEAR(ctl.offset(), target, 0.5);
}

// 속도 제한. 큰 점프에서도 화면 높이의 1.5배/초를 넘지 않는다 (7.4).
NOTA_TEST(ScrollRespectsSpeedLimit) {
  const ScoreGeometry geom = MakeGeometry(30, 4);
  ScrollController ctl(geom, kViewport);
  ctl.Reset(0.0);

  const double dt = 1.0 / 60.0;
  const double v_max = kViewport * 1.5;
  double max_speed = 0.0;
  double max_step = 0.0;
  double prev = ctl.offset();
  // 곡의 거의 끝으로 점프.
  const double far = geom.total_beats() - 1.0;
  for (int frame = 0; frame < 1200; ++frame) {
    const ScrollState s = ctl.Update(dt, far, 240.0);
    max_speed = std::fmax(max_speed, std::fabs(s.velocity));
    max_step = std::fmax(max_step, std::fabs(s.offset - prev));
    prev = s.offset;
  }
  CHECK(max_speed <= v_max + 1e-6);
  // 한 프레임의 이동도 속도 제한을 따른다.
  CHECK(max_step <= v_max * dt + 1e-6);
  // 제한이 걸렸는지 확인한다 — 걸리지 않았다면 이 테스트는 아무것도 보지 않는다.
  CHECK(max_speed > v_max * 0.9);
}

// 화면 높이가 바뀌면 속도 상한도 함께 바뀐다.
NOTA_TEST(ScrollSpeedLimitFollowsViewport) {
  const ScoreGeometry geom = MakeGeometry(30, 4);
  ScrollController small(geom, 300.0);
  ScrollController big(geom, 1200.0);
  small.Reset(0.0);
  big.Reset(0.0);

  const double dt = 1.0 / 60.0;
  const double far = geom.total_beats() - 1.0;
  double small_max = 0.0;
  double big_max = 0.0;
  for (int frame = 0; frame < 600; ++frame) {
    small_max = std::fmax(small_max, std::fabs(small.Update(dt, far, 240.0).velocity));
    big_max = std::fmax(big_max, std::fabs(big.Update(dt, far, 240.0).velocity));
  }
  CHECK(small_max <= 300.0 * 1.5 + 1e-6);
  CHECK(big_max > small_max * 2.0);
}

// 수동 개입: 스프링이 손을 이기지 않고, 손이 맞춘 위치를 인수한다 (7.5).
NOTA_TEST(ScrollManualInterventionHandsOverPosition) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  ctl.Reset(4.0);

  const double dt = 1.0 / 60.0;
  // 사용자가 화면을 잡고 20박 지점으로 끌어 놓는다.
  ctl.BeginManual();
  CHECK(ctl.manual());
  const double dragged = geom.CanvasY(20.0) - ctl.read_line();
  for (int frame = 0; frame < 30; ++frame) {
    ctl.SetManualOffset(dragged);
    // 이 사이 위치 추정기는 여전히 4박 근처를 말하고 있다. 스프링이 살아
    // 있으면 화면이 손에서 끌려 돌아간다.
    const ScrollState s = ctl.Update(dt, 4.0, 90.0);
    CHECK(s.manual);
    CHECK_NEAR(s.offset, dragged, 1e-9);
  }

  // 손을 뗀다. settle 시간(0.3초) 동안은 그 자리에 머문다.
  ctl.EndManual();
  bool handed_over = false;
  double handover_beat = 0.0;
  int frames_until_handover = 0;
  for (int frame = 0; frame < 60; ++frame) {
    const ScrollState s = ctl.Update(dt, 4.0, 90.0);
    if (!handed_over) {
      CHECK_NEAR(s.offset, dragged, 1e-9);  // 끌려 돌아가지 않는다
      ++frames_until_handover;
    }
    if (s.position_handover) {
      handed_over = true;
      handover_beat = s.handover_beat;
      break;
    }
  }
  CHECK(handed_over);
  // 0.3초 = 18프레임. ±1프레임 허용.
  CHECK(frames_until_handover >= 18);
  CHECK(frames_until_handover <= 20);
  // 인수된 박은 사용자가 맞춘 위치다.
  CHECK_NEAR(handover_beat, 20.0, 0.5);
  // 인수는 한 번만 일어난다.
  for (int frame = 0; frame < 30; ++frame) {
    CHECK(!ctl.Update(dt, handover_beat, 90.0).position_handover);
  }
}

// 수동 개입 중 다시 터치하면 대기가 취소된다. 연달아 끄는 동작에서 중간에
// 위치가 인수되면 안 된다.
NOTA_TEST(ScrollSecondTouchCancelsSettle) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  ctl.Reset(4.0);
  const double dt = 1.0 / 60.0;

  ctl.BeginManual();
  ctl.SetManualOffset(geom.CanvasY(20.0) - ctl.read_line());
  ctl.EndManual();
  // 0.1초만 지나고 다시 잡는다.
  for (int frame = 0; frame < 6; ++frame) {
    CHECK(!ctl.Update(dt, 4.0, 90.0).position_handover);
  }
  ctl.BeginManual();
  ctl.SetManualOffset(geom.CanvasY(28.0) - ctl.read_line());
  for (int frame = 0; frame < 30; ++frame) {
    CHECK(!ctl.Update(dt, 4.0, 90.0).position_handover);
  }
  // 두 번째로 손을 떼면 그때 인수된다 — 두 번째 위치로.
  ctl.EndManual();
  double beat = 0.0;
  bool got = false;
  for (int frame = 0; frame < 60 && !got; ++frame) {
    const ScrollState s = ctl.Update(dt, 4.0, 90.0);
    if (s.position_handover) {
      got = true;
      beat = s.handover_beat;
    }
  }
  CHECK(got);
  CHECK_NEAR(beat, 28.0, 0.5);
}

// dt 견고성. 프레임이 길어지거나 들쭉날쭉해도 발산하지 않고, 작은 dt의 결과와
// 비슷한 곳에 도달한다.
NOTA_TEST(ScrollIsStableUnderIrregularDt) {
  const ScoreGeometry geom = MakeGeometry();
  const double tempo = 100.0;

  // 기준: 고정 1/120초.
  ScrollController fine(geom, kViewport);
  fine.Reset(0.0);
  double beat = 0.0;
  for (int i = 0; i < 1200; ++i) {
    beat += (tempo / 60.0) / 120.0;
    fine.Update(1.0 / 120.0, beat, tempo);
  }

  // 비교: 들쭉날쭉한 dt (프레임 드롭, 긴 멈춤 포함). 총 경과 시간은 같다.
  ScrollController rough(geom, kViewport);
  rough.Reset(0.0);
  const double steps[] = {1.0 / 60.0, 1.0 / 20.0, 1.0 / 90.0, 0.2, 1.0 / 60.0};
  double elapsed = 0.0;
  double rough_beat = 0.0;
  int k = 0;
  while (elapsed < 10.0) {
    const double dt = steps[k++ % 5];
    elapsed += dt;
    rough_beat += (tempo / 60.0) * dt;
    const ScrollState s = rough.Update(dt, rough_beat, tempo);
    CHECK(std::isfinite(s.offset));
    CHECK(std::isfinite(s.velocity));
  }
  // 두 궤적이 같은 곳에 있다. 박이 약간 다르므로 그 차이를 감안해 비교한다.
  const double fine_target = geom.CanvasY(beat) - fine.read_line();
  const double rough_target = geom.CanvasY(rough_beat) - rough.read_line();
  CHECK(std::fabs(fine.offset() - fine_target) < 1.0);
  CHECK(std::fabs(rough.offset() - rough_target) < 2.0);
}

// 큰 dt를 한 번 주어도 적분이 폭발하지 않는다. max_dt_seconds가 막는다.
NOTA_TEST(ScrollClampsHugeDt) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  ctl.Reset(0.0);

  // 앱이 10분 멈췄다 돌아온 상황.
  const ScrollState s = ctl.Update(600.0, 40.0, 120.0);
  CHECK(std::isfinite(s.offset));
  CHECK(std::isfinite(s.velocity));
  CHECK(std::fabs(s.velocity) <= kViewport * 1.5 + 1e-6);
  // 범위 안에 있다.
  CHECK(s.offset >= ctl.min_offset() - 1e-9);
  CHECK(s.offset <= ctl.max_offset() + 1e-9);
}

// 비정상 dt(0, 음수, NaN)에서 상태가 깨지지 않는다.
NOTA_TEST(ScrollIgnoresInvalidDt) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  ctl.Reset(8.0);
  const double before = ctl.offset();

  for (double dt : {0.0, -1.0, std::nan("")}) {
    const ScrollState s = ctl.Update(dt, 40.0, 120.0);
    CHECK_NEAR(s.offset, before, 1e-12);
  }
  CHECK_NEAR(ctl.offset(), before, 1e-12);
}

// 연주가 멈추면 스크롤도 멈춘다 (V7의 제어기 쪽 절반).
NOTA_TEST(ScrollStopsWhenPlayingStops) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  const double dt = 1.0 / 60.0;
  const double tempo = 120.0;
  double beat = 0.0;
  ctl.Reset(0.0);
  for (int i = 0; i < 300; ++i) {
    beat += (tempo / 60.0) * dt;
    ctl.Update(dt, beat, tempo);
  }
  CHECK(std::fabs(ctl.velocity()) > 10.0);  // 달리고 있었다

  // 정지: 박도 템포도 멈춘다.
  int frames_to_stop = -1;
  for (int i = 0; i < 120; ++i) {
    const ScrollState s = ctl.Update(dt, beat, 0.0);
    if (frames_to_stop < 0 && std::fabs(s.velocity) < 1.0) frames_to_stop = i + 1;
  }
  CHECK(frames_to_stop >= 0);
  // 500 ms(30프레임) 이내에 멈춘다 — V7 기준.
  CHECK(frames_to_stop <= 30);
  CHECK(std::fabs(ctl.velocity()) < 1.0);
}

// 오프셋은 곡의 시작과 끝을 넘지 않는다.
NOTA_TEST(ScrollStaysWithinCanvas) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  ctl.Reset(0.0);
  CHECK_NEAR(ctl.offset(), ctl.min_offset(), 1e-9);

  const double dt = 1.0 / 60.0;
  // 범위를 한참 벗어난 박을 준다.
  for (int i = 0; i < 600; ++i) {
    const ScrollState s = ctl.Update(dt, -500.0, 120.0);
    CHECK(s.offset >= ctl.min_offset() - 1e-9);
  }
  for (int i = 0; i < 1200; ++i) {
    const ScrollState s = ctl.Update(dt, geom.total_beats() + 500.0, 120.0);
    CHECK(s.offset <= ctl.max_offset() + 1e-9);
  }
  CHECK_NEAR(ctl.offset(), ctl.max_offset(), 1e-6);
  // 벽에 붙어 있는 동안 속도가 쌓이지 않는다.
  CHECK_NEAR(ctl.velocity(), 0.0, 1e-9);

  // 수동 개입도 같은 범위를 지킨다.
  ctl.BeginManual();
  ctl.SetManualOffset(-1e9);
  CHECK_NEAR(ctl.offset(), ctl.min_offset(), 1e-9);
  ctl.SetManualOffset(1e9);
  CHECK_NEAR(ctl.offset(), ctl.max_offset(), 1e-9);
}

// 기하가 비어 있으면(검출 실패) 조용히 0을 낸다. 등급 C 폴백으로 넘어가는 동안
// 제어기가 터지지 않아야 한다.
NOTA_TEST(ScrollHandlesEmptyGeometry) {
  const ScoreGeometry empty;
  ScrollController ctl(empty, kViewport);
  CHECK(empty.empty());
  ctl.Reset(10.0);
  const ScrollState s = ctl.Update(1.0 / 60.0, 10.0, 120.0);
  CHECK_NEAR(s.offset, 0.0, 1e-12);
  CHECK_NEAR(s.velocity, 0.0, 1e-12);
  CHECK_NEAR(ctl.min_offset(), 0.0, 1e-12);
  CHECK_NEAR(ctl.max_offset(), 0.0, 1e-12);
}

// 화면 높이가 바뀌어도(회전) 상태가 깨지지 않고 읽기 선이 따라간다.
NOTA_TEST(ScrollAdaptsToViewportChange) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  const double dt = 1.0 / 60.0;
  double beat = 0.0;
  ctl.Reset(0.0);
  for (int i = 0; i < 300; ++i) {
    beat += (120.0 / 60.0) * dt;
    ctl.Update(dt, beat, 120.0);
  }
  CHECK_NEAR(ctl.read_line(), kViewport / 3.0, 1e-9);

  ctl.SetViewportHeight(600.0);
  CHECK_NEAR(ctl.read_line(), 200.0, 1e-9);
  for (int i = 0; i < 300; ++i) {
    beat += (120.0 / 60.0) * dt;
    const ScrollState s = ctl.Update(dt, beat, 120.0);
    CHECK(std::isfinite(s.offset));
  }
  // 새 읽기 선 기준으로 다시 맞춰진다.
  CHECK_NEAR(ctl.offset(), geom.CanvasY(beat) - 200.0, 1.0);
}

// 정지할 때 되돌아오는 양을 고정한다.
//
// ζ=1은 "위치 계단 입력에 오버슈트가 없다"는 뜻이고, **속도가 실린 상태에서는
// 다르다.** 연주가 멈추면 피드포워드가 0으로 떨어지는데 스프링의 속도는 바로
// 죽지 않으므로 목표를 조금 지나쳤다 되돌아온다. 되돌아오는 양의 해석값은
// v/(2ζω)다 — 속도가 1/(2ζω)의 시정수로 지수 감쇠하기 때문이다.
//
// 실측 2.9 px(90 BPM) ~ 6.4 px(200 BPM). 읽기 선 300 px의 1~2%다. ζ를 2로
// 올리면 역행은 40% 줄지만 최대 가속도가 2배가 된다 — 교환이 맞지 않는다
// (ALGORITHM.md 8.4). 그래서 ζ=1을 유지하고 이 값을 상한으로 묶어 둔다.
NOTA_TEST(ScrollReversalOnStopIsBounded) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  const double dt = 1.0 / 60.0;
  const double tempo = 120.0;
  double beat = 0.0;
  ctl.Reset(0.0);
  for (int i = 0; i < 300; ++i) {
    beat += (tempo / 60.0) * dt;
    ctl.Update(dt, beat, tempo);
  }
  const double speed_before = std::fabs(ctl.velocity());
  CHECK(speed_before > 10.0);

  // 정지. 박도 템포도 멈춘다.
  double reversal = 0.0;
  double prev = ctl.offset();
  for (int i = 0; i < 180; ++i) {
    const ScrollState s = ctl.Update(dt, beat, 0.0);
    if (s.offset < prev) reversal += prev - s.offset;
    prev = s.offset;
  }

  const ScrollControlParams defaults;
  const double analytic = speed_before / (2.0 * defaults.zeta * defaults.omega);
  CHECK(reversal <= analytic * 1.2);
  // 그리고 눈에 보이지 않는 수준이다 — 읽기 선의 5% 미만.
  CHECK(reversal < ctl.read_line() * 0.05);
}

// 재동기화 점프를 스프링이 흡수한다. 목표를 그대로 따랐을 때의 즉각적인
// 점프와 대비해 고정한다 (7.2절이 스프링을 두는 이유).
NOTA_TEST(ScrollAbsorbsResyncJumpWithinSpeedLimit) {
  const ScoreGeometry geom = MakeGeometry();
  ScrollController ctl(geom, kViewport);
  const double dt = 1.0 / 60.0;
  const double tempo = 90.0;
  double beat = 20.0;
  ctl.Reset(beat);

  // 8박 점프 — 이 기하에서 시스템 하나(16박)의 절반이다. 6.3절은 한 시스템
  // 이상의 점프를 억제하므로, 이것이 제어기가 실제로 받는 크기다.
  const double jump = 8.0;
  beat += jump;
  const double target = geom.CanvasY(beat) - ctl.read_line();
  const double instant_jump = std::fabs(target - ctl.offset());
  CHECK(instant_jump > 100.0);  // 목표를 그대로 따르면 이만큼 튄다

  double max_speed = 0.0;
  double max_step = 0.0;
  double prev = ctl.offset();
  double settle_time = -1.0;
  for (int i = 0; i < 600; ++i) {
    beat += (tempo / 60.0) * dt;
    const ScrollState s = ctl.Update(dt, beat, tempo);
    max_speed = std::fmax(max_speed, std::fabs(s.velocity));
    max_step = std::fmax(max_step, std::fabs(s.offset - prev));
    prev = s.offset;
    const double err = std::fabs(s.offset - (geom.CanvasY(beat) - ctl.read_line()));
    if (err > ctl.read_line() * 0.02) settle_time = (i + 1) * dt;
  }
  // 한 프레임에 통째로 튀지 않는다 — 점프가 여러 프레임에 퍼진다.
  CHECK(max_step < instant_jump * 0.2);
  // 속도 상한을 넘지 않는다.
  CHECK(max_speed <= kViewport * 1.5 + 1e-6);
  // 그리고 2초 안에 흡수된다. ω=2π·0.8에서 실측 1.05초 (ALGORITHM.md 8.4).
  CHECK(settle_time > 0.0);
  CHECK(settle_time < 2.0);
}

// 프레임 드롭이 추적 오차를 키우지 않는다. 부분 단계마다 목표를 템포로
// 보간하기 때문이다 — 보간이 없으면 v·dt/2만큼 어긋난다.
NOTA_TEST(ScrollFrameDropsDoNotDegradeTracking) {
  const ScoreGeometry geom = MakeGeometry();
  const double tempo = 100.0;

  const auto run = [&](bool drop) {
    ScrollController ctl(geom, kViewport);
    ctl.Reset(0.0);
    double beat = 0.0;
    double worst = 0.0;
    for (int i = 0; i < 900; ++i) {
      // 10프레임마다 한 번 dt가 4배로 길어진다.
      const double dt = (drop && (i % 10) == 9) ? 4.0 / 60.0 : 1.0 / 60.0;
      beat += (tempo / 60.0) * dt;
      const ScrollState s = ctl.Update(dt, beat, tempo);
      if (i > 120) {
        worst = std::fmax(worst,
                          std::fabs(s.offset - (geom.CanvasY(beat) - ctl.read_line())));
      }
    }
    return worst;
  };

  const double clean = run(false);
  const double dropped = run(true);
  CHECK(clean < 1.0);
  CHECK(dropped < 1.0);
  // 드롭이 있어도 오차가 눈에 띄게 커지지 않는다.
  CHECK(dropped < clean + 0.5);
}
