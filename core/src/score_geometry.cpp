// 시간↔픽셀 매핑 구현. 설계 근거는 score_geometry.hpp 머리주석과
// docs/DESIGN.md 6절에 있다. 여기서는 그 결정을 숫자로 옮긴다.

#include "nota/score_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace nota {
namespace {

// 시스템에 시간을 배분할 때 쓰는 가중치. 0이 되면 beat_start == beat_end가
// 되어 보간이 0으로 나누기가 되므로, 어떤 기준을 쓰더라도 바닥을 둔다.
constexpr double kMinWeight = 1e-9;

// 앵커가 완전히 같아지면 BeatAt이 역함수가 되지 못한다(한 y에 두 박이 대응).
// 분석 결과에서 시스템 높이가 0이 되는 일은 정상적으로는 없지만, 품질이 낮은
// 페이지에서는 가능하다. 그 경우에도 매핑이 단조 증가를 유지하도록 최소 간격을
// 강제한다. 1 px보다 작게 두는 이유는 실제 좌표에 눈에 보이는 왜곡을 주지 않기
// 위해서다.
constexpr double kMinAnchorGap = 1e-3;

double ContentTop(const std::vector<System>& systems) {
  double top = systems.front().y_top;
  for (const System& s : systems) top = std::min(top, s.y_top);
  return top;
}

double ContentBottom(const std::vector<System>& systems) {
  double bottom = systems.front().y_bottom;
  for (const System& s : systems) bottom = std::max(bottom, s.y_bottom);
  return bottom;
}

// 잘라낸 페이지의 위아래에 남길 여백.
//
// 그 페이지의 시스템 사이 간격 중앙값의 절반을 쓴다. 그러면 페이지를 넘는
// 자리의 간격이 페이지 안의 간격과 같아지고, 페이지 경계에서 y 전진이 튀지
// 않는다. 간격을 알 수 없는 한 장짜리 페이지에서는 오선 간격으로 대신한다
// (오선 한 묶음의 높이가 8 × 오선 간격이므로 그 절반).
double PageMargin(const std::vector<System>& systems) {
  std::vector<const System*> sorted;
  sorted.reserve(systems.size());
  for (const System& s : systems) sorted.push_back(&s);
  std::sort(sorted.begin(), sorted.end(),
            [](const System* a, const System* b) { return a->y_top < b->y_top; });

  // 이웃한 두 시스템 사이의 실제 빈 공간.
  std::vector<double> gaps;
  for (std::size_t i = 1; i < sorted.size(); ++i) {
    gaps.push_back(sorted[i]->y_top - sorted[i - 1]->y_bottom);
  }
  if (!gaps.empty()) {
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    return std::max(0.0, gaps[gaps.size() / 2] / 2.0);
  }
  return 4.0 * std::max(0.0, systems.front().staff_space);
}

// 캔버스에서 시스템 i를 연주하는 동안 y가 가야 할 끝점.
//
// 시스템 i의 시작 y에서 시스템 i+1의 시작 y까지 흐르게 한다. 시스템 사이의
// 여백도 이 구간에 포함되므로, 한 시스템을 마쳤을 때 다음 시스템이 정확히
// 읽기 선에 와 있다. 마지막 시스템은 다음이 없으므로 자기 아래쪽 끝까지 간다.
double SystemEndAnchor(const std::vector<GeometrySystem>& systems,
                       std::size_t i) {
  if (i + 1 < systems.size()) return systems[i + 1].canvas_y_top;
  return systems[i].canvas_y_bottom;
}

}  // namespace

