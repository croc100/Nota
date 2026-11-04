// nota-simulate — 매핑과 스크롤 제어기를 묶어 연주를 모사한다.
//
// 오디오가 없어도 제어기의 품질을 측정할 수 있다. 정답 박 곡선을 직접 만들어
// 넣고(등속 연주, 템포 변화, 재동기화 점프, 정지) 스크롤 궤적을 받아 본다.
// 측정하는 것은 정확성이 아니라 **부드러움**이다 — 떨리거나 급변하는 스크롤이
// 추적 오차보다 연주를 더 방해한다 (docs/ALGORITHM.md 7절).
//
// 사용법:
//   nota-simulate page-1.pgm page-2.pgm --beats 4 --tempo 90 --trace t.csv
//   nota-simulate page-1.pgm --beats 4 --resync-at 20 --resync-beats 8
//
// 코어를 그대로 호출한다. 여기에 알고리즘이 없다.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "image_io.hpp"
#include "nota/page_analysis.hpp"
#include "nota/position_estimator.hpp"
#include "nota/score_geometry.hpp"
#include "nota/scroll_control.hpp"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "usage: nota-simulate <page1.pgm> [page2.pgm ...] [options]\n"
               "\n"
               "악보 옵션:\n"
               "  --beats <f>          한 마디의 박 수 (기본 4)\n"
               "  --total-beats <f>    곡 길이를 직접 지정 (권장, ALGORITHM.md 8.3)\n"
               "  --no-trim            페이지 여백을 자르지 않는다\n"
               "\n"
               "연주 옵션:\n"
               "  --tempo <f>          BPM (기본 90)\n"
               "  --fps <f>            프레임률 (기본 60)\n"
               "  --viewport <f>       화면 높이 px (기본 900)\n"
               "  --omega <f>          스프링 고유 진동수 rad/s (기본 2pi*0.8)\n"
               "  --zeta <f>           감쇠비 (기본 1 = 임계 감쇠)\n"
               "  --drop-frames        10프레임마다 한 번 dt를 4배로 (프레임 드롭)\n"
               "  --resync-at <f>      이 박에서 위치를 점프시킨다 (재동기화 모사)\n"
               "  --resync-beats <f>   점프 크기(박). 기본 8\n"
               "  --stop-at <f>        이 박에서 연주를 멈춘다 (2초간)\n"
               "\n"
               "위치 추정 옵션 (ALGORITHM.md 6절):\n"
               "  --estimate           위치 추정기를 끼워 넣는다. 온셋은 정답\n"
               "                       위치의 잉크에서 합성한다 (완벽한 검출기 가정)\n"
               "  --tempo-error <f>    템포 추정 오차 (예: 0.03 = 3%% 낮게 추정)\n"
               "  --ink-per-onset <f>  합성에 쓸 실제 잉크/온셋 (기본 120)\n"
               "  --no-resync          추정기를 켜되 재동기화를 끈다 (적분만)\n"
               "\n"
               "출력:\n"
               "  --trace <out.csv>    프레임별 궤적 덤프\n"
               "  --quiet              요약 출력 생략\n");
}

struct Frame {
  double t = 0.0;
  double beat = 0.0;       // 제어기에 들어간 박 (추정기를 켜면 추정값)
  double true_beat = 0.0;  // 정답 박
  double confidence = 0.0;
  double target = 0.0;
  double offset = 0.0;
  double velocity = 0.0;
  int system = 0;
};

