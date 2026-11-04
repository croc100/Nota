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

### 시간↔픽셀 매핑 채점

박 위치를 정답으로 주고 "매핑이 올바른 시스템을 가리키는가"를 센다. 오디오
없이 매핑만 떼어 측정하므로 V6의 전제 조건을 먼저 확인할 수 있다.

```
python3 prototype/evaluate_geometry.py --beats 3 --true-length \
    prototype/out/scores/chopin_mazurka06_2_p1.svg:prototype/out/scores/chopin_mazurka06_2_p1.pgm \
    prototype/out/scores/chopin_mazurka06_2_p2.svg:prototype/out/scores/chopin_mazurka06_2_p2.pgm
```

기대값은 `ALGORITHM.md` 8.3절의 표와 같다. `--true-length`를 빼면 곡 길이를
검출된 마디 수에서 유도하며, 과검출이 축척 오차로 번지는 것을 그대로 볼 수
있다(Chopin 적중률 0.890 → 0.260).

매핑 자체를 눈으로 보려면 곡선을 덤프한다. 계단(멈춤→점프)이 아니라 꺾인
직선들이 이어진 모양이어야 하고, 꺾이는 지점이 시스템 경계와 맞아야 한다.

```
./build/tools/nota-geometry page-1.pgm page-2.pgm --beats 4 --curve curve.csv
```

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
| `nota::ScoreGeometry` | 시간↔픽셀 매핑. 시스템 단위 보간, 양방향 변환 |
| `nota-analyze` | 페이지 분석 CLI |
| `nota-geometry` | 여러 페이지 → 매핑 CLI (`--curve`로 박→y 곡선 덤프) |

측정 결과는 `ALGORITHM.md` 8.0(V1/V2), 8.1(열화 내성), 8.2(품질 평가),
8.3(매핑).

### 다음 — 스크롤 제어기

`ALGORITHM.md` 7절. 입력이 숫자뿐이므로 오디오도 이미지도 없이 단위 테스트가
된다. 매핑이 끝났으니 `PixelsPerBeat`(7.3절 피드포워드)와 `BeatAt`(7.5절 수동
개입)을 바로 쓸 수 있다.

구현할 것:

1. `core/include/nota/scroll_control.hpp` / `src/scroll_control.cpp` —
   임계 감쇠 스프링(7.2), 속도 피드포워드(7.3), 속도 제한(7.4), 수동 개입(7.5)
2. 테스트 — 정상상태 오차가 0에 수렴하는지, 오버슈트가 없는지, 템포 점프에서
   속도 제한이 걸리는지, 손 스크롤 후 위치가 인수되는지
3. 매핑과 묶은 시뮬레이션 — 정답 박 곡선을 주고 스크롤 y 궤적을 CSV로 덤프.
   `prototype/evaluate_geometry.py`가 매핑을 채점한 것과 같은 방식이다.

### 알려진 과제 — 짧은 시스템을 놓친다

Bach 2페이지 마지막 시스템은 마디가 하나뿐이라 페이지 폭의 14%만 차지하고, 행
투영 기반 오선 검출이 이를 찾지 못한다(`ALGORITHM.md` 8.3 말미). 곡 끝에서 한
시스템이 빈다.

행 투영 임계를 낮추면 8.1절의 열화 내성이 함께 내려갈 수 있다. 추측으로 숫자를
만지지 말고, 임계를 쓸어보며 두 지표를 같이 측정해서 정할 것.

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
