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
               "  --quiet                  JSON 출력 생략\n");
}

// 검출된 오선을 원본 위에 겹쳐 그린다. 사람이 눈으로 확인하는 것이 V1 검증에서
// 가장 빠르다. 숫자만 보면 "6개 찾았다"가 맞는지 알 수 없다.
nota_tools::RgbImage BuildOverlay(const nota::GrayImage& gray,
                                  const nota::StaffDetectResult& result) {
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

void PrintJson(const nota::GrayImage& gray,
               const nota::StaffDetectResult& result) {
  std::printf("{\n");
  std::printf("  \"image\": { \"width\": %d, \"height\": %d },\n", gray.width,
              gray.height);
  std::printf("  \"row_threshold\": %d,\n", result.row_threshold);
  std::printf("  \"line_count\": %zu,\n", result.lines.size());
  std::printf("  \"staff_count\": %zu,\n", result.staves.size());
  std::printf("  \"median_staff_space\": %.3f,\n", result.median_staff_space);
  std::printf("  \"staves\": [\n");
  for (std::size_t i = 0; i < result.staves.size(); ++i) {
    const nota::Staff& s = result.staves[i];
    std::printf(
        "    { \"index\": %zu, \"y_top\": %.2f, \"y_bottom\": %.2f, "
        "\"staff_space\": %.3f, \"lines\": [%.2f, %.2f, %.2f, %.2f, %.2f] }%s\n",
        i, s.y_top, s.y_bottom, s.staff_space, s.line_y[0], s.line_y[1],
        s.line_y[2], s.line_y[3], s.line_y[4],
        i + 1 < result.staves.size() ? "," : "");
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

  const std::string input = argv[1];
  std::string overlay_path;
  std::string projection_path;
  nota::SauvolaParams sauvola;
  nota::StaffDetectParams detect;
  bool quiet = false;

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

  const nota::BinaryImage bin = nota::BinarizeSauvola(gray.view(), sauvola);
  const nota::StaffDetectResult result = nota::DetectStaves(bin, detect);

  if (!overlay_path.empty()) {
    const nota_tools::RgbImage overlay = BuildOverlay(gray, result);
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

  if (!quiet) PrintJson(gray, result);
  return 0;
}
