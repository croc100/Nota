#include "nota/skew.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nota {
namespace {

constexpr double kPi = 3.14159265358979323846;

// 전단(shear)으로 회전을 근사한다. 작은 각도에서 오차가 무시할 수준이고,
// 픽셀 보간 없이 정수 연산만으로 끝나므로 탐색을 수십 번 반복할 수 있다.
//
// 각 잉크 픽셀 (x, y)를 행 (y - x*tan θ)에 넣고, 그 히스토그램의 제곱합을 본다.
double ProjectionConcentration(const std::vector<std::int32_t>& xs,
                               const std::vector<std::int32_t>& ys,
                               int height, int width, double degrees,
                               std::vector<std::int32_t>* scratch) {
  const double t = std::tan(degrees * kPi / 180.0);
  // 전단으로 행 번호가 범위를 벗어날 수 있으므로 여유를 둔다.
  const int margin = static_cast<int>(std::ceil(std::fabs(t) * width)) + 2;
  const std::size_t bins = static_cast<std::size_t>(height + 2 * margin);
  scratch->assign(bins, 0);

  for (std::size_t i = 0; i < xs.size(); ++i) {
    const double shifted = static_cast<double>(ys[i]) -
                           t * static_cast<double>(xs[i]) +
                           static_cast<double>(margin);
    const int bin = static_cast<int>(shifted + 0.5);
    if (bin >= 0 && static_cast<std::size_t>(bin) < bins) {
      ++(*scratch)[static_cast<std::size_t>(bin)];
    }
  }

  double sum_sq = 0.0;
  for (const std::int32_t v : *scratch) {
    const double d = static_cast<double>(v);
    sum_sq += d * d;
  }
  return sum_sq;
}

}  // namespace

double EstimateSkewDegrees(const BinaryImage& image, const SkewParams& params) {
  if (image.width <= 0 || image.height <= 0) return 0.0;

  // 잉크 픽셀 좌표를 한 번만 모은다. 이후 각도마다 이 목록만 훑으므로
  // 탐색 비용이 이미지 크기가 아니라 잉크 양에 비례한다.
  const int stride = std::max(1, params.column_stride);
  std::vector<std::int32_t> xs;
  std::vector<std::int32_t> ys;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; x += stride) {
      if (image.at(x, y) != 0) {
        xs.push_back(x);
        ys.push_back(y);
      }
    }
  }
  if (xs.empty()) return 0.0;

  std::vector<std::int32_t> scratch;
  const auto search = [&](double lo, double hi, double step) {
    double best_angle = 0.0;
    double best_score = -1.0;
    for (double a = lo; a <= hi + 1e-9; a += step) {
      const double s = ProjectionConcentration(xs, ys, image.height, image.width,
                                               a, &scratch);
      if (s > best_score) {
        best_score = s;
        best_angle = a;
      }
    }
    return best_angle;
  };

  const double coarse =
      search(-params.max_degrees, params.max_degrees, params.coarse_step);
  return search(coarse - params.coarse_step, coarse + params.coarse_step,
                params.fine_step);
}

namespace {

template <typename Sampler>
void RotateInto(int width, int height, double degrees, Sampler sample) {
  const double rad = -degrees * kPi / 180.0;
  const double c = std::cos(rad);
  const double s = std::sin(rad);
  const double cx = 0.5 * static_cast<double>(width - 1);
  const double cy = 0.5 * static_cast<double>(height - 1);

  for (int y = 0; y < height; ++y) {
    const double dy = static_cast<double>(y) - cy;
    for (int x = 0; x < width; ++x) {
      const double dx = static_cast<double>(x) - cx;
      // 역변환: 출력 좌표에 대응하는 입력 좌표를 찾는다.
      const double sxf = c * dx + s * dy + cx;
      const double syf = -s * dx + c * dy + cy;
      sample(x, y, sxf, syf);
    }
  }
}

}  // namespace

GrayImage RotateGray(const GrayView& image, double degrees, std::uint8_t fill) {
  GrayImage out(image.width, image.height, fill);
  if (!image.valid()) return out;

  RotateInto(image.width, image.height, degrees,
             [&](int x, int y, double sxf, double syf) {
               // 쌍선형 보간. 회색조에서는 계조가 있으므로 보간이 유효하고,
               // 이후 Sauvola가 다시 이진화하므로 획이 끊기지 않는다.
               const int x0 = static_cast<int>(std::floor(sxf));
               const int y0 = static_cast<int>(std::floor(syf));
               if (x0 < 0 || y0 < 0 || x0 + 1 >= image.width ||
                   y0 + 1 >= image.height) {
                 return;
               }
               const double fx = sxf - static_cast<double>(x0);
               const double fy = syf - static_cast<double>(y0);
               const double v =
                   (1 - fx) * (1 - fy) * image.at(x0, y0) +
                   fx * (1 - fy) * image.at(x0 + 1, y0) +
                   (1 - fx) * fy * image.at(x0, y0 + 1) +
                   fx * fy * image.at(x0 + 1, y0 + 1);
               out.at(x, y) = static_cast<std::uint8_t>(v + 0.5);
             });
  return out;
}

BinaryImage RotateBinary(const BinaryImage& image, double degrees) {
  BinaryImage out(image.width, image.height);
  if (image.width <= 0 || image.height <= 0) return out;

  RotateInto(image.width, image.height, degrees,
             [&](int x, int y, double sxf, double syf) {
               const int sx = static_cast<int>(sxf + 0.5);
               const int sy = static_cast<int>(syf + 0.5);
               if (sx < 0 || sy < 0 || sx >= image.width || sy >= image.height) {
                 return;
               }
               out.at(x, y) = image.at(sx, sy);
             });
  return out;
}

}  // namespace nota
