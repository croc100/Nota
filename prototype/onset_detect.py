"""온셋 검출기를 비교한다. 정답은 MIDI에서 나온다 (V3, V4).

ALGORITHM.md 2절에 설계한 스펙트럴 플럭스를 명세 그대로 구현해 측정한다.
설계가 종이 위에서만 옳은지, 실제로 동작하는지 확인하는 것이 목적이다.
"""
import numpy as np

SR = 44100
WIN, HOP = 2048, 512


def stft_mag(y, win=WIN, hop=HOP):
    w = np.hanning(win)
    n = 1 + (len(y) - win) // hop
    out = np.empty((n, win // 2 + 1), dtype=np.float32)
    for i in range(n):
        seg = y[i * hop:i * hop + win] * w
        out[i] = np.abs(np.fft.rfft(seg))
    return out


def spectral_flux_odf(y, sr=SR, gamma=1000.0, lo=50.0, hi=5000.0, whiten=False):
    """ALGORITHM.md 2.2절. 로그 압축 후 양의 변화만 합산."""
    mag = stft_mag(y)
    freqs = np.fft.rfftfreq(WIN, 1.0 / sr)
    band = (freqs >= lo) & (freqs <= hi)
    mag = mag[:, band]
    if whiten:
        # 각 빈을 최근 최대값으로 정규화 (페달 울림 억제 목적)
        run_max = np.maximum.accumulate(mag, axis=0)
        decay = 0.999 ** np.arange(len(mag))[:, None]
        run_max = np.maximum(run_max * decay, 1e-6)
        mag = mag / run_max
    Y = np.log1p(gamma * mag)
    d = np.diff(Y, axis=0)
    return np.concatenate([[0.0], np.maximum(d, 0).sum(axis=1)])


def pick_peaks(odf, sr=SR, hop=HOP, W=8, alpha=1.6, delta=0.0, min_gap=0.05):
    """ALGORITHM.md 2.3절. 중위수 적응 임계 + 최소 간격."""
    n = len(odf)
    times, last = [], -1e9
    pad = np.pad(odf, W, mode="edge")
    for i in range(n):
        local = pad[i:i + 2 * W + 1]
        thr = np.median(local) * alpha + delta
        if odf[i] < thr:
            continue
        if i > 0 and odf[i] < odf[i - 1]:
            continue
        if i + 1 < n and odf[i] < odf[i + 1]:
            continue
        t = i * hop / sr
        if t - last < min_gap:
            continue
        times.append(t)
        last = t
    return np.array(times)


def dedupe(onsets, gap=0.03):
    """동시에 눌린 화음은 하나의 온셋으로 본다."""
    out = []
    for t in sorted(onsets):
        if not out or t - out[-1] >= gap:
            out.append(t)
    return np.array(out)


def f_measure(truth, detected, tol=0.05):
    used = np.zeros(len(detected), dtype=bool)
    tp = 0
    for t in truth:
        best, bd = -1, tol + 1e-9
        for i, d in enumerate(detected):
            if used[i]:
                continue
            if abs(d - t) < bd:
                best, bd = i, abs(d - t)
        if best >= 0:
            used[best] = True
            tp += 1
    fp = len(detected) - tp
    fn = len(truth) - tp
    p = tp / (tp + fp) if tp + fp else 0.0
    r = tp / (tp + fn) if tp + fn else 0.0
    f = 2 * p * r / (p + r) if p + r else 0.0
    return p, r, f
