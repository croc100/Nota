// 악보 이진화. docs/ALGORITHM.md 5.1절.
//
// Sauvola 적응 임계를 쓴다. 전역 임계(Otsu 등)는 스캔 악보의 음영과 얼룩에서
// 깨진다 — 페이지 한쪽이 어두우면 그쪽 오선이 통째로 사라지거나, 밝은 쪽의
// 종이 질감이 잉크로 잡힌다. Sauvola는 국소 평균과 표준편차를 함께 보므로
// 균일한 배경(표준편차 작음)을 잉크로 오판하지 않는다.
#pragma once

#include "nota/bitmap.hpp"

namespace nota {

struct SauvolaParams {
  // 국소 윈도우 한 변의 길이(픽셀). 오선 간격의 3~4배가 적당하다.
  // 200 DPI 악보의 오선 간격이 보통 10~14 px이므로 기본값을 41로 둔다.
  int window = 41;
  // 표준편차 항의 가중. 클수록 보수적(잉크를 덜 잡는다). 통상 0.2~0.5.
  double k = 0.2;
  // 표준편차 정규화 상수. 8비트 영상에서 관례적으로 128.
  double r = 128.0;
};

// Sauvola 적응 임계로 이진화한다. 적분 영상을 쓰므로 윈도우 크기와 무관하게
// O(width * height)다.
BinaryImage BinarizeSauvola(const GrayView& image, const SauvolaParams& params = {});

}  // namespace nota
