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

JSON의 `advance_ratio`는 시스템마다의 y 전진을 중앙값으로 나눈 값이다. 1에서
크게 벗어나는 시스템이 있으면 그 줄을 연주하는 동안 스크롤이 튄다. `--no-trim`을
주면 페이지 여백을 자르지 않아 이 값이 페이지 경계에서 4.49까지 올라가는 것을
볼 수 있다(`ALGORITHM.md` 8.3).

### 위치 추정 측정 (V6)

온셋 검출기 없이 측정한다. 온셋 수를 정답 위치의 잉크에서 합성하므로 "검출기가
완벽하다면 추정기의 논리가 맞는가"를 묻는다.

```
./build/tools/nota-simulate page-1.pgm page-2.pgm --beats 3 --total-beats 219 \
    --tempo 100 --estimate --tempo-error 0.03
```

`--no-resync`를 붙이면 적분만 한 결과가 나오므로 재동기화의 기여를 바로 비교할
수 있다. 기대값은 `ALGORITHM.md` 8.5절.

**균일한 악보(Bach BWV846)에서 보정 횟수가 많이 나오면 그것이 고장이다.** 그
악보에서는 밀도 매칭이 위치를 가릴 수 없으므로 기권해야 한다. 보정이 늘면
신뢰도 계산이 다시 느슨해진 것이다.

### 스크롤 제어 측정

오디오 없이 연주를 모사한다. 등속·재동기화 점프·정지·프레임 드롭을 끼워 넣고
추적 오차와 부드러움(최대 가속도, 역행 거리)을 본다.

```
./build/tools/nota-simulate page-1.pgm page-2.pgm --beats 4 --total-beats 136 \
    --tempo 90 --trace trace.csv
```

기대값은 `ALGORITHM.md` 8.4절. 등속에서 추적 오차 중앙값 0.32 px, 역행 0 px가
나와야 한다. **역행이 0이 아니면 그것부터 봐야 한다** — 스크롤이 되돌아오면
연주자는 자기가 보던 자리를 잃는다.

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
| `nota::ScrollController` | 임계 감쇠 스프링 + 속도 피드포워드 + 수동 개입 |
| `nota::PositionEstimator` | 적분 + 밀도 매칭 재동기화 + 급변 억제 + 환산 보정 |
| `nota-analyze` | 페이지 분석 CLI |
| `nota-geometry` | 여러 페이지 → 매핑 CLI (`--curve`로 박→y 곡선 덤프) |
| `nota-simulate` | 매핑 + 제어기 묶은 연주 모사 CLI |

측정 결과는 `ALGORITHM.md` 8.0(V1/V2), 8.1(열화 내성), 8.2(품질 평가),
8.3(매핑), 8.4(스크롤 제어), 8.5(위치 추정).

### 다음 — 녹음

코어에서 오디오 없이 만들 수 있는 것은 끝났다. 남은 것은 모두 입력이 필요하다.

| 절 | 모듈 | 필요한 것 |
|---|---|---|
| 2 | 온셋 검출 | 피아노 녹음 (특히 **페달 사용**) |
| 3 | 템포 추정 | 메트로놈 연주 녹음 |
| 4 | 정지 감지 | 연주 중단이 섞인 녹음 |

`prototype/fixtures/README.md`에 필요한 녹음 조건이 정리되어 있다. MIDI 건반으로
오디오와 MIDI를 동시 녹음하면 온셋 정답이 자동으로 따라온다.

**녹음이 오면 가장 먼저 다시 정할 값**: 위치 추정의 `onset_count_relative_
uncertainty`(현재 0.15). 지금은 "온셋 검출이 15% 정도 틀린다"는 가정이고, 이 값이
재동기화의 신뢰도 전체를 좌우한다(`ALGORITHM.md` 8.5). V4 목표인 페달 F-measure
0.70에서는 관측 온셋 수가 30% 틀릴 수 있다.

### 상보 필터에서 위험한 것은 틀린 추정이 아니라 틀린 신뢰도다

위치 추정을 만들며 두 번 폭주했고, 두 번 모두 신뢰도 계산이 원인이었다 — 추정이
틀린 것이 아니라 **틀린 추정을 믿은 것**이 원인이었다. 박 오차가 적분만 한 것보다
21배 나빠졌다(`ALGORITHM.md` 8.5).

틀린 추정은 β=0.15가 줄여 준다. 틀린 신뢰도는 그 β를 무력화한다. 재동기화를
손볼 때는 추정기보다 신뢰도를 먼저 의심할 것.

### 제어기 상수는 기기에서 정해야 한다

`ω = 2π·0.8 rad/s`는 설계값 그대로다. 측정으로는 교환 곡선까지만 나왔다 —
흡수 시간은 1/ω, 최대 가속도는 ω²에 비례하고, 정상 추적 오차는 ω와 무관하다
(`ALGORITHM.md` 8.4). 4875 px/s²의 잡아챔이 연주를 방해하는지는 사람이 기기에서
봐야 안다. 추측으로 바꾸지 말 것.

판단에 쓸 명령:

```
./build/tools/nota-simulate page-1.pgm page-2.pgm --beats 4 --total-beats 136 \
    --tempo 90 --resync-at 40 --resync-beats 8 --omega 3.14 --trace t.csv
```

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
