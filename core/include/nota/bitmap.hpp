// 코어가 다루는 이미지 타입. 플랫폼 이미지 타입(CGImage, Bitmap 등)에 의존하지
// 않는다. 호출자가 래스터화 결과를 이 형태로 넘긴다.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nota {

// 8비트 그레이스케일 비소유 뷰. 0 = 검정, 255 = 흰색.
struct GrayView {
  const std::uint8_t* data = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;  // 바이트 단위 행 간격. 보통 width와 같다.

  bool valid() const {
    return data != nullptr && width > 0 && height > 0 && stride >= width;
  }
  std::uint8_t at(int x, int y) const {
    return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(x)];
  }
};

// 8비트 그레이스케일 소유 이미지. 테스트와 도구에서 쓴다.
struct GrayImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;

  GrayImage() = default;
  GrayImage(int w, int h, std::uint8_t fill = 255)
      : width(w),
        height(h),
        pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill) {}

  std::uint8_t& at(int x, int y) {
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
  }
  std::uint8_t at(int x, int y) const {
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
  }
  GrayView view() const { return GrayView{pixels.data(), width, height, width}; }
};

// 이진 이미지. 1 = 잉크(악보의 검은 획), 0 = 배경.
struct BinaryImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> ink;

  BinaryImage() = default;
  BinaryImage(int w, int h)
      : width(w),
        height(h),
        ink(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0) {}

  std::uint8_t& at(int x, int y) {
    return ink[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)];
  }
  std::uint8_t at(int x, int y) const {
    return ink[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)];
  }
};

}  // namespace nota
