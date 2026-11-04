#!/usr/bin/env python3
"""스캔·촬영 악보의 열화를 합성한다.

현재 검증 표본은 전부 디지털 조판이고, 이는 실사용 조건과 가장 먼 부분이다.
실제 사용자는 대개 스캔본이나 휴대폰으로 찍은 악보를 가지고 있다. 실제 스캔
표본을 모으기 전에, 어떤 열화에서 검출이 무너지는지 먼저 알아내기 위한 도구다.

합성이 실제 스캔을 대체하지는 못한다. 다만 "기울기 2도에서 깨진다" 같은 사실은
합성으로도 정확히 드러나며, 그것이 지금 필요한 정보다.

사용법:
    python3 prototype/degrade.py in.pgm out.pgm --skew 1.5 --shade 0.4 --noise 6
    python3 prototype/degrade.py in.pgm out.pgm --preset medium
"""
import argparse
import io
import sys

import numpy as np
from PIL import Image, ImageFilter

PRESETS = {
    # 평판 스캐너, 상태 좋음
    "light":  dict(skew=0.3, shade=0.15, noise=3,  blur=0.4, jpeg=0,  bleed=0.0),
    # 복사본을 스캔, 흔한 경우
    "medium": dict(skew=1.2, shade=0.35, noise=6,  blur=0.8, jpeg=75, bleed=0.08),
    # 휴대폰 촬영, 조명 불균일
    "heavy":  dict(skew=2.5, shade=0.60, noise=10, blur=1.4, jpeg=55, bleed=0.15),
}


def apply_shade(arr, strength, seed):
    """불균일 조명. 페이지 한쪽이 어둡거나 가운데가 접혀 그늘지는 상황."""
    if strength <= 0:
        return arr
    rng = np.random.default_rng(seed)
    h, w = arr.shape
    yy, xx = np.mgrid[0:h, 0:w]
    xn, yn = xx / max(w - 1, 1), yy / max(h - 1, 1)
    # 임의 방향의 선형 경사 + 한쪽 가장자리 그늘(제본 그림자)
    ax, ay = rng.uniform(-1, 1), rng.uniform(-1, 1)
    grad = ax * (xn - 0.5) + ay * (yn - 0.5)
    edge = np.exp(-((xn - rng.choice([0.0, 1.0])) ** 2) / 0.02)
    field = 1.0 - strength * (0.5 * (grad + 0.5) + 0.7 * edge)
    return np.clip(arr * np.clip(field, 0.25, 1.0), 0, 255)


def apply_bleed(img, strength, seed):
    """뒷면 비침. 좌우 반전한 희미한 복사본을 겹친다."""
    if strength <= 0:
        return img
    verso = img.transpose(Image.FLIP_LEFT_RIGHT).filter(ImageFilter.GaussianBlur(1.2))
    a = np.asarray(img, dtype=np.float32)
    b = np.asarray(verso, dtype=np.float32)
    # 어두운 쪽만 남기되 아주 약하게
    return Image.fromarray(np.clip(a - strength * (255.0 - b), 0, 255).astype(np.uint8))


def degrade(img, skew=0.0, shade=0.0, noise=0.0, blur=0.0, jpeg=0, bleed=0.0, seed=0):
    rng = np.random.default_rng(seed)

    if bleed > 0:
        img = apply_bleed(img, bleed, seed)

    if skew != 0.0:
        # 흰색으로 채우며 회전. 확대하지 않아 가장자리가 잘릴 수 있는데,
        # 실제 스캔에서도 같은 일이 일어난다.
        img = img.rotate(skew, resample=Image.BICUBIC, fillcolor=255)

    if blur > 0:
        img = img.filter(ImageFilter.GaussianBlur(blur))

    arr = np.asarray(img, dtype=np.float32)
    if shade > 0:
        arr = apply_shade(arr, shade, seed)
    if noise > 0:
        arr = np.clip(arr + rng.normal(0, noise, arr.shape), 0, 255)
    img = Image.fromarray(arr.astype(np.uint8))

    if jpeg > 0:
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=int(jpeg))
        buf.seek(0)
        img = Image.open(buf).convert("L")

    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--preset", choices=sorted(PRESETS))
    ap.add_argument("--skew", type=float, default=0.0, help="도 단위")
    ap.add_argument("--shade", type=float, default=0.0, help="0~1")
    ap.add_argument("--noise", type=float, default=0.0, help="회색조 표준편차")
    ap.add_argument("--blur", type=float, default=0.0, help="가우시안 반경 px")
    ap.add_argument("--jpeg", type=int, default=0, help="품질 1~100, 0이면 끔")
    ap.add_argument("--bleed", type=float, default=0.0, help="0~1")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    params = dict(PRESETS[args.preset]) if args.preset else {}
    for k in ("skew", "shade", "noise", "blur", "jpeg", "bleed"):
        v = getattr(args, k)
        if v:
            params[k] = v

    img = Image.open(args.src).convert("L")
    out = degrade(img, seed=args.seed, **params)
    out.save(args.dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
