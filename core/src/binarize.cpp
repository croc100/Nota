#include "nota/binarize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nota {
namespace {

// (width+1) x (height+1) 적분 영상. 경계 처리를 단순화하기 위해 0행/0열을 둔다.
struct Integrals {
  int w = 0;
  int h = 0;
  std::vector<std::uint64_t> sum;
  std::vector<std::uint64_t> sum_sq;

  std::size_t idx(int x, int y) const {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(w + 1) +
           static_cast<std::size_t>(x);
  }
};

Integrals BuildIntegrals(const GrayView& img) {
  Integrals it;
  it.w = img.width;
  it.h = img.height;
  const std::size_t n = static_cast<std::size_t>(img.width + 1) *
                        static_cast<std::size_t>(img.height + 1);
  it.sum.assign(n, 0);
  it.sum_sq.assign(n, 0);

  for (int y = 0; y < img.height; ++y) {
    std::uint64_t row_sum = 0;
    std::uint64_t row_sum_sq = 0;
    for (int x = 0; x < img.width; ++x) {
      const std::uint64_t v = img.at(x, y);
      row_sum += v;
      row_sum_sq += v * v;
      it.sum[it.idx(x + 1, y + 1)] = it.sum[it.idx(x + 1, y)] + row_sum;
      it.sum_sq[it.idx(x + 1, y + 1)] = it.sum_sq[it.idx(x + 1, y)] + row_sum_sq;
    }
  }
  return it;
}

// [x0, x1) x [y0, y1) 영역의 합
std::uint64_t RectSum(const std::vector<std::uint64_t>& table, int w, int x0,
                      int y0, int x1, int y1) {
  const auto at = [&](int x, int y) -> std::uint64_t {
    return table[static_cast<std::size_t>(y) * static_cast<std::size_t>(w + 1) +
                 static_cast<std::size_t>(x)];
  };
  return at(x1, y1) - at(x0, y1) - at(x1, y0) + at(x0, y0);
}

}  // namespace

BinaryImage BinarizeSauvola(const GrayView& image, const SauvolaParams& params) {
  if (!image.valid()) return BinaryImage();

  BinaryImage out(image.width, image.height);
  const Integrals it = BuildIntegrals(image);

  const int half = std::max(1, params.window / 2);

  for (int y = 0; y < image.height; ++y) {
    const int y0 = std::max(0, y - half);
    const int y1 = std::min(image.height, y + half + 1);
    for (int x = 0; x < image.width; ++x) {
      const int x0 = std::max(0, x - half);
      const int x1 = std::min(image.width, x + half + 1);

      const double area =
          static_cast<double>(x1 - x0) * static_cast<double>(y1 - y0);
      const double s = static_cast<double>(
          RectSum(it.sum, it.w, x0, y0, x1, y1));
      const double sq = static_cast<double>(
          RectSum(it.sum_sq, it.w, x0, y0, x1, y1));

      const double mean = s / area;
      // 수치 오차로 음수가 될 수 있으므로 하한을 둔다.
      const double variance = std::max(0.0, sq / area - mean * mean);
      const double stddev = std::sqrt(variance);

      const double threshold =
          mean * (1.0 + params.k * (stddev / params.r - 1.0));

      out.at(x, y) =
          static_cast<std::uint8_t>(static_cast<double>(image.at(x, y)) < threshold ? 1 : 0);
    }
  }
  return out;
}

}  // namespace nota
