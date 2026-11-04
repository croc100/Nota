#include "nota/staff_detect.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace nota {
namespace {

double Median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  const std::size_t mid = v.size() / 2;
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
  const double hi = v[mid];
  if (v.size() % 2 == 1) return hi;
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid - 1),
                   v.begin() + static_cast<std::ptrdiff_t>(mid));
  return 0.5 * (hi + v[mid - 1]);
}

int Percentile(std::vector<int> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  double pos = p * static_cast<double>(v.size() - 1);
  const std::size_t i = static_cast<std::size_t>(pos);
  return v[std::min(i, v.size() - 1)];
}

}  // namespace

StaffDetectResult DetectStaves(const BinaryImage& image,
                               const StaffDetectParams& params) {
  StaffDetectResult result;
  if (image.width <= 0 || image.height <= 0) return result;

  // --- 1. 행별 잉크 투영 ----------------------------------------------------
  result.row_projection.assign(static_cast<std::size_t>(image.height), 0);
  for (int y = 0; y < image.height; ++y) {
    int count = 0;
    for (int x = 0; x < image.width; ++x) count += image.at(x, y);
    result.row_projection[static_cast<std::size_t>(y)] = count;
  }

  // --- 2. 임계 결정 ---------------------------------------------------------
  const int p99 = Percentile(result.row_projection, 0.99);
  const int relative =
      static_cast<int>(params.row_peak_fraction * static_cast<double>(p99));
  const int floor_abs = static_cast<int>(params.min_row_fraction *
                                         static_cast<double>(image.width));
  result.row_threshold = std::max(relative, floor_abs);

  // --- 3. 임계를 넘는 연속 행을 하나의 수평선으로 묶는다 ---------------------
  int run_start = -1;
  int run_peak = 0;
  const auto close_run = [&](int end_exclusive) {
    if (run_start < 0) return;
    const int thickness = end_exclusive - run_start;
    if (thickness <= params.max_line_thickness) {
      StaffLine line;
      line.y_center =
          static_cast<double>(run_start) + 0.5 * static_cast<double>(thickness - 1);
      line.thickness = thickness;
      line.ink_count = run_peak;
      result.lines.push_back(line);
    }
    run_start = -1;
    run_peak = 0;
  };

  for (int y = 0; y < image.height; ++y) {
    const int v = result.row_projection[static_cast<std::size_t>(y)];
    if (v >= result.row_threshold) {
      if (run_start < 0) run_start = y;
      run_peak = std::max(run_peak, v);
    } else {
      close_run(y);
    }
  }
  close_run(image.height);

  // --- 4. 5선 묶음 찾기 -----------------------------------------------------
  // 연속한 5개 선의 간격 4개가 서로 일관되면 하나의 오선으로 본다.
  // 일관성만 보고 절대 간격은 보지 않는다. 악보의 조판 크기가 다양하기 때문이다.
  std::size_t i = 0;
  while (result.lines.size() >= 5 && i + 4 < result.lines.size()) {
    std::vector<double> gaps(4);
    for (std::size_t g = 0; g < 4; ++g) {
      gaps[g] = result.lines[i + g + 1].y_center - result.lines[i + g].y_center;
    }
    const double med = Median(gaps);

    bool consistent = med >= params.min_staff_space;
    if (consistent) {
      for (const double g : gaps) {
        if (std::fabs(g - med) > params.spacing_tolerance * med) {
          consistent = false;
          break;
        }
      }
    }

    if (consistent) {
      Staff staff;
      double sum = 0.0;
      for (std::size_t k = 0; k < 5; ++k) {
        staff.line_y[k] = result.lines[i + k].y_center;
      }
      for (const double g : gaps) sum += g;
      staff.staff_space = sum / 4.0;
      staff.y_top = staff.line_y[0];
      staff.y_bottom = staff.line_y[4];
      result.staves.push_back(staff);
      i += 5;
    } else {
      ++i;
    }
  }

  std::vector<double> spaces;
  spaces.reserve(result.staves.size());
  for (const Staff& s : result.staves) spaces.push_back(s.staff_space);
  result.median_staff_space = Median(std::move(spaces));

  return result;
}

}  // namespace nota
