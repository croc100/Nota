# Nota

*[한국어 README](README.ko.md)*

A sheet-music app that listens to your piano playing and scrolls the score for you. iOS and Android.

## What it is

Everything a normal sheet-music app does — library, PDF import, stylus annotation, setlists, metronome — plus the one thing that makes it Nota:

**It listens to the piano through the microphone and scrolls the score at the speed you are actually playing.**

Not page flipping. The PDF pages are stitched into a single vertical canvas that flows upward as you play, so your hands never leave the keys.

- Piano only. Assuming one instrument simplifies the acoustic analysis enormously.
- Microphone audio is processed entirely on the device. Nothing is sent to a server.

## Why continuous scrolling

Page flipping is a discrete event, so a timing error becomes an outright failure: one beat late and the player is looking at a measure that is already gone. Continuous scrolling turns the same error into "the score sits slightly too high or too low" — it is still on screen, and the player recovers. The failure mode is gentle.

It also means we never need to know *which notes* are being played. Knowing roughly how fast the music is moving is enough to set a scroll speed. That takes OMR (optical music recognition) off the critical path.

## Architecture

The algorithms live in **one platform-neutral C++ core**; the UI is native on each platform.

```
┌─────────────────┐   ┌─────────────────┐
│ iOS             │   │ Android         │
│ Swift + SwiftUI │   │ Kotlin + Compose│
└────────┬────────┘   └────────┬────────┘
         └───────────┬──────────┘
                     ▼
         ┌────────────────────────────┐
         │  NotaCore  (C++17)         │
         │  audio analysis · tempo    │
         │  score geometry · position │
         │  scroll control · PDF      │
         └────────────────────────────┘
```

Because the core is platform-neutral, it **builds and tests on Linux with no Xcode and no Android Studio**. The riskiest code is verified on every commit, and the rule is enforced by the Linux build passing: no Apple/Android/OS headers in `core/`.

| Layer | Shared | Technology |
|---|---|---|
| Audio analysis · tempo estimation | ✅ | C++17 + SIMD |
| Score geometry (staves, barlines) | ✅ | C++17 |
| Position estimation · scroll control | ✅ | C++17 |
| PDF rasterization | ✅ | PDFium (C++) |
| App UI · navigation | ❌ | SwiftUI / Jetpack Compose |
| Audio capture | ❌ | AVAudioEngine / Oboe |
| Stylus annotation | ❌ | PencilKit / androidx.ink |

Writing the UI twice is deliberate. Jank-free scrolling of a huge canvas and low-latency stylus input are exactly where cross-platform UI frameworks are weakest, and the platform-native components (PencilKit above all) are decisively better.

## Status

**The parts of the core that can be built without audio recordings are done.** There is no app UI yet.

| Module | What it does |
|---|---|
| `nota::RingBuffer` | Lock-free SPSC queue; split peek/consume for overlapping STFT windows |
| `nota::BinarizeSauvola` | Adaptive thresholding over an integral image |
| `nota::EstimateSkewDegrees` / `RotateGray` | Skew estimation and correction |
| `nota::DetectStaves` | Staff-line detection |
| `nota::AnalyzePageGray` | Full page pipeline through measure boxes, with self-assessed quality |
| `nota::ScoreGeometry` | Time ↔ pixel mapping; per-system interpolation, bidirectional |
| `nota::ScrollController` | Critically damped spring + velocity feedforward + manual override |
| `nota::PositionEstimator` | Integration + density-matching resync + jump suppression |
| `nota-analyze` | Page analysis CLI (with detection overlay) |
| `nota-geometry` | Multi-page → mapping CLI |
| `nota-simulate` | End-to-end performance simulation CLI |

87 unit tests, green under both Release and ASan/UBSan.

### Blocked on recordings

Onset detection, tempo estimation and silence detection all need real piano audio. The conditions we need are written up in [prototype/fixtures/README.md](prototype/fixtures/README.md); **pedalled playing is the decisive case**, because that is where onset detection breaks down and where the product's viability is decided. Recording audio and MIDI together from a MIDI keyboard gives onset ground truth for free.

