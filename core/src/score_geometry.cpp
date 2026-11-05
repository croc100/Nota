#include "nota/score_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace nota {
namespace {

double Lerp(double a, double b, double t) { return a + (b - a) * t; }

// 0으로 나누는 것을 막는다. 구간 길이가 0인 시스템(마디가 하나도 없는 등)은
// 정상 입력에서 나오지 않지만, 깨진 검출 결과가 들어와도 좌표가 NaN이 되어
// 스크롤이 날아가는 일은 없어야 한다.
double SafeRatio(double numerator, double denominator) {
  return denominator > 1e-12 ? numerator / denominator : 0.0;
}

}  // namespace

ScoreGeometry ScoreGeometry::Build(const std::vector<PageAnalysis>& pages,
                                   const std::vector<int>& page_heights,
                                   const ScoreGeometryParams& params) {
  ScoreGeometry out;
  if (pages.empty() || pages.size() != page_heights.size()) return out;

  // --- 1. 페이지를 세로로 이어붙인다 -----------------------------------------
  std::vector<double> page_offset(pages.size(), 0.0);
  double cursor = 0.0;
  for (std::size_t p = 0; p < pages.size(); ++p) {
    page_offset[p] = cursor;
    cursor += static_cast<double>(std::max(0, page_heights[p]));
    if (p + 1 < pages.size()) cursor += params.page_gap;
  }
  out.canvas_height_ = cursor;

  // --- 2. 시스템과 마디를 캔버스 좌표로 펼친다 -------------------------------
  // 시스템은 페이지 순서, 페이지 안에서는 위에서 아래 순서로 쌓인다. 이 순서가
  // 곧 연주 순서이고, anchors_의 단조성을 보장한다.
  for (std::size_t p = 0; p < pages.size(); ++p) {
    const PageAnalysis& page = pages[p];
    const double dy = page_offset[p];

    // 이 페이지의 시스템이 전체 목록에서 시작하는 위치.
    const int system_base = static_cast<int>(out.systems_.size());

    for (std::size_t i = 0; i < page.systems.size(); ++i) {
      const System& src = page.systems[i];
      GeometrySystem sys;
      sys.page_index = static_cast<int>(p);
      sys.index_in_page = static_cast<int>(i);
      sys.canvas_y_top = src.y_top + dy;
      sys.canvas_y_bottom = src.y_bottom + dy;
      sys.x_start = src.x_start;
      sys.x_end = src.x_end;
      sys.staff_space = src.staff_space;
      out.systems_.push_back(sys);
    }

    for (const Measure& src : page.measures) {
      if (src.system_index < 0 ||
          static_cast<std::size_t>(src.system_index) >= page.systems.size()) {
        continue;
      }
      GeometryMeasure m;
      m.index = static_cast<int>(out.measures_.size());
      m.system_index = system_base + src.system_index;
      m.x_start = src.x_start;
      m.x_end = src.x_end;
      m.canvas_y_top = src.y_top + dy;
      m.canvas_y_bottom = src.y_bottom + dy;
      m.ink_density = src.ink_density;
      out.measures_.push_back(m);

      GeometrySystem& sys = out.systems_[static_cast<std::size_t>(m.system_index)];
      ++sys.measure_count;
      sys.ink_density += src.ink_density;
    }
  }

  if (out.systems_.empty()) {
    out.canvas_height_ = cursor;
    return out;
  }

  // --- 3. 악보상 시간을 배분한다 ---------------------------------------------
  // 전체 길이는 두 방식 모두 "마디 수 x 마디당 박"으로 같다. 박자표가 고정이면
  // 그것이 실제 길이이기 때문이다. 다른 것은 그 길이를 시스템에 나누는 비율뿐이다.
  const auto weight_of = [&](const GeometrySystem& sys) -> double {
    if (params.time_weight == ScoreGeometryParams::TimeWeight::kInkDensity) {
      return static_cast<double>(std::max<long long>(sys.ink_density, 1));
    }
    return static_cast<double>(std::max(sys.measure_count, 1));
  };

  int total_measures = 0;
  double total_weight = 0.0;
  for (const GeometrySystem& sys : out.systems_) {
    total_measures += sys.measure_count;
    total_weight += weight_of(sys);
  }
  // 마디를 하나도 못 찾았으면 시스템 하나를 한 마디로 친다. 매핑이 없는 것보다
  // 거친 매핑이 낫다 — 적어도 시스템 단위 골격은 유효하다.
  if (total_measures == 0) {
    total_measures = static_cast<int>(out.systems_.size());
  }
  out.total_beats_ =
      static_cast<double>(total_measures) * params.beats_per_measure;

  double beat_cursor = 0.0;
  for (std::size_t i = 0; i < out.systems_.size(); ++i) {
    GeometrySystem& sys = out.systems_[i];
    const double share = SafeRatio(weight_of(sys), total_weight);
    sys.beat_start = beat_cursor;
    // 마지막 시스템은 누적 오차가 남지 않도록 끝에 딱 맞춘다.
    sys.beat_end = (i + 1 == out.systems_.size())
                       ? out.total_beats_
                       : beat_cursor + share * out.total_beats_;
    beat_cursor = sys.beat_end;
  }

  // 시스템 안에서 마디에 시간을 나눈다. 시스템 바깥의 절대 시간은 이미 정해져
  // 있으므로, 여기서의 배분은 시스템 내부 보간에만 영향을 준다.
  for (std::size_t s = 0; s < out.systems_.size(); ++s) {
    const GeometrySystem& sys = out.systems_[s];
    double inner_weight = 0.0;
    for (GeometryMeasure& m : out.measures_) {
      if (static_cast<std::size_t>(m.system_index) != s) continue;
      inner_weight +=
          params.time_weight == ScoreGeometryParams::TimeWeight::kInkDensity
              ? static_cast<double>(std::max(m.ink_density, 1))
              : 1.0;
    }
    double c = sys.beat_start;
    for (GeometryMeasure& m : out.measures_) {
      if (static_cast<std::size_t>(m.system_index) != s) continue;
      const double w =
          params.time_weight == ScoreGeometryParams::TimeWeight::kInkDensity
              ? static_cast<double>(std::max(m.ink_density, 1))
              : 1.0;
      m.beat_start = c;
      c += SafeRatio(w, inner_weight) * (sys.beat_end - sys.beat_start);
      m.beat_end = c;
    }
  }

  // --- 4. 보간 기준점 --------------------------------------------------------
  // 시스템 i의 시간 구간은 y를 anchors_[i]에서 anchors_[i+1]까지 옮긴다. 즉 한
  // 시스템을 연주하는 동안 다음 시스템이 그 자리로 올라온다 (헤더 주석 참조).
  //
  // 마지막 기준점만 다음 시스템이 없으므로 그 시스템의 아래 끝으로 둔다. 곡이
  // 끝난 뒤에도 계속 흘러내려가지 않게 하는 효과가 있다.
  out.anchors_.reserve(out.systems_.size() + 1);
  for (const GeometrySystem& sys : out.systems_) {
    out.anchors_.push_back(sys.canvas_y_top);
  }
  out.anchors_.push_back(out.systems_.back().canvas_y_bottom);

  // 단조성은 호출부가 기대하는 불변식이다. 검출이 깨져 순서가 뒤집힌 경우에도
  // 역함수가 성립하도록 강제한다.
  for (std::size_t i = 1; i < out.anchors_.size(); ++i) {
    out.anchors_[i] = std::max(out.anchors_[i], out.anchors_[i - 1]);
  }

  return out;
}

