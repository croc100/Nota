// 기울기 추정과 보정. docs/ALGORITHM.md 5.1절.
//
// 오선 검출은 행별 투영에 기대므로 회전에 극도로 취약하다. 실측(Bach BWV846,
// Chopin Op.6-2):
//
//   0.00도  오선 14/14 검출
//   0.25도  오선 12/14 검출 (이미 손상)
//   0.50도  오선  0/14 검출 (완전 붕괴)
//
// 폭 1500 px 페이지에서 0.5도는 좌우 끝의 높이 차가 13 px이다. 오선 간격이
// 14 px이므로 한 선이 다음 선 자리까지 내려간다. 투영 피크가 뭉개져 사라진다.
//
// 실제 스캔본은 통상 0.5~2도 기울어 있다. 따라서 이 보정 없이는 스캔 악보를
// 전혀 다룰 수 없다.
#pragma once

#include <cstdint>

#include "nota/bitmap.hpp"

namespace nota {

struct SkewParams {
  // 탐색 범위 (± 도). 이보다 많이 기운 입력은 사용자가 회전시켜야 한다.
  double max_degrees = 5.0;
  // 1단계 거친 탐색 간격 (도).
  double coarse_step = 0.25;
  // 2단계 정밀 탐색 간격 (도). 최종 정확도를 좌우한다.
  double fine_step = 0.02;
  // 추정 시 열을 건너뛰는 간격. 2면 절반만 본다. 정확도 손실 없이 빨라진다.
  int column_stride = 2;
};

// 행별 투영의 집중도가 최대가 되는 각도를 찾는다.
//
// 원리: 오선이 수평일 때 투영은 뾰족한 피크 몇 개에 몰린다. 기울면 같은 잉크가
// 여러 행으로 퍼지므로 피크가 낮아진다. 따라서 투영의 제곱합을 최대화하는
// 각도가 올바른 기울기다. 잉크 총량은 각도와 무관하므로 제곱합 최대화는
// 분산 최대화와 같다.
//
// 반환값은 도 단위. 양수면 이미지가 시계 반대 방향으로 기울어 있다는 뜻이며,
// 보정하려면 이 각도만큼 되돌려 회전해야 한다.
double EstimateSkewDegrees(const BinaryImage& image, const SkewParams& params = {});

// 이미지 중심을 기준으로 회전한다. 크기는 유지한다.
GrayImage RotateGray(const GrayView& image, double degrees, std::uint8_t fill = 255);
BinaryImage RotateBinary(const BinaryImage& image, double degrees);

}  // namespace nota
