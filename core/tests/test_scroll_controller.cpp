#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "nota/page_analysis.hpp"
#include "nota/score_geometry.hpp"
#include "nota/scroll_controller.hpp"
#include "synth_score.hpp"
#include "test_framework.hpp"

namespace {

constexpr double kViewport = 900.0;   // 태블릿 세로 화면 정도
constexpr double kDt = 1.0 / 60.0;

// 합성 + 전체 분석은 비싸다. 같은 입력을 여러 테스트가 쓰므로 한 번만 만든다.
// 캐시하지 않으면 스위트가 분 단위로 느려지고, 느린 테스트는 결국 돌리지 않게 된다.
nota::ScoreGeometry MakeGeometry(int systems = 4) {
  static std::vector<std::pair<int, nota::ScoreGeometry>> cache;
  for (const auto& e : cache) {
    if (e.first == systems) return e.second;
  }
  nota_test::SynthParams p;
  p.systems = systems;
  const nota_test::SynthScore score = nota_test::GenerateScore(p);
  const nota::PageAnalysis page = nota::AnalyzePageGray(score.image.view());
  nota::ScoreGeometry g = nota::ScoreGeometry::Build(
      {page, page}, {score.image.height, score.image.height});
  cache.emplace_back(systems, g);
  return g;
}

// 등속 연주를 흉내낸다. 템포에 맞춰 박을 전진시키며 제어기를 돌린다.
struct Sim {
  const nota::ScoreGeometry* geo;
  nota::ScrollController* ctrl;
  double beat = 0.0;
  double tempo = 90.0;

  void Run(double seconds, bool playing = true) {
    const int steps = static_cast<int>(seconds / kDt);
    for (int i = 0; i < steps; ++i) {
      if (playing) beat += (tempo / 60.0) * kDt;
      nota::ScrollInput in;
      in.dt_s = kDt;
      in.beat = beat;
      in.tempo_bpm = tempo;
      in.playing = playing;
      ctrl->Update(in);
    }
  }
};

}  // namespace

NOTA_TEST(ReadLinePositionIsHonoured) {
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(12.0);

  // 연주 위치가 화면 높이의 1/3 지점에 와야 한다.
  const double on_screen = g.CanvasY(12.0) - c.offset();
  CHECK_NEAR(on_screen, kViewport / 3.0, 0.01);
}

NOTA_TEST(CriticallyDampedHasNoOvershoot) {
  // 목표가 계단처럼 뛰어도 지나쳤다 되돌아오면 안 된다. 연주자가 자기 위치를
  // 잃기 때문이다.
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  const double start = c.offset();
  double worst_overshoot = 0.0;
  for (int i = 0; i < 600; ++i) {
    nota::ScrollInput in;
    in.dt_s = kDt;
    in.beat = 40.0;      // 박은 고정, 목표만 멀리
    in.tempo_bpm = 0.0;  // 피드포워드 없이 순수 스프링 응답을 본다
    in.playing = false;
    c.Update(in);
    const double over = c.offset() - c.target_offset();
    worst_overshoot = std::max(worst_overshoot, over);
  }
  CHECK(c.target_offset() > start);
  CHECK(worst_overshoot < 0.5);          // 사실상 0
  CHECK_NEAR(c.offset(), c.target_offset(), 1.0);   // 수렴은 했다
}

NOTA_TEST(FeedforwardEliminatesSteadyStateLag) {
  // 스프링만 쓰면 등속 구간에서 항상 뒤처진다. 피드포워드가 그것을 없애야 한다.
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  // 읽기 선이 화면 1/3에 있으므로 곡 시작에서는 오프셋이 음수다 (첫 시스템
  // 위로 여백이 있다). 절대값이 아니라 증가분을 본다.
  const double start = c.offset();

  Sim sim{&g, &c, 0.0, 90.0};
  sim.Run(6.0);   // 충분히 수렴할 시간

  const double lag = std::fabs(c.offset() - c.target_offset());
  CHECK(lag < 2.0);
  // 실제로 전진했는지도 본다 (멈춰 있으면 오차가 0이어도 의미 없다)
  CHECK(c.offset() > start + 1.0);
  CHECK(c.velocity() > 0.0);
}

NOTA_TEST(WithoutFeedforwardThereIsLag) {
  // 위 테스트가 의미를 가지려면, 피드포워드가 없을 때는 실제로 뒤처져야 한다.
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  double beat = 0.0;
  for (int i = 0; i < 360; ++i) {
    beat += (90.0 / 60.0) * kDt;
    nota::ScrollInput in;
    in.dt_s = kDt;
    in.beat = beat;
    in.tempo_bpm = 0.0;   // 피드포워드 끔
    in.playing = true;
    c.Update(in);
  }
  CHECK(std::fabs(c.offset() - c.target_offset()) > 2.0);
}