int ScoreGeometry::SystemAt(double beat) const {
  if (systems_.empty()) return 0;
  if (beat <= systems_.front().beat_start) return 0;
  if (beat >= systems_.back().beat_end) {
    return static_cast<int>(systems_.size()) - 1;
  }
  // beat_start로 정렬되어 있으므로 이분 탐색.
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
  const double clamped = std::clamp(beat, 0.0, total_beats_);
  const std::size_t i = static_cast<std::size_t>(SystemAt(clamped));
  const GeometrySystem& sys = systems_[i];
  const double t =
      SafeRatio(clamped - sys.beat_start, sys.beat_end - sys.beat_start);
  return Lerp(anchors_[i], anchors_[i + 1], std::clamp(t, 0.0, 1.0));
}

double ScoreGeometry::BeatAt(double canvas_y) const {
  if (systems_.empty()) return 0.0;
  const double lo_y = anchors_.front();
  const double hi_y = anchors_.back();
  const double clamped = std::clamp(canvas_y, lo_y, hi_y);

  // anchors_는 단조 증가하므로 구간을 이분 탐색할 수 있다.
  const auto it =
      std::upper_bound(anchors_.begin(), anchors_.end(), clamped);
  std::size_t i = static_cast<std::size_t>(it - anchors_.begin());
  if (i == 0) {
    i = 0;
  } else {
    --i;
  }
  i = std::min(i, systems_.size() - 1);

  const double t = SafeRatio(clamped - anchors_[i], anchors_[i + 1] - anchors_[i]);
  const GeometrySystem& sys = systems_[i];
  return Lerp(sys.beat_start, sys.beat_end, std::clamp(t, 0.0, 1.0));
}

double ScoreGeometry::PixelsPerBeat(double beat) const {
  if (systems_.empty()) return 0.0;
  const std::size_t i =
      static_cast<std::size_t>(SystemAt(std::clamp(beat, 0.0, total_beats_)));
  const GeometrySystem& sys = systems_[i];
  return SafeRatio(anchors_[i + 1] - anchors_[i], sys.beat_end - sys.beat_start);
}

}  // namespace nota