## Measured, not assumed

Thresholds in this repository come from measurement, and the reasoning sits in the comment next to each constant. Ground truth comes from real piano works typeset with Verovio, so there is no hand labelling — the staff and barline elements are read straight out of the SVG.

| What | Result |
|---|---|
| Staff / system detection (V1) | 100% on both scores |
| Measure boundaries (V2) | F = 1.000 (Bach BWV846), 0.923 (Chopin Op.6-2) |
| Scan degradation | Structure detection 100% across light / medium / heavy presets |
| Time ↔ pixel mapping | Within ±1 system: **1.000** |
| Scroll tracking, steady tempo | Median error **0.32 px**, zero reversal |
| Position drift (V6), 8% tempo error | Chopin **1.19 → 0.85 systems** (fail → pass) |

Details — including what *failed* — are in [docs/ALGORITHM.md](docs/ALGORITHM.md) §8.

### Three design assumptions that measurement overturned

Each is recorded as a reversal in the docs rather than quietly edited away.

1. **"Barline over-detection isn't fatal, because monotonicity survives."** Monotonicity does survive — but the *scale* breaks. Detecting Chopin's 73 measures as 89 stretches the beat axis by 1.22×, and by the end of the piece the mapping is two systems off. Fixed by separating scale from distribution: system-hit rate 0.260 → 0.890.

2. **"ζ = 1 means no overshoot."** True for a step in position only. With velocity already loaded, the scroll overshoots and returns by `v/(2ζω)` when playing stops. Raising ζ trades 40% less reversal for 1.9× the peak acceleration, so ζ = 1 stays — now with numbers behind it.

3. **Sections 6.2 and 6.3 contradict each other.** Jump suppression triggers at one system of disagreement, but the search range is ±2 measures — half the threshold. The rule cannot fire at grade B. Kept for grade A, and both facts are pinned by tests.

The most useful lesson, also written into the docs: **in a complementary filter the danger is not a wrong estimate but a wrong confidence.** Position estimation ran away twice (21× worse than integration alone at its worst), and both times the estimator was fine — the confidence calculation was not.

## Build and verify

No external C++ dependencies. The test harness is self-contained, so it builds without network access.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/core/tests/nota_core_tests
```

With sanitizers:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNOTA_SANITIZE=ON
cmake --build build-asan && ./build-asan/core/tests/nota_core_tests
```

Analyze a real page (PDF → PGM first):

```bash
pdftoppm -gray -r 200 -f 1 -l 1 score.pdf page
./build/tools/nota-analyze page-1.pgm --overlay overlay.ppm
```

Simulate a performance end to end:

```bash
./build/tools/nota-simulate page-1.pgm page-2.pgm --beats 4 --total-beats 136 \
    --tempo 100 --estimate --tempo-error 0.03 --trace trace.csv
```

Warnings are strict (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`) and kept at zero.

Generating the verification scores and scoring the detectors needs Python:

```bash
pip3 install verovio music21 cairosvg numpy pillow
python3 prototype/make_fixtures.py --out prototype/out/scores
```

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for the full harness.

## Platform support

| Platform | Minimum | Priority |
|---|---|---|
| iOS / iPadOS | 17.0 | First |
| Android | API 29 (Android 10) | Second |

iOS ships first: the iPad dominates this category, and uniform hardware removes variables while the algorithms are still being validated. The core has been platform-neutral since the first commit, so this is not a "port it later" plan.

## Documentation

| Document | Contents |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Architecture, platform strategy, technical decisions, risks, roadmap |
| [docs/ALGORITHM.md](docs/ALGORITHM.md) | Signal processing, CV and control algorithms; verification items and results |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Environment, verification harness, current state, open tasks |
| [docs/decisions/](docs/decisions/) | Architecture decision records |

## License

Undecided.
