// nota-analyze — 악보 페이지에서 오선 구조를 검출해 JSON으로 출력한다.
// 검증 항목 V1 (docs/ALGORITHM.md 8절).
//
// 사용법:
//   pdftoppm -gray -r 200 -f 1 -l 1 score.pdf page   # page-1.pgm 생성
//   nota-analyze page-1.pgm --overlay overlay.ppm
//
// 코어를 그대로 호출한다. 여기에 알고리즘이 없다.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "image_io.hpp"
#include "nota/binarize.hpp"
#include "nota/page_analysis.hpp"
#include "nota/skew.hpp"
#include "nota/staff_detect.hpp"

namespace {

void PrintUsage() {
  std::fprintf(stderr,
               "usage: nota-analyze <input.pgm> [options]\n"
               "\n"
               "options:\n"
               "  --overlay <out.ppm>      검출 결과를 겹쳐 그린 이미지 출력\n"
               "  --projection <out.csv>   행별 잉크 투영 덤프 (y,ink)\n"
               "  --window <n>             Sauvola 윈도우 (기본 41)\n"
               "  --peak-fraction <f>      오선 임계 비율 (기본 0.5)\n"
               "  --no-deskew              기울기 보정 끄기\n"
               "  --quiet                  JSON 출력 생략\n");
}

// 검출된 오선을 원본 위에 겹쳐 그린다. 사람이 눈으로 확인하는 것이 V1 검증에서
// 가장 빠르다. 숫자만 보면 "6개 찾았다"가 맞는지 알 수 없다.
nota_tools::RgbImage BuildOverlay(const nota::GrayImage& gray,
                                  const nota::PageAnalysis& page) {
  const nota::StaffDetectResult& result = page.staves;
  nota_tools::RgbImage out(gray.width, gray.height);
  for (int y = 0; y < gray.height; ++y) {
    for (int x = 0; x < gray.width; ++x) {
      const std::uint8_t v = gray.at(x, y);
      out.set(x, y, v, v, v);
    }
  }

  // 5선 묶음을 이루지 못한 수평선: 주황색 — 오검출을 눈에 띄게 한다
  for (const nota::StaffLine& line : result.lines) {
    const int y = static_cast<int>(line.y_center);
    for (int x = 0; x < gray.width; x += 6) out.set(x, y, 255, 150, 0);
  }

  // 오선으로 묶인 선: 초록색, 전체 폭
  for (const nota::Staff& staff : result.staves) {
    for (int k = 0; k < 5; ++k) {
      const int y = static_cast<int>(staff.line_y[k]);
      for (int x = 0; x < gray.width; ++x) out.set(x, y, 0, 190, 60);
    }
    // 오선 묶음의 수직 범위를 좌측 여백에 표시
    const int y0 = static_cast<int>(staff.y_top);
    const int y1 = static_cast<int>(staff.y_bottom);
    for (int y = y0; y <= y1; ++y) {
      for (int x = 0; x < 6; ++x) out.set(x, y, 40, 90, 255);
    }
  }

  // 검출된 마디선: 빨간 세로선
  for (const nota::Barline& bar : page.barlines) {
    const nota::System& sys = page.systems[static_cast<std::size_t>(bar.system_index)];
    const int x = static_cast<int>(bar.x);
    for (int y = static_cast<int>(sys.y_top); y <= static_cast<int>(sys.y_bottom); ++y) {
      out.set(x, y, 230, 30, 30);
    }
  }

  // 시스템 경계: 좌우 끝에 보라색 세로 막대
  for (const nota::System& sys : page.systems) {
    for (int y = static_cast<int>(sys.y_top); y <= static_cast<int>(sys.y_bottom); ++y) {
      for (int d = 0; d < 3; ++d) {
        out.set(static_cast<int>(sys.x_start) - d, y, 150, 40, 200);
        out.set(static_cast<int>(sys.x_end) + d, y, 150, 40, 200);
      }
    }
  }

  // 마디 상자: 상단에 번갈아 색이 바뀌는 띠 — 경계가 눈에 바로 들어온다
  for (const nota::Measure& m : page.measures) {
    const bool even = (m.index % 2) == 0;
    const int y = static_cast<int>(m.y_top) - 6;
    for (int x = static_cast<int>(m.x_start); x <= static_cast<int>(m.x_end); ++x) {
      for (int d = 0; d < 4; ++d) {
        if (even) {
          out.set(x, y - d, 255, 170, 40);
        } else {
          out.set(x, y - d, 40, 170, 255);
        }
      }
    }
  }
  return out;
}

bool WriteProjectionCsv(const std::string& path,
                        const nota::StaffDetectResult& result) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "y,ink,threshold\n");
  for (std::size_t y = 0; y < result.row_projection.size(); ++y) {
    std::fprintf(f, "%zu,%d,%d\n", y, result.row_projection[y],
                 result.row_threshold);
  }
  std::fclose(f);
  return true;
}

