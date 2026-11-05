// nota-geometry — 여러 페이지를 이어붙여 시간↔픽셀 매핑을 만들고 내보낸다.
//
// 사용법:
//   pdftoppm -gray -r 200 score.pdf page        # page-1.pgm, page-2.pgm ...
//   nota-geometry page-1.pgm page-2.pgm --beats-per-measure 3
//
// 코어를 그대로 호출한다. 여기에 알고리즘이 없다.

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
               "  --beats-per-measure <f>  마디당 박 수 (기본 4)\n"
               "  --page-gap <px>          페이지 사이 간격 (기본 0)\n"
               "  --weight measure|ink     시간 배분 기준 (기본 measure)\n"
               "  --csv <out.csv>          beat,canvas_y,px_per_beat 표본 출력\n"
               "  --samples <n>            CSV 표본 수 (기본 400)\n");
}

bool WriteCsv(const std::string& path, const nota::ScoreGeometry& g, int samples) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "beat,canvas_y,px_per_beat,system\n");
  for (int i = 0; i <= samples; ++i) {
    const double beat =
        g.total_beats() * static_cast<double>(i) / static_cast<double>(samples);
    std::fprintf(f, "%.4f,%.3f,%.4f,%d\n", beat, g.CanvasY(beat),
                 g.PixelsPerBeat(beat), g.SystemAt(beat));
  }
  std::fclose(f);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  std::vector<std::string> inputs;
  nota::ScoreGeometryParams params;
  std::string csv_path;
  int samples = 400;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (std::strcmp(arg, "--beats-per-measure") == 0) {
      params.beats_per_measure = std::atof(next(arg));
    } else if (std::strcmp(arg, "--page-gap") == 0) {
      params.page_gap = std::atof(next(arg));
    } else if (std::strcmp(arg, "--weight") == 0) {
      const std::string w = next(arg);
      if (w == "ink") {
        params.time_weight = nota::ScoreGeometryParams::TimeWeight::kInkDensity;
      } else if (w != "measure") {
        std::fprintf(stderr, "error: --weight must be measure or ink\n");
        return 2;
      }
    } else if (std::strcmp(arg, "--csv") == 0) {
      csv_path = next(arg);
    } else if (std::strcmp(arg, "--samples") == 0) {
      samples = std::atoi(next(arg));
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

  if (inputs.empty() || samples < 1) {
    PrintUsage();
    return 2;
  }

  std::vector<nota::PageAnalysis> pages;
  std::vector<int> heights;
  int inconsistent_pages = 0;

  for (const std::string& path : inputs) {
    std::string error;
    const nota::GrayImage gray = nota_tools::ReadPgm(path, &error);
    if (gray.width == 0) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
    nota::PageAnalysis page = nota::AnalyzePageGray(gray.view());
    if (!page.quality.is_consistent()) ++inconsistent_pages;
    heights.push_back(gray.height);
    pages.push_back(std::move(page));
  }

  const nota::ScoreGeometry g =
      nota::ScoreGeometry::Build(pages, heights, params);

  if (!csv_path.empty()) {
    if (!WriteCsv(csv_path, g, samples)) {
      std::fprintf(stderr, "error: cannot write %s\n", csv_path.c_str());
      return 1;
    }
    std::fprintf(stderr, "csv written: %s\n", csv_path.c_str());
  }

  std::printf("{\n");
  std::printf("  \"pages\": %zu,\n", pages.size());
  std::printf("  \"inconsistent_pages\": %d,\n", inconsistent_pages);
  std::printf("  \"canvas_height\": %.2f,\n", g.canvas_height());
  std::printf("  \"total_beats\": %.3f,\n", g.total_beats());
  std::printf("  \"system_count\": %zu,\n", g.systems().size());
  std::printf("  \"measure_count\": %zu,\n", g.measures().size());
  std::printf("  \"systems\": [\n");
  for (std::size_t i = 0; i < g.systems().size(); ++i) {
    const nota::GeometrySystem& s = g.systems()[i];
    std::printf(
        "    { \"i\": %zu, \"page\": %d, \"measures\": %d, "
        "\"canvas_y\": [%.2f, %.2f], \"beats\": [%.3f, %.3f], "
        "\"px_per_beat\": %.4f }%s\n",
        i, s.page_index, s.measure_count, s.canvas_y_top, s.canvas_y_bottom,
        s.beat_start, s.beat_end,
        g.PixelsPerBeat(0.5 * (s.beat_start + s.beat_end)),
        i + 1 < g.systems().size() ? "," : "");
  }
  std::printf("  ]\n}\n");
  return 0;
}
