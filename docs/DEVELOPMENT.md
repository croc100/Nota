# 개발 환경과 현재 상태

## 1. 지금 필요한 것은 C++ 하나다

`NotaCore`는 플랫폼 중립 C++17이므로 Xcode 없이 빌드·테스트된다. iOS 작업
(Swift/SwiftUI)은 1단계부터, Android(Kotlin/Compose)는 3단계부터 필요하다
(`DESIGN.md` 11절).

### macOS 설치

```
xcode-select --install          # clang, make
brew install cmake ninja
```

검증 하네스(`prototype/`)를 돌리려면 추가로:

```
brew install imagemagick
pip3 install verovio music21 cairosvg numpy pillow
```

### Linux 설치

```
sudo apt install build-essential cmake ninja-build imagemagick
pip3 install verovio music21 cairosvg numpy pillow
```

외부 C++ 의존성은 없다. 테스트 하네스도 자체 구현이라 네트워크 없이 빌드된다
(`core/tests/test_framework.hpp`에 그 이유가 있다).

## 2. 빌드와 테스트

```
cmake -S . -B build -G Ninja
cmake --build build
./build/core/tests/nota_core_tests
```

경고를 엄격히 켜 두었다 (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`).
경고 0건을 유지한다.

### Sanitizer

실시간 링 버퍼처럼 동시성이 얽힌 코드는 반드시 이쪽으로도 돌린다.

```
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNOTA_SANITIZE=ON
cmake --build build-asan
./build-asan/core/tests/nota_core_tests
```

## 3. 검증 하네스

알고리즘을 Python으로 다시 구현하지 않는다. 프로덕션 코어를 CLI로 호출하고
채점만 한다. 검증하는 코드와 배포하는 코드가 같아야 하기 때문이다
(`DESIGN.md` 9.3).

### 검증용 악보 생성

music21 코퍼스의 실제 피아노 작품을 Verovio로 조판한다. 정답(오선·마디선
위치)을 SVG에서 직접 뽑으므로 수작업 라벨링이 없다.

```
python3 prototype/make_fixtures.py --out prototype/out/scores
```

### V1 / V2 채점

```
python3 prototype/evaluate_page.py \
    prototype/out/scores/bach_bwv846_p1.svg \
    prototype/out/scores/bach_bwv846_p1.pgm --verbose
```

기대값은 `ALGORITHM.md` 8.0절의 표와 같아야 한다.

### 열화 내성

```
python3 prototype/sweep_robustness.py
```

기대값은 `ALGORITHM.md` 8.1절의 표와 같다. 모든 프리셋에서 시스템·오선
검출이 100%여야 한다.

### 한 페이지만 직접 보기

```
./build/tools/nota-analyze page.pgm --overlay overlay.ppm
```

PDF에서 PGM을 만들려면:

```
pdftoppm -gray -r 200 -f 1 -l 1 score.pdf page   # page-1.pgm
```

오버레이 색: 초록=검출된 오선, 주황=묶이지 못한 수평선(오검출 신호),
빨강=마디선, 보라=시스템 좌우 끝, 상단 띠=마디 경계.

## 4. 현재 상태

### 끝난 것

| 모듈 | 내용 |
|---|---|
| `nota::RingBuffer` | lock-free SPSC. peek/consume 분리로 STFT 윈도우 겹침 지원 |
| `nota::BinarizeSauvola` | 적분 영상 기반 적응 임계 |
| `nota::EstimateSkewDegrees` / `RotateGray` | 기울기 추정·보정 |
| `nota::DetectStaves` | 오선 검출 |
| `nota::AnalyzePageGray` | 전처리~마디 상자까지 전체 경로 + 자체 품질 평가 |
| `nota::ScoreGeometry` | 여러 페이지 → 이어붙인 캔버스, 박↔픽셀 양방향 변환 |
| `nota-analyze` | 페이지 분석 CLI |
| `nota::ScrollController` | 임계 감쇠 스프링 + 속도 피드포워드 + 수동 개입 양보 |
| `nota-geometry` | 다중 페이지 매핑 CLI |
| `nota-scroll` | 전체 파이프라인 시뮬레이션 CLI |

측정 결과는 `ALGORITHM.md` 8.0(V1/V2), 8.0.1(매핑 오차), 8.1(열화 내성),
8.2(품질 평가). 다섯 페이지 모두 V1/V2 F=1.000이고 매핑 드리프트는 0박이다.

### 매핑 오차 측정

```
python3 prototype/evaluate_geometry.py prototype/out/scores/bach_bwv846 2 \
    --beats-per-measure 4
```

### 전체 파이프라인 확인

오디오를 제외한 전 구간이 이어져 있다. 실제 악보로 돌려보고 눈으로 확인할 수 있다.

```
./build/tools/nota-scroll prototype/out/scores/bach_bwv846_p1.pgm \
    prototype/out/scores/bach_bwv846_p2.pgm \
    --tempo 72 --beats-per-measure 4 --viewport 900 --csv trace.csv

python3 prototype/render_scroll.py trace.csv \
    prototype/out/scores/bach_bwv846_p1.png \
    prototype/out/scores/bach_bwv846_p2.png -o filmstrip.png
```

기대값은 `ALGORITHM.md` 7.6절.

### 다음 — 오디오

남은 것은 오디오 파이프라인뿐이다. 자리는 이미 비워져 있다 — `nota-scroll`이
일정한 템포를 넣는 그 자리에 위치 추정기의 출력이 들어간다.

**실제 피아노 녹음이 있어야 진행할 수 있다.** 합성 오디오로는 판정할 수 없다는
것이 측정으로 드러났다(`ALGORITHM.md` 8.3, `docs/decisions/0002-ml-vs-dsp.md`).

필요한 것은 **악보 PDF 한 부와 그 악보를 연주한 녹음 하나**다. 실제로 연주하는
그대로면 된다 — 페달을 쓰든, 루바토가 있든, 방이 울리든. 그것이 제품이 마주할
조건이고, 거기서 동작하지 않으면 통제된 조건에서 동작해도 의미가 없다.

압축만 예외다. 실제 앱은 마이크에서 raw PCM을 받지 파일을 읽지 않으므로,
저비트레이트 파일로 시험하면 제품에 없는 열화를 재게 된다. WAV나 MP3
320 kbps면 좋고, 여의치 않으면 그대로 받되 결과를 읽을 때 감안한다.
상세는 `prototype/fixtures/README.md`.

라벨이 없어도 전체 드리프트(V6의 핵심), 템포 안정성, 깨지는 지점은 측정된다.
녹음의 시작과 끝이 악보의 시작과 끝이므로 기준점 두 개가 공짜로 생기기 때문이다.

녹음을 기다리는 동안 할 수 있는 것:

- librosa가 잔향에서 더 강건한 이유를 분석해 피크 선택기를 개선 (0.971 대 0.999)
- 검증 표본 확대 — 반복 구간, 변박, 성부 분리가 있는 작품
- 실제 스캔본 표본 확보 (현재는 전부 디지털 조판 + 합성 열화)