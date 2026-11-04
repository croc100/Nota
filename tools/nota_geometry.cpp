// nota-geometry — 여러 페이지를 분석해 시간↔픽셀 매핑을 내보낸다.
//
// 사용법:
//   pdftoppm -gray -r 200 score.pdf page            # page-1.pgm, page-2.pgm ...
//   nota-geometry page-1.pgm page-2.pgm --beats 4
//   nota-geometry page-*.pgm --curve curve.csv      # 박→y 곡선 덤프
//
// 매핑이 맞는지 숫자만 보고 판단하기는 어렵다. --curve로 뽑은 CSV를 그려 보면
// 계단(멈춤→점프)이 아니라 꺾인 직선들이 이어진 모양이어야 하고, 꺾이는 지점이
// 시스템 경계와 일치해야 한다. 기울기가 그 시스템의 픽셀/박이다.
//
// 코어를 그대로 호출한다. 여기에 알고리즘이 없다.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "image_io.hpp"
#include "nota/page_analysis.hpp"
#include "nota/score_geometry.hpp"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "usage: nota-geometry <page1.pgm> [page2.pgm ...] [options]\n"
               "\n"
               "options:\n"
               "  --beats <f>        한 마디의 박 수 (기본 4)\n"
               "  --page-gap <f>     페이지 사이 간격 px (기본 0)\n"
               "  --total-beats <f>  곡 전체 길이(박)를 직접 지정. 0이면 검출에서 유도\n"
               "  --no-trim          페이지 여백을 자르지 않는다 (경계에서 전진이 튄다)\n"
               "  --weight <mode>    시간 배분 기준: measures | ink (기본 measures)\n"
               "  --curve <out.csv>  박→y 곡선 덤프 (beat,canvas_y,px_per_beat)\n"
               "  --curve-steps <n>  곡선 샘플 수 (기본 2000)\n"
               "  --quiet            JSON 출력 생략\n");
}

bool WriteCurveCsv(const std::string& path, const nota::ScoreGeometry& geom,
                   int steps) {
  std::FILE* f = std::fopen(path.c_str(), "w");
  if (f == nullptr) return false;
  std::fprintf(f, "beat,canvas_y,px_per_beat,system\n");
  for (int i = 0; i <= steps; ++i) {
    const double beat = geom.total_beats() * i / steps;
    std::fprintf(f, "%.6f,%.4f,%.6f,%d\n", beat, geom.CanvasY(beat),
                 geom.PixelsPerBeat(beat), geom.SystemAt(beat));
  }
  std::fclose(f);
  return true;
}

