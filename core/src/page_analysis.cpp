#include "nota/page_analysis.hpp"

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

// 오선이 실제로 그려진 수평 범위를 찾는다. 페이지 폭이 아니라 이 범위가
// 마디 계산의 기준이다 — 여백과 잘라내기 정도가 악보마다 다르기 때문이다.
void StaffHorizontalExtent(const BinaryImage& image, const Staff& staff,
                           double* x_start, double* x_end) {
  std::vector<double> firsts;
  std::vector<double> lasts;
  for (int k = 0; k < 5; ++k) {
    const int y = static_cast<int>(std::lround(staff.line_y[k]));
    if (y < 0 || y >= image.height) continue;
    int first = -1;
    int last = -1;
    for (int x = 0; x < image.width; ++x) {
      if (image.at(x, y) != 0) {
        if (first < 0) first = x;
        last = x;
      }
    }
    if (first >= 0) {
      firsts.push_back(static_cast<double>(first));
      lasts.push_back(static_cast<double>(last));
    }
  }
  // 중위수를 쓴다. 한 선이 스캔 얼룩으로 길게 번져도 흔들리지 않는다.
  *x_start = Median(std::move(firsts));
  *x_end = Median(std::move(lasts));
}

// 오선 사이 간격을 "시스템 내부"와 "시스템 사이"로 가른다.
//
// 절대 임계를 쓰지 않는 이유: 조판 크기와 여백이 악보마다 다르다. 대신 관측된
// 간격들에서 가장 큰 틈을 찾아 둘로 나눈다. 피아노 악보에서 이 두 분포는 잘
// 분리되어 있다 — 대보표 내부 간격은 시스템 사이 간격보다 뚜렷하게 작다.
double SplitThreshold(std::vector<double> gaps) {
  if (gaps.size() < 2) return 0.0;
  std::sort(gaps.begin(), gaps.end());
  double best_gap = -1.0;
  double threshold = 0.0;
  for (std::size_t i = 0; i + 1 < gaps.size(); ++i) {
    const double d = gaps[i + 1] - gaps[i];
    if (d > best_gap) {
      best_gap = d;
      threshold = 0.5 * (gaps[i] + gaps[i + 1]);
    }
  }
  return threshold;
}

// 두 오선 사이에 시작부 수직 연결선이 있는지 본다. 대보표의 정의적 특징이다.
bool StavesAreConnected(const BinaryImage& image, const Staff& upper,
                        const Staff& lower, double x_start, double staff_space,
                        const SystemDetectParams& params) {
  const int y0 = std::max(0, static_cast<int>(std::floor(upper.y_top)));
  const int y1 =
      std::min(image.height, static_cast<int>(std::ceil(lower.y_bottom)) + 1);
  if (y1 <= y0) return false;
  const double height = static_cast<double>(y1 - y0);

  const int xa = std::max(
      0, static_cast<int>(x_start - params.connector_search_before * staff_space));
  const int xb = std::min(
      image.width,
      static_cast<int>(x_start + params.connector_search_after * staff_space) + 1);

  int best = 0;
  for (int x = xa; x < xb; ++x) {
    int count = 0;
    for (int y = y0; y < y1; ++y) count += image.at(x, y);
    best = std::max(best, count);
  }
  return static_cast<double>(best) >= params.connector_height_fraction * height;
}