NOTA_TEST(VelocityIsLimited) {
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollControllerParams p;
  p.max_velocity_screens_per_second = 1.5;
  nota::ScrollController c(&g, p);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  const double limit = kViewport * p.max_velocity_screens_per_second;
  for (int i = 0; i < 600; ++i) {
    nota::ScrollInput in;
    in.dt_s = kDt;
    in.beat = g.total_beats();   // 곡 끝으로 목표를 날린다
    in.tempo_bpm = 600.0;        // 말이 안 되는 템포로 피드포워드도 과하게
    in.playing = true;
    c.Update(in);
    CHECK(std::fabs(c.velocity()) <= limit + 1e-6);
  }
}

NOTA_TEST(FreezesWhenNotPlaying) {
  // 연주가 멈추면 스크롤도 멈춰야 한다 (DESIGN.md 2.3).
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(10.0);

  Sim sim{&g, &c, 10.0, 90.0};
  sim.Run(3.0, /*playing=*/false);   // 박이 전진하지 않는다

  CHECK_NEAR(c.offset(), c.target_offset(), 0.5);
  CHECK_NEAR(c.velocity(), 0.0, 1.0);
}

NOTA_TEST(ManualScrollTakesPrecedence) {
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  c.BeginManualScroll();
  c.SetManualOffset(1234.0);
  CHECK(c.manual_active());

  // 손을 대고 있는 동안에는 제어기가 움직이지 않는다.
  Sim sim{&g, &c, 30.0, 90.0};
  sim.Run(2.0);
  CHECK_NEAR(c.offset(), 1234.0, 1e-9);
}

NOTA_TEST(ResumesOnlyAfterDelay) {
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollControllerParams p;
  p.manual_resume_delay_s = 0.3;
  nota::ScrollController c(&g, p);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  c.BeginManualScroll();
  c.SetManualOffset(500.0);
  c.EndManualScroll();
  CHECK(!c.manual_active());

  // 대기 중에는 움직이지 않는다.
  Sim before{&g, &c, 30.0, 90.0};
  before.Run(0.2);
  CHECK_NEAR(c.offset(), 500.0, 1e-9);

  // 대기가 끝나면 다시 추적한다.
  Sim after{&g, &c, 30.0, 90.0};
  after.Run(2.0);
  CHECK(std::fabs(c.offset() - 500.0) > 1.0);
}

NOTA_TEST(ManualOffsetMapsBackToBeat) {
  // 손을 뗀 위치가 새로운 현재 위치가 되어야 한다 (DESIGN.md 2.4).
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);

  for (const double beat : {5.0, 20.0, 40.0}) {
    c.SnapTo(beat);
    CHECK_NEAR(c.BeatAtCurrentOffset(), beat, 1e-6);
  }
}

NOTA_TEST(DoesNotMoveBackwardDuringSteadyPlayback) {
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  double beat = 0.0;
  double prev = c.offset();
  for (int i = 0; i < 1800; ++i) {   // 30초
    beat += (90.0 / 60.0) * kDt;
    nota::ScrollInput in;
    in.dt_s = kDt;
    in.beat = beat;
    in.tempo_bpm = 90.0;
    in.playing = true;
    c.Update(in);
    CHECK(c.offset() >= prev - 1e-6);
    prev = c.offset();
  }
}

NOTA_TEST(LargeTimeStepIsClamped) {
  // 앱이 백그라운드에 다녀오면 dt가 수 초로 들어온다. 그대로 적분하면 화면이
  // 날아간다.
  const nota::ScoreGeometry g = MakeGeometry();
  nota::ScrollController c(&g);
  c.SetViewportHeight(kViewport);
  c.SnapTo(0.0);

  nota::ScrollInput in;
  in.dt_s = 5.0;
  in.beat = g.total_beats();
  in.tempo_bpm = 120.0;
  in.playing = true;
  c.Update(in);

  const double limit = kViewport * 1.5;
  CHECK(std::fabs(c.velocity()) <= limit + 1e-6);
  // 한 걸음에 화면 하나 이상 건너뛰지 않는다.
  CHECK(std::fabs(c.offset()) < kViewport);
}

NOTA_TEST(NullGeometryIsSafe) {
  nota::ScrollController c(nullptr);
  c.SetViewportHeight(kViewport);
  c.SnapTo(10.0);
  nota::ScrollInput in;
  in.dt_s = kDt;
  in.beat = 10.0;
  in.tempo_bpm = 90.0;
  in.playing = true;
  c.Update(in);
  CHECK_NEAR(c.offset(), 0.0, 1e-9);
  CHECK_NEAR(c.BeatAtCurrentOffset(), 0.0, 1e-9);
}

NOTA_TEST(EmptyGeometryIsSafe) {
  const nota::ScoreGeometry empty;
  nota::ScrollController c(&empty);
  c.SetViewportHeight(kViewport);
  nota::ScrollInput in;
  in.dt_s = kDt;
  in.playing = true;
  c.Update(in);
  CHECK_NEAR(c.offset(), 0.0, 1e-9);
}