ScoreGeometry ScoreGeometry::Build(const std::vector<PageAnalysis>& pages,
                                   const std::vector<int>& page_heights,
                                   const ScoreGeometryParams& params) {
  ScoreGeometry geom;
  if (pages.size() != page_heights.size()) return geom;
  if (params.beats_per_measure <= 0.0) return geom;

  // 1. 페이지를 세로로 이어붙여 캔버스 좌표로 옮긴다.
  double canvas_cursor = 0.0;
  for (std::size_t p = 0; p < pages.size(); ++p) {
    const PageAnalysis& page = pages[p];

    GeometryPage slot;
    slot.index = static_cast<int>(p);
    slot.src_y_top = 0.0;
    slot.src_y_bottom = static_cast<double>(page_heights[p]);
    slot.has_content = !page.systems.empty();
    if (params.trim_page_margins) {
      // 내용 바깥의 빈 여백을 잘라낸다. 자르지 않으면 그 페이지의 마지막
      // 시스템을 연주하는 동안 빈 공간까지 지나야 하고, 실측에서 그 시스템의
      // y 전진이 피치의 449%까지 커졌다 (score_geometry.hpp).
      if (!slot.has_content) {
        // 시스템이 없는 페이지는 캔버스를 차지하지 않는다. 빈 페이지를 넣었다고
        // 스크롤이 빈 공간을 질주하게 만들 이유가 없다.
        slot.src_y_bottom = slot.src_y_top;
      } else {
        slot.src_y_top = ContentTop(page.systems);
        slot.src_y_bottom = ContentBottom(page.systems);
        const double margin = PageMargin(page.systems);
        slot.src_y_top = std::max(0.0, slot.src_y_top - margin);
        slot.src_y_bottom = std::min(static_cast<double>(page_heights[p]),
                                     slot.src_y_bottom + margin);
      }
    }
    slot.offset = canvas_cursor - slot.src_y_top;
    const double offset = slot.offset;
    geom.pages_.push_back(slot);

    // 페이지 안의 시스템은 위에서 아래 순서여야 한다. 분석기가 이미 그 순서로
    // 내놓지만, 매핑의 단조성이 여기에 의존하므로 믿지 않고 정렬한다.
    std::vector<int> order(page.systems.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&page](int a, int b) {
      return page.systems[static_cast<std::size_t>(a)].y_top <
             page.systems[static_cast<std::size_t>(b)].y_top;
    });

    for (std::size_t k = 0; k < order.size(); ++k) {
      const int src_index = order[k];
      const System& sys = page.systems[static_cast<std::size_t>(src_index)];

      GeometrySystem out;
      out.page_index = static_cast<int>(p);
      out.index_in_page = src_index;
      out.canvas_y_top = sys.y_top + offset;
      out.canvas_y_bottom = sys.y_bottom + offset;
      out.x_start = sys.x_start;
      out.x_end = sys.x_end;
      out.staff_space = sys.staff_space;

      for (const Measure& m : page.measures) {
        if (m.system_index != src_index) continue;
        ++out.measure_count;
        out.ink_density += m.ink_density;
      }
      geom.systems_.push_back(out);
    }

    const double span = slot.src_y_bottom - slot.src_y_top;
    canvas_cursor += span;
    // 내용이 없는 페이지에는 간격도 주지 않는다.
    if (span > 0.0) canvas_cursor += params.page_gap;
  }
  if (geom.systems_.empty()) return geom;

  geom.canvas_height_ = std::max(0.0, canvas_cursor - params.page_gap);

  // 2. 시스템에 악보상 시간을 배분한다.
  //
  // 총 시간은 어느 기준을 쓰든 같다 — 검출된 마디 수 × 박자. 기준이 바꾸는
  // 것은 그 총량을 시스템들에 어떻게 나누는가뿐이다. 이렇게 두면 배분 기준을
  // 바꾸어도 곡 길이가 흔들리지 않는다.
  int total_measures = 0;
  for (const GeometrySystem& s : geom.systems_) total_measures += s.measure_count;
  // 마디선을 전혀 못 찾은 경우(눈금 없음): 시스템 하나를 한 마디로 센다.
  // 매핑은 거칠어지지만 시스템 골격은 여전히 맞다 (DESIGN.md 6절).
  if (total_measures == 0) total_measures = static_cast<int>(geom.systems_.size());
  // 축척은 분포와 분리한다. 외부에서 길이를 주면 그것을 쓴다 — 과검출이
  // 축척 오차가 되지 않게 하는 유일한 장치다 (score_geometry.hpp).
  geom.scale_from_detection_ = !(params.total_beats_override > 0.0);
  geom.total_beats_ =
      geom.scale_from_detection_
          ? static_cast<double>(total_measures) * params.beats_per_measure
          : params.total_beats_override;

  std::vector<double> weights(geom.systems_.size(), 0.0);
  double weight_sum = 0.0;
  const bool use_ink =
      params.time_weight == ScoreGeometryParams::TimeWeight::kInkDensity;
  for (std::size_t i = 0; i < geom.systems_.size(); ++i) {
    const GeometrySystem& s = geom.systems_[i];
    double w = use_ink ? static_cast<double>(s.ink_density)
                       : static_cast<double>(s.measure_count);
    if (w <= 0.0) w = kMinWeight;
    weights[i] = w;
    weight_sum += w;
  }
  // 잉크가 전혀 없는 입력(빈 페이지)에서는 잉크 기준이 성립하지 않는다.
  // 시스템 균등 배분으로 떨어진다.
  if (weight_sum <= 0.0) {
    std::fill(weights.begin(), weights.end(), 1.0);
    weight_sum = static_cast<double>(weights.size());
  }

  double beat_cursor = 0.0;
  for (std::size_t i = 0; i < geom.systems_.size(); ++i) {
    GeometrySystem& s = geom.systems_[i];
    s.beat_start = beat_cursor;
    // 마지막 시스템은 누적 오차가 남지 않도록 총 박에 딱 맞춘다.
    beat_cursor = (i + 1 == geom.systems_.size())
                      ? geom.total_beats_
                      : beat_cursor + geom.total_beats_ * weights[i] / weight_sum;
    s.beat_end = beat_cursor;
  }

  // 3. 보간 앵커. 길이는 시스템 수 + 1이고 단조 증가를 보장한다.
  geom.anchors_.resize(geom.systems_.size() + 1);
  geom.anchors_[0] = geom.systems_.front().canvas_y_top;
  for (std::size_t i = 0; i < geom.systems_.size(); ++i) {
    const double end = SystemEndAnchor(geom.systems_, i);
    geom.anchors_[i + 1] = std::max(end, geom.anchors_[i] + kMinAnchorGap);
  }

  // 4. 마디 눈금. 시스템 안에서 가로 폭에 비례해 시간을 나눈다.
  //
  // 마디의 가로 폭은 조판기가 그 마디의 음표를 담기 위해 준 공간이므로 음악적
  // 길이의 꽤 좋은 대리 지표다. 과검출된 마디선이 섞이면 눈금 간격이 고르지
  // 않아질 뿐, 단조성은 깨지지 않는다 (DESIGN.md 6절).
  int global_measure_index = 0;
  for (std::size_t i = 0; i < geom.systems_.size(); ++i) {
    const GeometrySystem& s = geom.systems_[i];
    const PageAnalysis& page = pages[static_cast<std::size_t>(s.page_index)];

    std::vector<const Measure*> in_system;
    for (const Measure& m : page.measures) {
      if (m.system_index == s.index_in_page) in_system.push_back(&m);
    }
    if (in_system.empty()) continue;
    std::sort(in_system.begin(), in_system.end(),
              [](const Measure* a, const Measure* b) {
                return a->x_start < b->x_start;
              });

    double width_sum = 0.0;
    for (const Measure* m : in_system) {
      width_sum += std::max(0.0, m->x_end - m->x_start);
    }
    const bool equal_split = width_sum <= 0.0;

    const double page_offset = s.canvas_y_top - page.systems[
        static_cast<std::size_t>(s.index_in_page)].y_top;

    double cursor = s.beat_start;
    for (std::size_t k = 0; k < in_system.size(); ++k) {
      const Measure* m = in_system[k];
      GeometryMeasure out;
      out.index = global_measure_index++;
      out.system_index = static_cast<int>(i);
      out.x_start = m->x_start;
      out.x_end = m->x_end;
      out.canvas_y_top = m->y_top + page_offset;
      out.canvas_y_bottom = m->y_bottom + page_offset;
      out.ink_density = m->ink_density;
      out.beat_start = cursor;
      if (k + 1 == in_system.size()) {
        cursor = s.beat_end;
      } else {
        const double share =
            equal_split ? 1.0 / static_cast<double>(in_system.size())
                        : std::max(0.0, m->x_end - m->x_start) / width_sum;
        cursor += (s.beat_end - s.beat_start) * share;
      }
      out.beat_end = cursor;
      geom.measures_.push_back(out);
    }
  }

  return geom;
}