void PrintJson(const nota::ScoreGeometry& geom,
               const std::vector<nota::PageAnalysis>& pages) {
  std::printf("{\n");
  std::printf("  \"page_count\": %zu,\n", pages.size());
  std::printf("  \"canvas_height\": %.2f,\n", geom.canvas_height());
  std::printf("  \"total_beats\": %.3f,\n", geom.total_beats());
  std::printf("  \"scale_from_detection\": %s,\n",
              geom.scale_from_detection() ? "true" : "false");
  std::printf("  \"system_count\": %zu,\n", geom.systems().size());
  std::printf("  \"measure_count\": %zu,\n", geom.measures().size());

  // 페이지별 자체 품질 평가. 하나라도 불일치면 매핑을 신뢰하면 안 된다
  // (page_analysis.hpp PageQuality).
  bool all_consistent = true;
  for (const nota::PageAnalysis& p : pages) {
    if (!p.quality.is_consistent()) all_consistent = false;
  }
  std::printf("  \"quality_consistent\": %s,\n", all_consistent ? "true" : "false");
  std::printf("  \"pages\": [\n");
  for (std::size_t i = 0; i < pages.size(); ++i) {
    const nota::PageQuality& q = pages[i].quality;
    std::printf(
        "    { \"index\": %zu, \"systems\": %d, \"consistency\": %.3f, "
        "\"dominant_staff_count\": %d, \"deskew_angle_deg\": %.3f }%s\n",
        i, q.total_systems, q.consistency(), q.dominant_staff_count,
        pages[i].deskew_angle_deg, i + 1 < pages.size() ? "," : "");
  }
  std::printf("  ],\n");

  // 페이지 배치. 비트맵을 그리는 쪽이 쓰는 정보다.
  std::printf("  \"page_slots\": [\n");
  for (std::size_t i = 0; i < geom.pages().size(); ++i) {
    const nota::GeometryPage& pg = geom.pages()[i];
    std::printf(
        "    { \"index\": %d, \"has_content\": %s, \"src_y\": [%.2f, %.2f], "
        "\"offset\": %.2f }%s\n",
        pg.index, pg.has_content ? "true" : "false", pg.src_y_top, pg.src_y_bottom,
        pg.offset, i + 1 < geom.pages().size() ? "," : "");
  }
  std::printf("  ],\n");

  // 시스템마다의 y 전진과, 그 중앙값에 대한 비율. 비율이 1에서 크게 벗어나면
  // 그 시스템을 연주하는 동안 스크롤이 튄다 — 페이지 경계에서 가장 잘 생긴다
  // (ALGORITHM.md 8.3).
  std::vector<double> advance;
  for (std::size_t i = 0; i + 1 < geom.systems().size(); ++i) {
    advance.push_back(geom.systems()[i + 1].canvas_y_top -
                      geom.systems()[i].canvas_y_top);
  }
  double median_advance = 0.0;
  if (!advance.empty()) {
    std::vector<double> tmp = advance;
    std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
    median_advance = tmp[tmp.size() / 2];
  }
  std::printf("  \"median_system_advance\": %.2f,\n", median_advance);

  std::printf("  \"systems\": [\n");
  for (std::size_t i = 0; i < geom.systems().size(); ++i) {
    const nota::GeometrySystem& s = geom.systems()[i];
    std::printf(
        "    { \"index\": %zu, \"page\": %d, \"in_page\": %d, "
        "\"canvas_y\": [%.2f, %.2f], \"x\": [%.2f, %.2f], "
        "\"measures\": %d, \"ink\": %lld, \"beats\": [%.3f, %.3f], "
        "\"px_per_beat\": %.4f, \"advance\": %.2f, \"advance_ratio\": %.3f }%s\n",
        i, s.page_index, s.index_in_page, s.canvas_y_top, s.canvas_y_bottom,
        s.x_start, s.x_end, s.measure_count, s.ink_density, s.beat_start,
        s.beat_end, geom.PixelsPerBeat((s.beat_start + s.beat_end) / 2.0),
        i < advance.size() ? advance[i] : 0.0,
        (i < advance.size() && median_advance > 0.0) ? advance[i] / median_advance
                                                     : 0.0,
        i + 1 < geom.systems().size() ? "," : "");
  }
  std::printf("  ],\n");

  std::printf("  \"measures\": [\n");
  for (std::size_t i = 0; i < geom.measures().size(); ++i) {
    const nota::GeometryMeasure& m = geom.measures()[i];
    std::printf(
        "    { \"index\": %d, \"system\": %d, \"x\": [%.2f, %.2f], "
        "\"canvas_y\": [%.2f, %.2f], \"beats\": [%.3f, %.3f], \"ink\": %d }%s\n",
        m.index, m.system_index, m.x_start, m.x_end, m.canvas_y_top,
        m.canvas_y_bottom, m.beat_start, m.beat_end, m.ink_density,
        i + 1 < geom.measures().size() ? "," : "");
  }
  std::printf("  ]\n");
  std::printf("}\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  std::vector<std::string> inputs;
  std::string curve_path;
  int curve_steps = 2000;
  bool quiet = false;
  nota::ScoreGeometryParams params;

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
      params.beats_per_measure = std::atof(next(arg));
    } else if (std::strcmp(arg, "--page-gap") == 0) {
      params.page_gap = std::atof(next(arg));
    } else if (std::strcmp(arg, "--total-beats") == 0) {
      params.total_beats_override = std::atof(next(arg));
    } else if (std::strcmp(arg, "--no-trim") == 0) {
      params.trim_page_margins = false;
    } else if (std::strcmp(arg, "--weight") == 0) {
      const char* mode = next(arg);
      if (std::strcmp(mode, "measures") == 0) {
        params.time_weight = nota::ScoreGeometryParams::TimeWeight::kMeasureCount;
      } else if (std::strcmp(mode, "ink") == 0) {
        params.time_weight = nota::ScoreGeometryParams::TimeWeight::kInkDensity;
      } else {
        std::fprintf(stderr, "error: --weight must be measures or ink\n");
        return 2;
      }
    } else if (std::strcmp(arg, "--curve") == 0) {
      curve_path = next(arg);
    } else if (std::strcmp(arg, "--curve-steps") == 0) {
      curve_steps = std::atoi(next(arg));
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
  if (curve_steps < 1) {
    std::fprintf(stderr, "error: --curve-steps must be >= 1\n");
    return 2;
  }

  // 페이지 순서가 곧 곡의 순서다. 명령줄에 준 순서를 그대로 쓴다
  // (셸 글로브 page-*.pgm은 사전순이므로 10페이지가 넘으면 page-10이 page-2보다
  // 앞에 온다 — 그런 경우 호출자가 순서를 맞춰 넘겨야 한다).
  std::vector<nota::PageAnalysis> pages;
  std::vector<int> heights;
  pages.reserve(inputs.size());
  heights.reserve(inputs.size());

  for (const std::string& path : inputs) {
    std::string error;
    const nota::GrayImage gray = nota_tools::ReadPgm(path, &error);
    if (gray.width == 0) {
      std::fprintf(stderr, "error: %s: %s\n", path.c_str(), error.c_str());
      return 1;
    }
    const nota::PageAnalysis page = nota::AnalyzePageGray(gray.view());
    std::fprintf(stderr, "%s: %zu systems, %zu measures, consistency %.3f\n",
                 path.c_str(), page.systems.size(), page.measures.size(),
                 page.quality.consistency());
    pages.push_back(page);
    heights.push_back(gray.height);
  }

  const nota::ScoreGeometry geom =
      nota::ScoreGeometry::Build(pages, heights, params);
  if (geom.empty()) {
    std::fprintf(stderr, "error: 매핑을 만들 수 없다 (검출된 시스템이 없음)\n");
    return 1;
  }

  if (!curve_path.empty()) {
    if (!WriteCurveCsv(curve_path, geom, curve_steps)) {
      std::fprintf(stderr, "error: cannot write %s\n", curve_path.c_str());
      return 1;
    }
    std::fprintf(stderr, "curve written: %s\n", curve_path.c_str());
  }

  // 조용히 틀리지 않게 한다. 검출에서 축척을 유도한 경우가 실측에서 가장 크게
  // 틀렸던 조건이다 (ALGORITHM.md 8.3).
  if (geom.scale_from_detection()) {
    std::fprintf(stderr,
                 "주의: 곡 길이를 검출된 마디 수(%zu개)에서 유도했다. 마디선 "
                 "과검출이 그대로 축척 오차가 된다.\n"
                 "      실제 총 마디 수를 안다면 --total-beats <마디 수 × 박>을 "
                 "주는 편이 낫다.\n",
                 geom.measures().size());
  }

  if (!quiet) PrintJson(geom, pages);
  return 0;
}