double Percentile(std::vector<double> v, double q) {
  if (v.empty()) return 0.0;
  const std::size_t i = static_cast<std::size_t>(
      std::min<double>(static_cast<double>(v.size()) - 1.0,
                       std::max(0.0, q * (static_cast<double>(v.size()) - 1.0))));
  std::nth_element(v.begin(), v.begin() + static_cast<long>(i), v.end());
  return v[i];
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  std::vector<std::string> inputs;
  std::string trace_path;
  nota::ScoreGeometryParams geom_params;
  nota::ScrollControlParams ctl_params;
  double beats_per_measure = 4.0;
  double tempo = 90.0;
  double fps = 60.0;
  double viewport = 900.0;
  bool drop_frames = false;
  bool quiet = false;
  double resync_at = -1.0;
  double resync_beats = 8.0;
  double stop_at = -1.0;
  bool estimate = false;
  bool no_resync = false;
  double tempo_error = 0.0;
  double true_ink_per_onset = 120.0;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (std::strcmp(arg, "--beats") == 0) {
      beats_per_measure = std::atof(next(arg));
    } else if (std::strcmp(arg, "--total-beats") == 0) {
      geom_params.total_beats_override = std::atof(next(arg));
    } else if (std::strcmp(arg, "--no-trim") == 0) {
      geom_params.trim_page_margins = false;
    } else if (std::strcmp(arg, "--tempo") == 0) {
      tempo = std::atof(next(arg));
    } else if (std::strcmp(arg, "--fps") == 0) {
      fps = std::atof(next(arg));
    } else if (std::strcmp(arg, "--viewport") == 0) {
      viewport = std::atof(next(arg));
    } else if (std::strcmp(arg, "--omega") == 0) {
      ctl_params.omega = std::atof(next(arg));
    } else if (std::strcmp(arg, "--zeta") == 0) {
      ctl_params.zeta = std::atof(next(arg));
    } else if (std::strcmp(arg, "--drop-frames") == 0) {
      drop_frames = true;
    } else if (std::strcmp(arg, "--resync-at") == 0) {
      resync_at = std::atof(next(arg));
    } else if (std::strcmp(arg, "--resync-beats") == 0) {
      resync_beats = std::atof(next(arg));
    } else if (std::strcmp(arg, "--stop-at") == 0) {
      stop_at = std::atof(next(arg));
    } else if (std::strcmp(arg, "--estimate") == 0) {
      estimate = true;
    } else if (std::strcmp(arg, "--tempo-error") == 0) {
      tempo_error = std::atof(next(arg));
    } else if (std::strcmp(arg, "--ink-per-onset") == 0) {
      true_ink_per_onset = std::atof(next(arg));
    } else if (std::strcmp(arg, "--no-resync") == 0) {
      no_resync = true;
    } else if (std::strcmp(arg, "--trace") == 0) {
      trace_path = next(arg);
    } else if (std::strcmp(arg, "--quiet") == 0) {
      quiet = true;
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      PrintUsage();
      return 0;
    } else if (arg[0] == '-') {
      std::fprintf(stderr, "error: unknown option %s\n", arg);
      return 2;
    } else {
      inputs.push_back(arg);
    }
  }

  if (inputs.empty()) {
    std::fprintf(stderr, "error: no input pages\n");
    return 2;
  }
  if (!(fps > 0.0) || !(tempo > 0.0) || !(viewport > 0.0)) {
    std::fprintf(stderr, "error: --fps/--tempo/--viewport must be > 0\n");
    return 2;
  }
  geom_params.beats_per_measure = beats_per_measure;

  std::vector<nota::PageAnalysis> pages;
  std::vector<int> heights;
  for (const std::string& path : inputs) {
    std::string error;
    const nota::GrayImage gray = nota_tools::ReadPgm(path, &error);
    if (gray.width == 0) {
      std::fprintf(stderr, "error: %s: %s\n", path.c_str(), error.c_str());
      return 1;
    }
    pages.push_back(nota::AnalyzePageGray(gray.view()));
    heights.push_back(gray.height);
  }

  const nota::ScoreGeometry geom =
      nota::ScoreGeometry::Build(pages, heights, geom_params);
  if (geom.empty()) {
    std::fprintf(stderr, "error: 매핑을 만들 수 없다 (검출된 시스템이 없음)\n");
    return 1;
  }

  nota::ScrollController ctl(geom, viewport, ctl_params);
  ctl.Reset(0.0);

  // 연주를 모사한다. 박은 템포로 적분하고, 요청에 따라 점프와 정지를 끼운다.
  nota::PositionEstimatorParams est_params;
  std::unique_ptr<nota::PositionEstimator> est;
  const double estimated_tempo = tempo * (1.0 - tempo_error);
  if (estimate) {
    est = std::make_unique<nota::PositionEstimator>(geom, est_params);
    est->Reset(0.0, estimated_tempo);
  }

  std::vector<Frame> frames;
  const double base_dt = 1.0 / fps;
  double t = 0.0;
  double beat = 0.0;
  double true_beat = 0.0;
  bool resynced = false;
  double stop_remaining = -1.0;
  int frame_index = 0;

  while (true_beat < geom.total_beats() && t < 1800.0) {
    double dt = base_dt;
    // 프레임 드롭: 10프레임마다 한 번 4배로 길어진다.
    if (drop_frames && (frame_index % 10) == 9) dt = base_dt * 4.0;

    bool playing = true;
    if (stop_at >= 0.0 && stop_remaining < 0.0 && true_beat >= stop_at) {
      stop_remaining = 2.0;  // 2초간 정지
    }
    if (stop_remaining >= 0.0) {
      playing = false;
      stop_remaining -= dt;
      if (stop_remaining < 0.0) stop_remaining = -1.0;
    }

    if (playing) true_beat += (tempo / 60.0) * dt;

    // 재동기화 모사: 위치 추정이 한 번에 뛴다 (ALGORITHM.md 6.2).
    if (!resynced && resync_at >= 0.0 && true_beat >= resync_at) {
      true_beat += resync_beats;
      resynced = true;
    }

    double confidence = 0.0;
    if (estimate) {
      // 완벽한 온셋 검출기를 가정하고 정답 위치의 잉크에서 온셋 수를 합성한다.
      // 검출기의 품질은 녹음이 와야 측정된다 (V3/V4).
      const double window_beats =
          (estimated_tempo / 60.0) * est_params.resync_window_seconds;
      int onsets = -1;
      if (!no_resync && playing) {
        const double ink =
            geom.InkBetween(true_beat - window_beats, true_beat);
        onsets = static_cast<int>(std::lround(ink / true_ink_per_onset));
      }
      const nota::PositionState ps =
          est->Update(dt, estimated_tempo, playing, onsets);
      beat = ps.beat;
      confidence = ps.confidence;
    } else {
      beat = true_beat;
    }

    const nota::ScrollState s = ctl.Update(dt, beat, playing ? estimated_tempo : 0.0);
    t += dt;
    ++frame_index;

    Frame f;
    f.t = t;
    f.beat = beat;
    f.true_beat = true_beat;
    f.confidence = confidence;
    f.target = geom.CanvasY(beat) - ctl.read_line();
    f.offset = s.offset;
    f.velocity = s.velocity;
    f.system = geom.SystemAt(beat);
    frames.push_back(f);
  }

  if (!trace_path.empty()) {
    std::FILE* out = std::fopen(trace_path.c_str(), "w");
    if (out == nullptr) {
      std::fprintf(stderr, "error: cannot write %s\n", trace_path.c_str());
      return 1;
    }
    std::fprintf(out,
                 "t,beat,true_beat,beat_error,confidence,target_y,offset,"
                 "velocity,accel,system\n");
    for (std::size_t i = 0; i < frames.size(); ++i) {
      const double prev_v = i > 0 ? frames[i - 1].velocity : 0.0;
      const double h = i > 0 ? frames[i].t - frames[i - 1].t : base_dt;
      std::fprintf(out, "%.5f,%.5f,%.5f,%.5f,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n",
                   frames[i].t, frames[i].beat, frames[i].true_beat,
                   frames[i].beat - frames[i].true_beat, frames[i].confidence,
                   frames[i].target, frames[i].offset, frames[i].velocity,
                   (frames[i].velocity - prev_v) / h, frames[i].system);
    }
    std::fclose(out);
    std::fprintf(stderr, "trace written: %s\n", trace_path.c_str());
  }

  if (quiet) return 0;

  // 요약. 부드러움 지표를 뽑는다.
  std::vector<double> errors;
  std::vector<double> accels;
  double max_speed = 0.0;
  double max_accel = 0.0;
  double max_error = 0.0;
  double worst_accel_t = 0.0;
  int worst_accel_system = 0;
  double reversal_px = 0.0;  // 역행한 총 거리 — 0이어야 한다
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const double err = std::fabs(frames[i].offset - frames[i].target);
    errors.push_back(err);
    max_error = std::fmax(max_error, err);
    max_speed = std::fmax(max_speed, std::fabs(frames[i].velocity));
    if (i > 0) {
      const double h = frames[i].t - frames[i - 1].t;
      const double a = (frames[i].velocity - frames[i - 1].velocity) / h;
      accels.push_back(std::fabs(a));
      if (std::fabs(a) > max_accel) {
        max_accel = std::fabs(a);
        worst_accel_t = frames[i].t;
        worst_accel_system = frames[i].system;
      }
      const double step = frames[i].offset - frames[i - 1].offset;
      if (step < 0.0) reversal_px += -step;
    }
  }

  // 재동기화 보정을 흡수하는 데 걸린 시간. ω를 정하는 축이다 — 낮은 ω는
  // 부드럽지만 보정이 오래 남고, 높은 ω는 빠르지만 화면을 잡아챈다.
  double resync_settle = -1.0;
  double resync_peak_error = 0.0;
  if (resync_at >= 0.0) {
    // 점프 직후부터 오차가 읽기 선의 2% 아래로 내려가 유지되는 첫 시점.
    const double settled = ctl.read_line() * 0.02;
    double jump_t = -1.0;
    for (std::size_t i = 1; i < frames.size(); ++i) {
      if (frames[i].true_beat - frames[i - 1].true_beat > (tempo / 60.0) * 0.5) {
        jump_t = frames[i].t;
        break;
      }
    }
    if (jump_t >= 0.0) {
      for (std::size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].t < jump_t) continue;
        const double err = std::fabs(frames[i].offset - frames[i].target);
        resync_peak_error = std::fmax(resync_peak_error, err);
        if (err > settled) resync_settle = frames[i].t - jump_t;
      }
    }
  }

  std::printf("프레임 %zu개, %.1f초, 템포 %.0f BPM, 화면 %.0f px\n", frames.size(),
              t, tempo, viewport);
  std::printf("시스템 %zu개, 총 %.1f박, 축척 %s\n", geom.systems().size(),
              geom.total_beats(),
              geom.scale_from_detection() ? "검출에서 유도 (주의)" : "외부 지정");
  std::printf("\n추적 오차 (목표 오프셋 대비)\n");
  std::printf("  중앙값 %.2f px,  95%% %.2f px,  최대 %.2f px (읽기 선 %.0f px)\n",
              Percentile(errors, 0.5), Percentile(errors, 0.95), max_error,
              ctl.read_line());
  std::printf("\n부드러움\n");
  std::printf("  최대 속도   %.1f px/s (상한 %.1f)\n", max_speed, viewport * 1.5);
  std::printf("  최대 가속도 %.1f px/s^2  (t=%.2fs, 시스템 %d)\n", max_accel,
              worst_accel_t, worst_accel_system);
  std::printf("  가속도 95%% %.1f px/s^2\n", Percentile(accels, 0.95));
  std::printf("  역행 거리   %.3f px\n", reversal_px);
  std::printf("  (역행 = 스크롤이 되돌아온 총 거리. 0이어야 한다, ALGORITHM.md 7.2)\n");
  if (estimate) {
    // 위치 추정 오차. V6(3분 연주 후 1시스템 이내)의 지표다.
    std::vector<double> beat_errors;
    double worst_beat_error = 0.0;
    double conf_sum = 0.0;
    int conf_count = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
      const double e = std::fabs(frames[i].beat - frames[i].true_beat);
      beat_errors.push_back(e);
      worst_beat_error = std::fmax(worst_beat_error, e);
      if (frames[i].confidence > 0.0) {
        conf_sum += frames[i].confidence;
        ++conf_count;
      }
    }
    const double system_beats =
        geom.systems()[0].beat_end - geom.systems()[0].beat_start;
    std::printf("\n위치 추정 (템포 오차 %.1f%%, 시스템당 %.1f박)\n",
                tempo_error * 100.0, system_beats);
    std::printf("  박 오차 중앙값 %.2f,  95%% %.2f,  최대 %.2f박\n",
                Percentile(beat_errors, 0.5), Percentile(beat_errors, 0.95),
                worst_beat_error);
    std::printf("  최대 오차 = %.2f 시스템  (V6 기준 1.0 이내)\n",
                system_beats > 0.0 ? worst_beat_error / system_beats : 0.0);
    if (!no_resync) {
      std::printf("  신뢰도 > 0인 프레임 %d개, 평균 신뢰도 %.3f\n", conf_count,
                  conf_count > 0 ? conf_sum / conf_count : 0.0);
      std::printf("  잉크/온셋 환산: 참값 %.0f → 추정 %.0f\n", true_ink_per_onset,
                  est->ink_per_onset());
    }
  }

  if (resync_settle >= 0.0) {
    std::printf("\n재동기화 흡수\n");
    std::printf("  점프 %.1f박 → 최대 오차 %.1f px, 흡수 시간 %.2f초\n",
                resync_beats, resync_peak_error, resync_settle);
  }
  return 0;
}
