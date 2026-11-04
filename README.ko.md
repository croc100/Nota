# Nota

*[English README](README.md)*

피아노 연주를 듣고 악보를 자동으로 스크롤해주는 악보 앱. iOS와 Android.

## 무엇인가

일반적인 악보 앱의 기능(라이브러리, PDF 임포트, 필기 주석, 세트리스트, 메트로놈)을 모두 갖추고, 여기에 Nota만의 기능을 더한다:

**마이크로 피아노 연주를 실시간으로 들으면서, 연주 속도에 맞춰 악보를 자동으로 스크롤한다.**

페이지 단위로 넘기는 방식이 아니다. PDF 페이지들을 세로로 이어붙여 하나의 연속된 악보로 만들고, 연주 진행에 맞춰 부드럽게 아래로 흘려보낸다. 연주자는 건반에서 손을 떼지 않는다.

- 피아노 전용 (단일 악기 가정이 음향 분석을 크게 단순화한다)
- 마이크 오디오는 전부 기기 안에서 처리된다. 서버로 전송하지 않는다.

## 왜 연속 스크롤인가

페이지 넘김은 이산적 사건이므로 타이밍 오차가 그대로 실패가 된다. 한 박 늦으면 연주자는 이미 지나간 마디를 보고 있다. 연속 스크롤은 같은 오차가 "악보가 조금 위/아래로 어긋남"으로 나타나고, 악보는 여전히 화면에 있으므로 연주자가 복구할 수 있다. 실패 모드가 부드럽다.

또한 **어떤 음표가 연주되는지 정확히 알 필요가 없다.** "지금 어느 정도 속도로 진행 중인가"만 알면 스크롤 속도가 나온다. 이것이 OMR(광학 악보 인식)을 필수 경로에서 빼낸다.

## 구조

핵심 알고리즘은 **플랫폼 중립 C++ 코어 하나**에 모아 두고, UI는 각 플랫폼 네이티브로 작성한다.

```
┌─────────────────┐   ┌─────────────────┐
│ iOS             │   │ Android         │
│ Swift + SwiftUI │   │ Kotlin + Compose│
└────────┬────────┘   └────────┬────────┘
         └───────────┬──────────┘
                     ▼
         ┌───────────────────────────┐
         │  NotaCore  (C++17)        │
         │  오디오 분석 · 템포 추정    │
         │  악보 기하 · 위치 추정      │
         │  스크롤 제어 · PDF 래스터   │
         └───────────────────────────┘
```

코어가 플랫폼 중립이므로 **Xcode나 Android Studio 없이 Linux에서 빌드·테스트된다.** 가장 위험한 알고리즘을 매 커밋 검증할 수 있고, "코어에 플랫폼 API 금지" 규칙은 Linux 빌드가 통과하는 것으로 강제된다.

| 레이어 | 공유 | 기술 |
|---|---|---|
| 오디오 분석 · 템포 추정 | ✅ | C++17 + SIMD |
| 악보 기하 분석 (오선·마디선) | ✅ | C++17 |
| 위치 추정 · 스크롤 제어 | ✅ | C++17 |
| PDF 래스터화 | ✅ | PDFium (C++) |
| 앱 UI · 네비게이션 | ❌ | SwiftUI / Jetpack Compose |
| 오디오 캡처 | ❌ | AVAudioEngine / Oboe |
| 필기 주석 | ❌ | PencilKit / androidx.ink |

UI를 두 번 작성하는 것은 의도된 선택이다. 거대 캔버스의 끊김 없는 스크롤과 저지연 스타일러스 입력은 크로스 플랫폼 UI 프레임워크가 가장 약한 지점이고, 플랫폼 기성품(특히 PencilKit)이 압도적으로 낫다.

## 상태

**오디오 녹음 없이 만들 수 있는 코어는 끝났다.** 앱 UI는 아직 없다.

| 모듈 | 내용 |
|---|---|
| `nota::RingBuffer` | lock-free SPSC. peek/consume 분리로 STFT 윈도우 겹침 지원 |
| `nota::BinarizeSauvola` | 적분 영상 기반 적응 임계 |
| `nota::EstimateSkewDegrees` / `RotateGray` | 기울기 추정·보정 |
| `nota::DetectStaves` | 오선 검출 |
| `nota::AnalyzePageGray` | 전처리~마디 상자까지 전체 경로 + 자체 품질 평가 |
| `nota::ScoreGeometry` | 시간↔픽셀 매핑. 시스템 단위 보간, 양방향 변환 |
| `nota::ScrollController` | 임계 감쇠 스프링 + 속도 피드포워드 + 수동 개입 |
| `nota::PositionEstimator` | 적분 + 밀도 매칭 재동기화 + 급변 억제 |
| `nota-analyze` | 페이지 분석 CLI (검출 결과 오버레이) |
| `nota-geometry` | 여러 페이지 → 매핑 CLI |
| `nota-simulate` | 매핑+제어기+추정기 묶은 연주 모사 CLI |

단위 테스트 87개, Release와 ASan/UBSan 양쪽에서 통과.

### 녹음이 와야 가능한 것

