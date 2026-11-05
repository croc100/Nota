"""피아노 오디오를 합성한다. 온셋 정답은 MIDI에서 그대로 나온다.

한계가 크다 — evaluate_onsets.py 의 설명을 먼저 읽을 것. 실제 피아노를 어렵게
만드는 요소가 재현되지 않으므로 V4 판정에는 쓸 수 없다.

실제 녹음을 대체하지는 못한다. 다만 V4(페달 사용 시 온셋 검출)는 "이전 음들이
계속 울리는 상태에서 새 음의 에너지 상승이 묻히는가"의 문제이고, 그 조건은
합성으로 정확히 재현하고 **연속적으로 조절**할 수 있다. 실제 녹음으로는
페달 양을 눈금처럼 바꿔가며 측정할 수 없다.
"""
import numpy as np

SR = 44100


def piano_note(freq, dur, sr=SR, decay=2.5, n_harm=12, velocity=0.8):
    """배음 합성 + 지수 감쇠. 타현 악기의 거친 근사."""
    n = int(dur * sr)
    t = np.arange(n) / sr
    out = np.zeros(n)
    for h in range(1, n_harm + 1):
        f = freq * h
        if f > sr / 2:
            break
        # 높은 배음일수록 약하고 빨리 사라진다
        amp = velocity / (h ** 1.4)
        out += amp * np.sin(2 * np.pi * f * t) * np.exp(-t * decay * (1 + 0.25 * h))
    # 타현 순간의 짧은 잡음 성분 (망치 소리)
    attack = int(0.004 * sr)
    if attack > 0:
        rng = np.random.default_rng(int(freq))
        out[:attack] += 0.25 * velocity * rng.normal(0, 1, attack) * \
            np.linspace(1, 0, attack)
    # 어택 램프 (클릭 방지)
    ramp = max(1, int(0.002 * sr))
    out[:ramp] *= np.linspace(0, 1, ramp)
    return out


def render(midi, sr=SR, pedal=0.0, noise_db=-60.0, seed=0):
    """pedal 0 = 페달 없음(짧은 감쇠), 1 = 완전히 밟음(길게 울림)."""
    notes = []
    for inst in midi.instruments:
        if inst.is_drum:
            continue
        notes.extend(inst.notes)
    if not notes:
        return np.zeros(sr), []

    end = max(n.end for n in notes) + 3.0
    buf = np.zeros(int(end * sr))

    # 페달을 밟으면 건반을 떼도 소리가 이어지고 감쇠가 느려진다.
    base_decay = 6.0
    decay = base_decay * (1.0 - 0.88 * pedal)

    for n in notes:
        freq = 440.0 * 2 ** ((n.pitch - 69) / 12.0)
        held = n.end - n.start
        dur = held + (0.4 + 6.0 * pedal)       # 페달이 깊을수록 길게 남는다
        wave = piano_note(freq, dur, sr, decay=decay,
                          velocity=0.3 + 0.7 * (n.velocity / 127.0))
        i0 = int(n.start * sr)
        i1 = min(len(buf), i0 + len(wave))
        buf[i0:i1] += wave[:i1 - i0]

    rng = np.random.default_rng(seed)
    buf += rng.normal(0, 10 ** (noise_db / 20.0), len(buf))
    peak = np.max(np.abs(buf))
    if peak > 0:
        buf /= peak * 1.05
    onsets = sorted(n.start for n in notes)
    return buf.astype(np.float32), onsets
