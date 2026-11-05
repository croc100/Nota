// nota-scroll — 전체 파이프라인을 실제 악보로 시뮬레이션한다.
//
// 페이지 이미지 → 분석 → 캔버스 매핑 → 스크롤 제어기까지 한 번에 돌리고,
// 시간에 따른 화면 오프셋을 내보낸다. 오디오 입력만 아직 없고, 그 자리에
// 일정한 템포를 넣는다. 녹음이 도착하면 이 템포 자리에 위치 추정기의 출력이
// 들어간다.
//
// 사용법:
//   nota-scroll page-1.pgm page-2.pgm --tempo 72 --beats-per-measure 4
//                --viewport 900 --csv trace.csv

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "image_io.hpp"
#include "nota/page_analysis.hpp"
#include "nota/score_geometry.hpp"
#include "nota/scroll_controller.hpp"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "usage: nota-scroll <page1.pgm> [page2.pgm ...] [options]\n"
               "\n"
               "options:\n"
               "  --tempo <bpm>            연주 템포 (기본 72)\n"
               "  --beats-per-measure <f>  마디당 박 (기본 4)\n"
               "  --viewport <px>          화면 높이 (기본 900)\n"
               "  --fps <n>                시뮬레이션 프레임률 (기본 60)\n"
               "  --read-line <f>          읽기 선 위치 0~1 (기본 0.333)\n"
               "  --stop-at <beat>         이 박에서 연주를 멈춘다 (정지 시험)\n"
               "  --resume-at <beat-time>  멈춘 뒤 이 초 후 재개\n"
               "  --csv <out.csv>          프레임별 추적 기록\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  std::vector<std::string> inputs;
  double tempo = 72.0;
  double viewport = 900.0;
  int fps = 60;
  double stop_at = -1.0;
  double resume_after = 3.0;
  std::string csv_path;
  nota::ScoreGeometryParams geo_params;
  nota::ScrollControllerParams ctrl_params;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    const auto next = [&](const char* n) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", n);
        std::exit(2);
      }
      return argv[++i];
    };
    if (std::strcmp(a, "--tempo") == 0) {
      tempo = std::atof(next(a));
    } else if (std::strcmp(a, "--beats-per-measure") == 0) {
      geo_params.beats_per_measure = std::atof(next(a));
    } else if (std::strcmp(a, "--viewport") == 0) {
      viewport = std::atof(next(a));
    } else if (std::strcmp(a, "--fps") == 0) {
      fps = std::atoi(next(a));
    } else if (std::strcmp(a, "--read-line") == 0) {
      ctrl_params.read_line_fraction = std::atof(next(a));
    } else if (std::strcmp(a, "--stop-at") == 0) {
      stop_at = std::atof(next(a));
    } else if (std::strcmp(a, "--resume-at") == 0) {
      resume_after = std::atof(next(a));
    } else if (std::strcmp(a, "--csv") == 0) {
      csv_path = next(a);
    } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      PrintUsage();
      return 0;
    } else if (a[0] == '-') {
      std::fprintf(stderr, "error: unknown option %s\n", a);
      return 2;
    } else {
      inputs.push_back(a);
    }
  }
  if (inputs.empty() || fps < 1) {
    PrintUsage();
    return 2;
  }

  std::vector<nota::PageAnalysis> pages;
  std::vector<int> heights;
  int inconsistent = 0;
  for (const std::string& path : inputs) {
    std::string error;
    const nota::GrayImage gray = nota_tools::ReadPgm(path, &error);
    if (gray.width == 0) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
    nota::PageAnalysis page = nota::AnalyzePageGray(gray.view());
    if (!page.quality.is_consistent()) ++inconsistent;
    heights.push_back(gray.height);
    pages.push_back(std::move(page));
  }

  const nota::ScoreGeometry geo =
      nota::ScoreGeometry::Build(pages, heights, geo_params);
  if (geo.empty()) {
    std::fprintf(stderr, "error: 악보 구조를 찾지 못했다\n");
    return 1;
  }

  nota::ScrollController ctrl(&geo, ctrl_params);
  ctrl.SetViewportHeight(viewport);
  ctrl.SnapTo(0.0);

  const double dt = 1.0 / static_cast<double>(fps);
  const double total_seconds = geo.total_beats() / (tempo / 60.0) + 4.0;
  const int frames = static_cast<int>(total_seconds / dt);

  std::FILE* csv = nullptr;
  if (!csv_path.empty()) {
    csv = std::fopen(csv_path.c_str(), "wb");
    if (csv == nullptr) {
      std::fprintf(stderr, "error: cannot write %s\n", csv_path.c_str());
      return 1;
    }
    std::fprintf(csv, "time_s,beat,offset,velocity,target,system,playing\n");
  }

  double beat = 0.0;
  double paused_for = 0.0;
  double max_lag = 0.0;
  double max_speed = 0.0;
  // 역행은 있고 없고가 아니라 크기가 중요하다. 1 px 미만은 보이지 않는다.
  double max_backward = 0.0;
  double prev_offset = ctrl.offset();

  for (int f = 0; f < frames; ++f) {
    const double t = static_cast<double>(f) * dt;
    bool playing = true;

    // 정지 시험: 지정한 박에서 멈췄다가 지정 시간 뒤 재개한다.
    if (stop_at >= 0.0 && beat >= stop_at && paused_for < resume_after) {
      playing = false;
      paused_for += dt;
    }
    if (playing) beat += (tempo / 60.0) * dt;
    if (beat > geo.total_beats()) beat = geo.total_beats();

    nota::ScrollInput in;
    in.dt_s = dt;
    in.beat = beat;
    in.tempo_bpm = tempo;
    in.playing = playing;
    ctrl.Update(in);

    const double back = prev_offset - ctrl.offset();
    if (back > max_backward) max_backward = back;
    prev_offset = ctrl.offset();
    const double lag = std::abs(ctrl.offset() - ctrl.target_offset());
    if (lag > max_lag) max_lag = lag;
    if (std::abs(ctrl.velocity()) > max_speed) max_speed = std::abs(ctrl.velocity());

    if (csv != nullptr) {
      std::fprintf(csv, "%.4f,%.4f,%.3f,%.3f,%.3f,%d,%d\n", t, beat,
                   ctrl.offset(), ctrl.velocity(), ctrl.target_offset(),
                   geo.SystemAt(beat), playing ? 1 : 0);
    }
  }
  if (csv != nullptr) {
    std::fclose(csv);
    std::fprintf(stderr, "csv written: %s\n", csv_path.c_str());
  }

  std::printf("{\n");
  std::printf("  \"pages\": %zu,\n", pages.size());
  std::printf("  \"inconsistent_pages\": %d,\n", inconsistent);
  std::printf("  \"systems\": %zu,\n", geo.systems().size());
  std::printf("  \"measures\": %zu,\n", geo.measures().size());
  std::printf("  \"total_beats\": %.2f,\n", geo.total_beats());
  std::printf("  \"canvas_height\": %.1f,\n", geo.canvas_height());
  std::printf("  \"tempo_bpm\": %.1f,\n", tempo);
  std::printf("  \"duration_s\": %.1f,\n", geo.total_beats() / (tempo / 60.0));
  std::printf("  \"viewport_px\": %.0f,\n", viewport);
  std::printf("  \"max_lag_px\": %.2f,\n", max_lag);
  std::printf("  \"max_speed_px_s\": %.1f,\n", max_speed);
  std::printf("  \"max_backward_px\": %.3f\n", max_backward);
  std::printf("}\n");
  return 0;
}