std::vector<System> GroupSystems(const BinaryImage& image,
                                 const StaffDetectResult& staves,
                                 const SystemDetectParams& params) {
  std::vector<System> systems;
  if (staves.staves.empty()) return systems;

  const std::size_t n = staves.staves.size();

  // 오선별 수평 범위를 먼저 구한다. 연결선 탐색의 기준점이 된다.
  std::vector<double> sx(n, 0.0);
  std::vector<double> ex(n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    StaffHorizontalExtent(image, staves.staves[i], &sx[i], &ex[i]);
  }

  // 연속한 오선 쌍이 연결되어 있는지 판정한다.
  std::vector<bool> connected(n > 0 ? n - 1 : 0, false);
  bool any_connected = false;
  for (std::size_t i = 0; i + 1 < n; ++i) {
    const double space =
        0.5 * (staves.staves[i].staff_space + staves.staves[i + 1].staff_space);
    connected[i] = StavesAreConnected(image, staves.staves[i], staves.staves[i + 1],
                                      std::min(sx[i], sx[i + 1]), space, params);
    if (connected[i]) any_connected = true;
  }

  // 연결선이 전혀 없으면 간격 통계로 폴백한다.
  if (!any_connected && n > 1) {
    std::vector<double> gaps;
    gaps.reserve(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
      gaps.push_back(staves.staves[i + 1].y_top - staves.staves[i].y_bottom);
    }
    double threshold = SplitThreshold(gaps);
    if (threshold <= 0.0) {
      threshold = params.fallback_pair_ratio * 4.0 * staves.median_staff_space;
    }
    for (std::size_t i = 0; i + 1 < n; ++i) connected[i] = gaps[i] < threshold;
  }

  std::size_t i = 0;
  while (i < n) {
    System sys;
    sys.first_staff = static_cast<int>(i);
    std::size_t j = i;
    while (j + 1 < n && connected[j]) ++j;
    sys.staff_count = static_cast<int>(j - i + 1);
    sys.y_top = staves.staves[i].y_top;
    sys.y_bottom = staves.staves[j].y_bottom;

    double sum_space = 0.0;
    for (std::size_t k = i; k <= j; ++k) sum_space += staves.staves[k].staff_space;
    sys.staff_space = sum_space / static_cast<double>(j - i + 1);

    double x0 = 0.0;
    double x1 = 0.0;
    bool first = true;
    for (std::size_t k = i; k <= j; ++k) {
      if (ex[k] <= sx[k]) continue;
      if (first) {
        x0 = sx[k];
        x1 = ex[k];
        first = false;
      } else {
        x0 = std::min(x0, sx[k]);
        x1 = std::max(x1, ex[k]);
      }
    }
    sys.x_start = x0;
    sys.x_end = x1;

    systems.push_back(sys);
    i = j + 1;
  }
  return systems;
}

// 마디선을 찾는다. 판별식은 BarlineDetectParams 주석 참조 — 시스템 전체가
// 아니라 오선 하나하나를 기준으로 보고, 모든 오선이 동의할 때만 인정한다.
std::vector<Barline> DetectBarlinesInSystem(const BinaryImage& image,
                                            const StaffDetectResult& staves,
                                            const System& system,
                                            int system_index,
                                            const BarlineDetectParams& params) {
  std::vector<Barline> result;
  if (system.staff_count <= 0) return result;

  // 오선별 수직 범위를 모은다.
  struct Range {
    int y0;
    int y1;
  };
  std::vector<Range> ranges;
  ranges.reserve(static_cast<std::size_t>(system.staff_count));
  for (int k = 0; k < system.staff_count; ++k) {
    const std::size_t idx = static_cast<std::size_t>(system.first_staff + k);
    if (idx >= staves.staves.size()) continue;
    const Staff& st = staves.staves[idx];
    const int y0 = std::max(0, static_cast<int>(std::floor(st.y_top)));
    const int y1 =
        std::min(image.height, static_cast<int>(std::ceil(st.y_bottom)) + 1);
    if (y1 > y0) ranges.push_back(Range{y0, y1});
  }
  if (ranges.empty()) return result;

  const double start_margin =
      params.system_start_margin_ratio * system.staff_space;
  const int x0 = std::max(
      0, static_cast<int>(std::floor(system.x_start + start_margin)));
  const int x1 = std::min(image.width, static_cast<int>(std::ceil(system.x_end)) + 1);

  int run_start = -1;
  std::vector<Barline> raw;
  const auto close_run = [&](int end_exclusive) {
    if (run_start < 0) return;
    const int thickness = end_exclusive - run_start;
    if (thickness <= params.max_thickness) {
      Barline bar;
      bar.x = static_cast<double>(run_start) +
              0.5 * static_cast<double>(thickness - 1);
      bar.thickness = thickness;
      bar.system_index = system_index;
      raw.push_back(bar);
    }
    run_start = -1;
  };

  for (int x = x0; x < x1; ++x) {
    // 모든 오선에서 동시에 꽉 차야 한다 → 오선별 비율의 최솟값을 본다.
    double worst = 1.0;
    for (const Range& r : ranges) {
      int count = 0;
      for (int y = r.y0; y < r.y1; ++y) count += image.at(x, y);
      const double ratio =
          static_cast<double>(count) / static_cast<double>(r.y1 - r.y0);
      worst = std::min(worst, ratio);
    }
    if (worst >= params.staff_height_fraction) {
      if (run_start < 0) run_start = x;
    } else {
      close_run(x);
    }
  }
  close_run(x1);

  // 가까이 붙은 선들을 하나로 합친다. 겹세로줄·반복기호·종지선은 수직선
  // 여러 개로 그려지므로 묶지 않으면 마디가 과다 계수된다.
  // 대표 위치는 두께 가중 평균으로 잡는다 — 묶음의 기하학적 중심이 판본별
  // 그리기 방식에 가장 덜 휘둘린다.
  const double min_spacing = params.min_spacing_ratio * system.staff_space;
  std::size_t i = 0;
  while (i < raw.size()) {
    std::size_t j = i;
    while (j + 1 < raw.size() && raw[j + 1].x - raw[j].x < min_spacing) ++j;

    double weight_sum = 0.0;
    double weighted_x = 0.0;
    int thickest = 0;
    for (std::size_t k = i; k <= j; ++k) {
      const double w = static_cast<double>(raw[k].thickness);
      weighted_x += raw[k].x * w;
      weight_sum += w;
      thickest = std::max(thickest, raw[k].thickness);
    }
    Barline bar;
    bar.x = weight_sum > 0.0 ? weighted_x / weight_sum : raw[i].x;
    bar.thickness = thickest;
    bar.system_index = system_index;
    result.push_back(bar);
    i = j + 1;
  }
  return result;
}

