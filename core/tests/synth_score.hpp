// 합성 악보 생성기. 정답을 알고 있는 입력으로 검출기를 테스트한다.
//
// 실제 악보 PDF 표본으로 하는 검증(V1)은 이것을 대체하지 못하고, 그 반대도
// 아니다. 합성 영상은 "기하학적으로 올바른 입력에서 알고리즘이 정확한 값을
// 내는가"를 확인하고, 실제 악보는 "현실의 변형에서 견디는가"를 확인한다.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "nota/bitmap.hpp"

namespace nota_test {

struct SynthParams {
  int width = 1000;
  int margin_x = 80;
  int systems = 4;           // 시스템(대보표) 개수
  double staff_space = 12.0; // 오선 간격
  int line_thickness = 2;
  double intra_system_gap = 40.0;  // 대보표 내 두 오선 사이 간격
  double inter_system_gap = 120.0; // 시스템 사이 간격
  int top_margin = 100;
  int bottom_margin = 100;
  int noteheads_per_staff = 8;
  int barlines_per_system = 4;
  bool draw_noteheads = true;
  bool draw_barlines = true;
};

struct SynthScore {
  nota::GrayImage image;
  // 모든 오선의 정답 중심 y. 위에서 아래 순서.
  std::vector<double> line_centers;
  int staff_count = 0;  // 5선 묶음 개수 (= systems * 2)
};

namespace detail {

inline void FillRect(nota::GrayImage& img, int x0, int y0, int x1, int y1,
                     std::uint8_t value) {
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(img.width, x1);
  y1 = std::min(img.height, y1);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) img.at(x, y) = value;
  }
}

inline void FillEllipse(nota::GrayImage& img, double cx, double cy, double rx,
                        double ry, std::uint8_t value) {
  const int x0 = std::max(0, static_cast<int>(std::floor(cx - rx)));
  const int x1 = std::min(img.width, static_cast<int>(std::ceil(cx + rx)) + 1);
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - ry)));
  const int y1 = std::min(img.height, static_cast<int>(std::ceil(cy + ry)) + 1);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const double dx = (static_cast<double>(x) - cx) / rx;
      const double dy = (static_cast<double>(y) - cy) / ry;
      if (dx * dx + dy * dy <= 1.0) img.at(x, y) = value;
    }
  }
}

}  // namespace detail

inline SynthScore GenerateScore(const SynthParams& p) {
  const double staff_height = 4.0 * p.staff_space;
  const double system_height = 2.0 * staff_height + p.intra_system_gap;
  const double total =
      static_cast<double>(p.top_margin) +
      static_cast<double>(p.systems) * system_height +
      static_cast<double>(p.systems - 1) * p.inter_system_gap +
      static_cast<double>(p.bottom_margin);

  SynthScore out;
  out.image = nota::GrayImage(p.width, static_cast<int>(std::ceil(total)), 255);
  out.staff_count = p.systems * 2;

  const int x_start = p.margin_x;
  const int x_end = p.width - p.margin_x;
  const double half_thick = 0.5 * static_cast<double>(p.line_thickness - 1);

  double y = static_cast<double>(p.top_margin);
  for (int s = 0; s < p.systems; ++s) {
    const double system_top = y;
    // 대보표 = 오선 2개
    for (int staff = 0; staff < 2; ++staff) {
      const double staff_top = y + static_cast<double>(staff) *
                                       (staff_height + p.intra_system_gap);
      for (int k = 0; k < 5; ++k) {
        const double line_top = staff_top + static_cast<double>(k) * p.staff_space;
        const int iy = static_cast<int>(std::lround(line_top));
        detail::FillRect(out.image, x_start, iy, x_end, iy + p.line_thickness, 0);
        out.line_centers.push_back(static_cast<double>(iy) + half_thick);
      }

      if (p.draw_noteheads) {
        // 음표 머리를 선과 칸에 번갈아 놓는다. 투영에 잉크를 더하지만 폭이
        // 좁으므로 오선 임계를 넘지 못해야 한다. 이 테스트의 목적 중 하나다.
        const double span = static_cast<double>(x_end - x_start);
        for (int n = 0; n < p.noteheads_per_staff; ++n) {
          const double t = (static_cast<double>(n) + 0.5) /
                           static_cast<double>(p.noteheads_per_staff);
          const double cx = static_cast<double>(x_start) + t * span;
          // 0..8 위치(선/칸)를 순환
          const double step = static_cast<double>((n * 3) % 9) * 0.5;
          const double cy = staff_top + step * p.staff_space;
          detail::FillEllipse(out.image, cx, cy, 0.70 * p.staff_space,
                              0.50 * p.staff_space, 0);
          // 기둥
          const int sx = static_cast<int>(std::lround(cx + 0.70 * p.staff_space));
          detail::FillRect(out.image, sx, static_cast<int>(cy - 3.2 * p.staff_space),
                           sx + 2, static_cast<int>(cy), 0);
        }
      }
    }

    const double system_bottom = y + staff_height + p.intra_system_gap + staff_height;
    if (p.draw_barlines) {
      for (int b = 1; b <= p.barlines_per_system; ++b) {
        const double t = static_cast<double>(b) /
                         static_cast<double>(p.barlines_per_system);
        const int bx = x_start + static_cast<int>(
                                     t * static_cast<double>(x_end - x_start)) - 2;
        detail::FillRect(out.image, bx, static_cast<int>(system_top), bx + 2,
                         static_cast<int>(system_bottom) + p.line_thickness, 0);
      }
    }

    y = system_bottom + p.inter_system_gap;
  }

  return out;
}

}  // namespace nota_test