온셋 검출, 템포 추정, 정지 감지는 모두 실제 피아노 오디오가 필요하다. 필요한 녹음 조건은 [prototype/fixtures/README.md](prototype/fixtures/README.md)에 정리해 두었다. 특히 **페달을 밟은 녹음이 결정적이다** — 온셋 검출이 가장 깨지는 조건이고, 여기서 제품 성립 여부가 갈린다. MIDI 건반으로 오디오와 MIDI를 동시 녹음하면 온셋 정답이 자동으로 따라온다.

## 추측이 아니라 측정으로 정한다

이 저장소의 임계값은 대부분 실측에서 나왔고, 근거가 상수 옆 주석에 있다. 정답은 실제 피아노 작품을 Verovio로 조판해 얻는다 — SVG에서 오선·마디선 요소를 직접 읽으므로 수작업 라벨링이 없다.

| 항목 | 결과 |
|---|---|
| 오선·시스템 검출 (V1) | 두 악보에서 100% |
| 마디 경계 (V2) | F = 1.000 (Bach BWV846), 0.923 (Chopin Op.6-2) |
| 스캔 열화 내성 | light / medium / heavy 프리셋 전부에서 구조 검출 100% |
| 시간↔픽셀 매핑 | ±1 시스템 이내 **1.000** |
| 스크롤 추적 (등속) | 오차 중앙값 **0.32 px**, 역행 0 |
| 위치 드리프트 (V6), 템포 오차 8% | Chopin **1.19 → 0.85 시스템** (실패 → 통과) |

**실패한 것까지** 포함한 상세는 [docs/ALGORITHM.md](docs/ALGORITHM.md) 8절에 있다.

### 측정으로 뒤집힌 설계 가정 3개

세 번 다 조용히 수정하지 않고 번복 기록으로 남겼다.

1. **"마디선 과검출은 단조성을 깨지 않으니 치명적이지 않다."** 단조성은 실제로 유지된다. 그러나 **축척이 깨진다.** Chopin의 73마디를 89개로 검출하면 박 축이 1.22배 늘어나고, 곡 끝에서 매핑이 2시스템 어긋난다. 축척과 배분을 분리해 고쳤다 — 시스템 적중률 0.260 → 0.890.

2. **"ζ = 1이면 오버슈트가 없다."** *위치* 계단 입력에만 참이다. 속도가 실린 상태에서 연주가 멈추면 `v/(2ζω)`만큼 지나쳤다 되돌아온다. ζ를 올리면 역행이 40% 줄지만 최대 가속도가 1.9배가 되므로 ζ = 1을 유지했다 — 이제는 숫자를 근거로.

3. **6.2절과 6.3절이 서로 모순이다.** 급변 억제는 "한 시스템 이상 떨어진 보정"에 발동하는데 탐색 범위가 ±2마디, 즉 임계의 절반이다. 등급 B에서 발동할 수 없다. 등급 A를 위해 남기고, 두 사실을 모두 테스트로 고정했다.

가장 쓸모 있었던 교훈도 문서에 적어 두었다. **상보 필터에서 위험한 것은 틀린 추정이 아니라 틀린 신뢰도다.** 위치 추정에서 두 번 폭주했고(최악 적분만 한 것보다 21배 악화), 두 번 다 추정기가 아니라 신뢰도 계산이 원인이었다.

## 빌드와 검증

외부 C++ 의존성이 없다. 테스트 하네스도 자체 구현이라 네트워크 없이 빌드된다.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/core/tests/nota_core_tests
```

Sanitizer:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNOTA_SANITIZE=ON
cmake --build build-asan && ./build-asan/core/tests/nota_core_tests
```

실제 페이지 분석 (PDF → PGM 먼저):

```bash
pdftoppm -gray -r 200 -f 1 -l 1 score.pdf page
./build/tools/nota-analyze page-1.pgm --overlay overlay.ppm
```

연주를 끝에서 끝까지 모사:

```bash
./build/tools/nota-simulate page-1.pgm page-2.pgm --beats 4 --total-beats 136 \
    --tempo 100 --estimate --tempo-error 0.03 --trace trace.csv
```

경고를 엄격히 켜 두었고(`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`) 0건을 유지한다.

검증용 악보 생성과 채점에는 Python이 필요하다:

```bash
pip3 install verovio music21 cairosvg numpy pillow
python3 prototype/make_fixtures.py --out prototype/out/scores
```

하네스 전체는 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)에 있다.

## 지원 버전

| 플랫폼 | 최소 | 우선순위 |
|---|---|---|
| iOS / iPadOS | 17.0 | 1차 |
| Android | API 29 (Android 10) | 2차 |

iOS를 먼저 완성한다. 악보 앱의 주 사용 기기가 iPad이고, 하드웨어가 균일해서 알고리즘 검증 단계의 변수를 줄일 수 있기 때문이다. 다만 코어는 첫 커밋부터 플랫폼 중립으로 작성했으므로 "나중에 이식"이 아니다.

## 문서

| 문서 | 내용 |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | 아키텍처, 플랫폼 전략, 기술 결정, 리스크, 로드맵 |
| [docs/ALGORITHM.md](docs/ALGORITHM.md) | 신호 처리 · CV · 제어 알고리즘 상세, 검증 항목과 측정 결과 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 개발 환경, 검증 하네스, 현재 상태, 남은 과제 |
| [docs/decisions/](docs/decisions/) | 주요 기술 결정 기록 (ADR) |

## 라이선스

미정.
