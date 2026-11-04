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
| `nota-analyze` | 페이지 분석 CLI |

측정 결과는 `ALGORITHM.md` 8.0(V1/V2), 8.1(열화 내성), 8.2(품질 평가).

### 다음 — 시간↔픽셀 매핑

`core/include/nota/score_geometry.hpp`에 **설계와 API만** 들어 있고 구현이
없다. 헤더 주석에 핵심 결정의 근거가 적혀 있으니 먼저 읽을 것.

요점: 시스템 안에서 음악은 가로로 진행하지만 스크롤은 세로로 움직인다. 연주
위치를 그대로 y로 옮기면 "멈춤 → 점프"가 반복된다. 한 시스템을 연주하는 동안
y가 시스템 하나 높이만큼 연속으로 흐르게 보간한다.

구현할 것:

1. `core/src/score_geometry.cpp` — `Build`, `CanvasY`, `BeatAt`,
   `PixelsPerBeat`, `SystemAt`
2. `core/CMakeLists.txt`에 소스 추가
3. `core/tests/test_score_geometry.cpp` — 단조성, `BeatAt(CanvasY(b)) == b`
   왕복, 페이지 이어붙이기 오프셋, 범위 밖 고정, 두 가지 시간 배분 방식
4. 여러 페이지를 받아 매핑을 내보내는 CLI (`nota-geometry`)

그 다음은 스크롤 제어기(`ALGORITHM.md` 7절). 입력이 숫자뿐이라 오디오 없이
단위 테스트가 된다.

### 녹음이 와야 가능한 것

V3~V7(온셋·템포·드리프트). 필요한 녹음 조건은
`prototype/fixtures/README.md`에 정리해 두었다. 특히 **페달을 밟은 녹음**이
결정적이다 — 온셋 검출이 가장 깨지는 조건이고, 여기서 제품 성립 여부가 갈린다.
MIDI 건반으로 오디오와 MIDI를 동시 녹음하면 온셋 정답이 자동으로 따라온다.

## 5. 지켜야 할 규칙

- **코어에 플랫폼 API 금지.** Apple/Android/OS 헤더가 섞이면 Linux 빌드가
  깨지고 CI가 잡는다. 이것이 규칙을 강제하는 장치다 (`DESIGN.md` 3.2).
- **임계값은 측정해서 정한다.** 이 저장소의 상수는 대부분 실측에서 나왔고
  근거가 주석에 있다. 추측으로 숫자를 만지지 말 것.
- **실제 악보로 확인한다.** 합성 악보에서 통과한 판별식이 실제 악보에서 두 번
  틀렸다 (`ALGORITHM.md` 5.3, 5.4).