void PrintJson(const nota::GrayImage& gray, const nota::PageAnalysis& page) {
  const nota::StaffDetectResult& result = page.staves;
  std::printf("{\n");
  std::printf("  \"image\": { \"width\": %d, \"height\": %d },\n", gray.width,
              gray.height);
  std::printf("  \"deskew_angle_deg\": %.3f,\n", page.deskew_angle_deg);
  std::printf(
      "  \"quality\": { \"consistent\": %s, \"dominant_staff_count\": %d, "
      "\"consistency\": %.3f },\n",
      page.quality.is_consistent() ? "true" : "false",
      page.quality.dominant_staff_count, page.quality.consistency());
  std::printf("  \"row_threshold\": %d,\n", result.row_threshold);
  std::printf("  \"line_count\": %zu,\n", result.lines.size());
  std::printf("  \"staff_count\": %zu,\n", result.staves.size());
  std::printf("  \"system_count\": %zu,\n", page.systems.size());
  std::printf("  \"barline_count\": %zu,\n", page.barlines.size());
  std::printf("  \"measure_count\": %zu,\n", page.measures.size());
  std::printf("  \"median_staff_space\": %.3f,\n", result.median_staff_space);

  std::printf("  \"systems\": [\n");
  for (std::size_t i = 0; i < page.systems.size(); ++i) {
    const nota::System& s = page.systems[i];
    std::printf(
        "    { \"index\": %zu, \"staves\": %d, \"y_top\": %.2f, "
        "\"y_bottom\": %.2f, \"x_start\": %.2f, \"x_end\": %.2f, "
        "\"staff_space\": %.3f }%s\n",
        i, s.staff_count, s.y_top, s.y_bottom, s.x_start, s.x_end, s.staff_space,
        i + 1 < page.systems.size() ? "," : "");
  }
  std::printf("  ],\n");

  std::printf("  \"measures\": [\n");
  for (std::size_t i = 0; i < page.measures.size(); ++i) {
    const nota::Measure& m = page.measures[i];
    std::printf(
        "    { \"index\": %d, \"system\": %d, \"x\": [%.2f, %.2f], "
        "\"y\": [%.2f, %.2f], \"ink\": %d }%s\n",
        m.index, m.system_index, m.x_start, m.x_end, m.y_top, m.y_bottom,
        m.ink_density, i + 1 < page.measures.size() ? "," : "");
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

  if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
    PrintUsage();
    return 0;
  }

  const std::string input = argv[1];
  std::string overlay_path;
  std::string projection_path;
  nota::SauvolaParams sauvola;
  nota::StaffDetectParams detect;
  bool quiet = false;
  bool no_deskew = false;

  for (int i = 2; i < argc; ++i) {
    const char* arg = argv[i];
    const auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (std::strcmp(arg, "--overlay") == 0) {
      overlay_path = next(arg);
    } else if (std::strcmp(arg, "--projection") == 0) {
      projection_path = next(arg);
    } else if (std::strcmp(arg, "--window") == 0) {
      sauvola.window = std::atoi(next(arg));
    } else if (std::strcmp(arg, "--peak-fraction") == 0) {
      detect.row_peak_fraction = std::atof(next(arg));
    } else if (std::strcmp(arg, "--no-deskew") == 0) {
      no_deskew = true;
    } else if (std::strcmp(arg, "--quiet") == 0) {
      quiet = true;
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      PrintUsage();
      return 0;
    } else {
      std::fprintf(stderr, "error: unknown option %s\n", arg);
      return 2;
    }
  }

  std::string error;
  const nota::GrayImage gray = nota_tools::ReadPgm(input, &error);
  if (gray.width == 0) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  nota::PageAnalysisParams analysis_params;
  analysis_params.sauvola = sauvola;
  analysis_params.staff = detect;
  analysis_params.deskew = !no_deskew;
  const nota::PageAnalysis page = nota::AnalyzePageGray(gray.view(), analysis_params);
  const nota::StaffDetectResult& result = page.staves;

  if (!overlay_path.empty()) {
    // 좌표는 기울기 보정 후 기준이므로 배경 이미지도 같은 각도로 돌려야
    // 겹쳐 그린 결과가 맞는다.
    const nota::GrayImage shown =
        page.deskew_angle_deg != 0.0
            ? nota::RotateGray(gray.view(), page.deskew_angle_deg)
            : gray;
    const nota_tools::RgbImage overlay = BuildOverlay(shown, page);
    if (!nota_tools::WritePpm(overlay_path, overlay, &error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
    std::fprintf(stderr, "overlay written: %s\n", overlay_path.c_str());
  }

  if (!projection_path.empty()) {
    if (!WriteProjectionCsv(projection_path, result)) {
      std::fprintf(stderr, "error: cannot write %s\n", projection_path.c_str());
      return 1;
    }
    std::fprintf(stderr, "projection written: %s\n", projection_path.c_str());
  }

  if (!quiet) PrintJson(gray, page);
  return 0;
}
