// 호스트 전용 이미지 I/O. 코어에는 들어가지 않는다 (코어는 파일을 모른다).
// PGM/PPM만 다루는 이유: 외부 의존성 없이 pdftoppm 출력을 바로 읽을 수 있고,
// 검증 하네스에 그 이상이 필요하지 않다.
#pragma once

#include <string>

#include "nota/bitmap.hpp"

namespace nota_tools {

// P5 (바이너리 그레이스케일) PGM을 읽는다. 실패 시 width/height가 0.
nota::GrayImage ReadPgm(const std::string& path, std::string* error);

// P6 (바이너리 RGB) PPM으로 쓴다.
struct RgbImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgb;  // 3 bytes per pixel

  RgbImage() = default;
  RgbImage(int w, int h)
      : width(w),
        height(h),
        rgb(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3, 255) {}

  void set(int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const std::size_t i =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) * 3;
    rgb[i] = r;
    rgb[i + 1] = g;
    rgb[i + 2] = b;
  }
};

bool WritePpm(const std::string& path, const RgbImage& image, std::string* error);

}  // namespace nota_tools
