#!/usr/bin/env python3
"""온셋 검출기를 합성 피아노 오디오로 비교한다 (V3, V4 준비).

**이 하네스는 V4를 판정할 수 없다.** 그것이 이 실험의 주된 결론이다.

가산 합성은 음이 시작될 때 배음열을 통째로 더하므로 수학적으로 날카로운
스펙트럼 불연속을 만든다. 어택 트랜지언트를 0으로 지워도, 페달을 끝까지
밟아도, 성당 수준의 잔향(RT60 3초)을 걸어도 검출기가 F≈0.99를 유지한다.
실제 피아노를 어렵게 만드는 요소 — 현의 공명, 복잡한 어택, 마이크 응답,
기기 전처리 — 가 재현되지 않는다.

그래도 쓸모가 있다. 구현이 명세대로 동작하는지, 그리고 서로 다른 검출기의
상대적 강건성이 어떤지는 알려준다. 실제 녹음이 도착하면 같은 채점기를 그대로
쓸 수 있다 (정답만 MIDI 대신 사람이 라벨한 것으로 바꾸면 된다).

사용법:
    pip install librosa pretty_midi music21 scipy
    python3 prototype/evaluate_onsets.py
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from onset_detect import (SR, dedupe, f_measure, pick_peaks,   # noqa: E402
                          spectral_flux_odf)
from synth_piano import render                                  # noqa: E402


def reverb(y, rt60, wet, sr=SR, seed=1):
    from scipy.signal import fftconvolve
    if wet <= 0 or rt60 <= 0:
        return y
    n = int(rt60 * sr)
    rng = np.random.default_rng(seed)
    ir = rng.normal(0, 1, n) * np.exp(-np.arange(n) / sr * (6.9 / rt60))
    ir /= np.sqrt(np.sum(ir ** 2))
    wet_sig = fftconvolve(y, ir)[:len(y)]
    wet_sig /= (np.max(np.abs(wet_sig)) + 1e-9)
    out = (1 - wet) * y + wet * wet_sig
    return (out / (np.max(np.abs(out)) * 1.05)).astype(np.float32)


def main():
    try:
        import librosa
        import pretty_midi
        from music21 import corpus
    except ImportError as e:
        print("의존성 없음: %s" % e, file=sys.stderr)
        return 2

    out = Path("prototype/out/audio")
    out.mkdir(parents=True, exist_ok=True)
    mid = out / "bwv846.mid"
    if not mid.exists():
        corpus.parse("bach/bwv846.mxl").write("midi", fp=str(mid))
    pm = pretty_midi.PrettyMIDI(str(mid))

    base, onsets = render(pm, pedal=0.0)
    truth = dedupe(onsets)
    print("합성: Bach BWV846, %.0f초, 정답 온셋 %d개 (동시음 병합)\n"
          % (len(base) / SR, len(truth)))

    def both(y):
        a = f_measure(truth, pick_peaks(spectral_flux_odf(y)))[2]
        b = f_measure(truth,
                      librosa.onset.onset_detect(y=y, sr=SR, units="time",
                                                 backtrack=False))[2]
        return "%.3f / %.3f" % (a, b)

    print("페달 (각 칸: 스펙트럴플럭스 F / librosa F)")
    for ped in (0.0, 0.5, 1.0):
        y, _ = render(pm, pedal=ped)
        print("  페달 %.1f   %s" % (ped, both(y)))

    print("\n잔향 (페달 1.0)")
    y1, _ = render(pm, pedal=1.0)
    for label, rt, wet in [("없음", 0, 0), ("작은방 0.4s/30%", 0.4, 0.3),
                           ("거실 0.8s/45%", 0.8, 0.45),
                           ("홀 1.8s/60%", 1.8, 0.60),
                           ("성당 3.0s/75%", 3.0, 0.75)]:
        print("  %-16s %s" % (label, both(reverb(y1, rt, wet))))

    print("\n위 수치가 모두 1.0에 가깝다는 것이 이 하네스의 한계를 보여준다.")
    print("V3/V4 판정에는 실제 피아노 녹음이 필요하다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