int ScoreGeometry::SystemAt(double beat) const {
  if (systems_.empty()) return 0;
  if (!(beat > systems_.front().beat_start)) return 0;
  if (beat >= systems_.back().beat_end) {
    return static_cast<int>(systems_.size()) - 1;
  }
  // beat_start 기준 이분 탐색. beat_start는 단조 증가한다.
  std::size_t lo = 0;
  std::size_t hi = systems_.size() - 1;
  while (lo < hi) {
    const std::size_t mid = (lo + hi + 1) / 2;
    if (systems_[mid].beat_start <= beat) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return static_cast<int>(lo);
}

double ScoreGeometry::CanvasY(double beat) const {
  if (systems_.empty()) return 0.0;
  if (!(beat > 0.0)) return anchors_.front();
  if (beat >= total_beats_) return anchors_.back();

  const std::size_t i = static_cast<std::size_t>(SystemAt(beat));
  const GeometrySystem& s = systems_[i];
  const double span = s.beat_end - s.beat_start;
  if (span <= 0.0) return anchors_[i];
  const double t = (beat - s.beat_start) / span;
  return anchors_[i] + t * (anchors_[i + 1] - anchors_[i]);
}

double ScoreGeometry::BeatAt(double canvas_y) const {
  if (systems_.empty()) return 0.0;
  if (canvas_y <= anchors_.front()) return 0.0;
  if (canvas_y >= anchors_.back()) return total_beats_;

  // anchors_는 강하게 단조 증가하므로 이분 탐색이 성립한다.
  std::size_t lo = 0;
  std::size_t hi = systems_.size() - 1;
  while (lo < hi) {
    const std::size_t mid = (lo + hi + 1) / 2;
    if (anchors_[mid] <= canvas_y) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  const GeometrySystem& s = systems_[lo];
  const double y_span = anchors_[lo + 1] - anchors_[lo];
  if (y_span <= 0.0) return s.beat_start;
  const double t = (canvas_y - anchors_[lo]) / y_span;
  return s.beat_start + t * (s.beat_end - s.beat_start);
}

double ScoreGeometry::PixelsPerBeat(double beat) const {
  if (systems_.empty()) return 0.0;
  const std::size_t i = static_cast<std::size_t>(SystemAt(beat));
  const GeometrySystem& s = systems_[i];
  const double span = s.beat_end - s.beat_start;
  if (span <= 0.0) return 0.0;
  return (anchors_[i + 1] - anchors_[i]) / span;
}

}  // namespace nota