// 마디 상자 안의 잉크에서 오선 획이 차지하는 몫을 뺀다. 오선은 모든 마디에
// 똑같이 존재하므로 빼지 않으면 밀도 지표가 마디 너비에만 비례하게 된다.
int MeasureInkDensity(const BinaryImage& image, const StaffDetectResult& staves,
                      const System& system, int x0, int x1, int y0, int y1) {
  std::vector<std::uint8_t> is_staff_row(
      static_cast<std::size_t>(std::max(0, y1 - y0)), 0);
  for (int s = system.first_staff; s < system.first_staff + system.staff_count; ++s) {
    if (s < 0 || static_cast<std::size_t>(s) >= staves.staves.size()) continue;
    for (int k = 0; k < 5; ++k) {
      const int center = static_cast<int>(std::lround(staves.staves[static_cast<std::size_t>(s)].line_y[k]));
      // 선 두께를 모르므로 중심 ±2행을 오선으로 본다. 보수적인 근사다.
      for (int dy = -2; dy <= 2; ++dy) {
        const int y = center + dy;
        if (y >= y0 && y < y1) {
          is_staff_row[static_cast<std::size_t>(y - y0)] = 1;
        }
      }
    }
  }

  int ink = 0;
  for (int y = y0; y < y1; ++y) {
    if (is_staff_row[static_cast<std::size_t>(y - y0)] != 0) continue;
    for (int x = x0; x < x1; ++x) ink += image.at(x, y);
  }
  return ink;
}

}  // namespace

PageAnalysis AnalyzePage(const BinaryImage& image,
                         const PageAnalysisParams& params) {
  PageAnalysis out;
  if (image.width <= 0 || image.height <= 0) return out;

  out.staves = DetectStaves(image, params.staff);
  out.systems = GroupSystems(image, out.staves, params.system);

  int measure_index = 0;
  for (std::size_t si = 0; si < out.systems.size(); ++si) {
    const System& sys = out.systems[si];
    if (sys.x_end <= sys.x_start) continue;

    const std::vector<Barline> bars =
        DetectBarlinesInSystem(image, out.staves, sys, static_cast<int>(si),
                               params.barline);

    // 마디 경계 = 시스템 좌측 끝 + 마디선들 + 시스템 우측 끝
    std::vector<double> edges;
    edges.push_back(sys.x_start);
    for (const Barline& bar : bars) {
      out.barlines.push_back(bar);
      if (bar.x > sys.x_start + 1.0 && bar.x < sys.x_end - 1.0) {
        edges.push_back(bar.x);
      }
    }
    edges.push_back(sys.x_end);
    std::sort(edges.begin(), edges.end());

    const int y0 = std::max(0, static_cast<int>(std::floor(sys.y_top)));
    const int y1 =
        std::min(image.height, static_cast<int>(std::ceil(sys.y_bottom)) + 1);

    for (std::size_t e = 0; e + 1 < edges.size(); ++e) {
      Measure m;
      m.index = measure_index++;
      m.system_index = static_cast<int>(si);
      m.x_start = edges[e];
      m.x_end = edges[e + 1];
      m.y_top = sys.y_top;
      m.y_bottom = sys.y_bottom;
      const int mx0 = std::max(0, static_cast<int>(std::floor(m.x_start)));
      const int mx1 = std::min(image.width, static_cast<int>(std::ceil(m.x_end)));
      m.ink_density =
          MeasureInkDensity(image, out.staves, sys, mx0, mx1, y0, y1);
      out.measures.push_back(m);
    }
  }
  return out;
}

}  // namespace nota
