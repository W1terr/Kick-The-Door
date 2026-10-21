"""Synthesizes the door impact sounds (res/Sound/FX/KickTheDoor/door_impact_0N.wav): a boot or a shout wave slamming into
a wooden door, built the way game impacts are layered, from nothing but noise and oscillators (no game audio in it):

  thump    the mass of the door taking the hit: a sine dropping from ~130 to ~55 Hz plus a low-passed noise boom
  body     the panel: wood resonances excited by noise plus a few short, rough modes (wood is damped, no ringing)
  crack    the hit itself and the planks' fibres: a hard click, then a few smaller cracks within ~25 ms
  knock    the door hitting its frame 70 - 110 ms later: a second, smaller hit
  rattle   hinges, lock and bolt: short metallic ticks bouncing 30 - 170 ms after the hit, quiet
  tail     dust and the frame settling: soft low noise, then a short room reflection

Three variations (different modes, timings, pitch) for the sound descriptor to pick from, like vanilla impacts.
Mono, 16 bit, 44.1 kHz, peak -0.3 dBFS: as loud as the game's own heavy impacts (they are normalized to 0 dBFS).
usage: python make_sounds.py   (needs numpy + scipy)"""
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "res" / "Sound" / "FX" / "KickTheDoor"
RATE = 44100
LENGTH = 1.1  # seconds
GAIN = {"thump": 0.6, "body": 1.15, "crack": 0.63, "splinter": 0.6, "rattle": 1.6, "tail": 0.75, "knock": 1.6, "f0": 170.0, "splinter_decay": 0.031}


def t_axis():
    return np.arange(int(LENGTH * RATE)) / RATE


def env(t, attack, decay, start=0.0):
    """fast attack, exponential decay, from start (s)"""
    x = t - start
    a = np.clip(x / max(attack, 1e-5), 0.0, 1.0)
    return np.where(x >= 0, a * np.exp(-np.maximum(x, 0) / decay), 0.0)


def bandpass(x, low, high, order=2):
    b, a = signal.butter(order, [low / (RATE / 2), high / (RATE / 2)], "band")
    return signal.lfilter(b, a, x)


def lowpass(x, cut, order=2):
    b, a = signal.butter(order, cut / (RATE / 2), "low")
    return signal.lfilter(b, a, x)


def highpass(x, cut, order=2):
    b, a = signal.butter(order, cut / (RATE / 2), "high")
    return signal.lfilter(b, a, x)


def rough(t, rng, rate=40.0, depth=0.5):
    """slow random wobble (0..): makes a resonance sound like a real material, not a tuned tone"""
    wobble = lowpass(rng.normal(size=t.size), rate, 2)
    wobble /= np.abs(wobble).max() + 1e-9
    return 1.0 + depth * wobble


def thump(t, rng, at=0.0, size=1.0):
    """the door's mass taking the hit: a short pitch-dropping sine and a low boom of noise"""
    base = 58.0 * rng.uniform(0.92, 1.08)
    freq = base + 80.0 * np.exp(-np.maximum(t - at, 0) / 0.02)
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    tone = np.sin(phase) * env(t, 0.0015, 0.075 * size, at)
    boom = lowpass(rng.normal(size=t.size), 200, 4) * env(t, 0.001, 0.05 * size, at) * 6.0
    return tone + boom * 0.7


def body(t, rng, at=0.0, size=1.0):
    """the panel: wood resonances excited by noise (formants, not tones) and a few short, rough modes"""
    out = np.zeros_like(t)
    f0 = GAIN["f0"] * rng.uniform(0.92, 1.08)
    noise = rng.normal(size=t.size)
    for f, q, decay in ((f0 * 1.6, 2.5, 0.07), (f0 * 3.6, 3.0, 0.05), (f0 * 7.2, 3.5, 0.035), (f0 * 12.5, 4.0, 0.022)):
        f *= rng.uniform(0.93, 1.07)
        band = bandpass(noise, f / (1 + 0.5 / q), f * (1 + 0.5 / q), 2)
        out += band * env(t, 0.001, decay * size * rng.uniform(0.8, 1.2), at) * 3.0
    for k, r in enumerate((1.0, 1.52, 2.21, 3.04, 3.93, 4.81)):
        f = f0 * r * rng.uniform(0.95, 1.05)
        decay = 0.09 * (100.0 / f) ** 0.5 * size * rng.uniform(0.8, 1.2)
        out += 0.5 / (1 + k) ** 0.4 * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.3)) * env(t, 0.0015, decay, at) * rough(t, rng)
    return out * 0.5


def crack(t, rng, at=0.0, size=1.0):
    """the hit and the fibres: one hard click, then smaller cracks"""
    noise = rng.normal(size=t.size)
    hard = highpass(bandpass(noise, 900, 7000, 2), 600) * env(t, 0.0003, 0.007, at) * 2.2 * size
    small = np.zeros_like(t)
    for _ in range(rng.integers(4, 8)):
        start = at + rng.uniform(0.004, 0.028)
        small += bandpass(rng.normal(size=t.size), rng.uniform(1200, 2500), rng.uniform(4000, 7500)) * \
            env(t, 0.0002, rng.uniform(0.002, 0.005), start) * rng.uniform(0.4, 1.1) * size
    return hard + small


def splinter(t, rng):
    """wood fibres giving: bright crunching noise for a few tens of milliseconds"""
    grain = bandpass(rng.normal(size=t.size), 1400, 6500, 2)
    flutter = 0.6 + 0.4 * np.abs(np.sin(2 * np.pi * rng.uniform(55, 90) * t))  # uneven, not a hiss
    return grain * flutter * env(t, 0.0008, GAIN["splinter_decay"], 0.002)


def knock(t, rng):
    """the door hitting its frame / stop right after: a second, smaller hit"""
    at = rng.uniform(0.07, 0.11)
    size = rng.uniform(0.35, 0.5)
    return size * (0.8 * thump(t, rng, at, 0.8) + body(t, rng, at, 0.7) + 0.6 * crack(t, rng, at, 0.7))


def rattle(t, rng):
    """hinges and lock: short metallic ticks bouncing after the hit"""
    out = np.zeros_like(t)
    modes = np.array([1820.0, 2950.0, 4310.0, 6080.0]) * rng.uniform(0.9, 1.1)
    hits = np.sort(rng.uniform(0.03, 0.17, rng.integers(3, 6)))
    for i, at in enumerate(hits):
        level = 0.85 ** i * rng.uniform(0.5, 1.0)
        for k, f in enumerate(modes):
            out += level * (0.6 ** k) * np.sin(2 * np.pi * f * (t - at)) * env(t, 0.0004, rng.uniform(0.008, 0.016), at)
    return out * 0.12


def tail(t, rng):
    """dust and the frame settling"""
    dust = lowpass(rng.normal(size=t.size), 700, 2) * env(t, 0.03, 0.3, 0.02)
    return dust * 0.10


def room(x, rng, seconds=0.32, wet=0.12):
    """a short dark reflection (the game's output model adds the real reverb)"""
    n = int(seconds * RATE)
    impulse = rng.normal(size=n) * np.exp(-np.arange(n) / (n / 5.0))
    impulse = lowpass(impulse, 2500)
    impulse[: int(0.008 * RATE)] = 0.0  # pre-delay
    impulse /= np.sqrt(np.sum(impulse ** 2))
    return x + wet * signal.fftconvolve(x, impulse)[: x.size]


def make(seed):
    rng = np.random.default_rng(seed)
    t = t_axis()
    mix = (GAIN["thump"] * thump(t, rng) + GAIN["body"] * body(t, rng) + GAIN["crack"] * crack(t, rng)
           + GAIN["splinter"] * splinter(t, rng) + GAIN["knock"] * knock(t, rng) + GAIN["rattle"] * rattle(t, rng)
           + GAIN["tail"] * tail(t, rng))
    mix = room(mix, rng)
    mix = highpass(mix, 30, 2)  # nothing below what a speaker plays
    fade = int(0.15 * RATE)
    mix[-fade:] *= np.linspace(1.0, 0.0, fade) ** 2
    # mastered like the game's heavy impacts: the loud part brought to ~-9.5 dB RMS, the spikes above soft-limited
    level = np.sqrt(np.convolve(mix * mix, np.ones(441) / 441, "same"))
    mix *= 10 ** (-9.5 / 20) / np.sqrt(np.mean(mix[level > level.max() * 0.1] ** 2))
    mix = np.tanh(mix * 1.15) / 1.15
    mix *= 10 ** (-0.3 / 20) / np.abs(mix).max()
    return mix


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for i, seed in enumerate((11, 23, 37), 1):
        data = make(seed)
        path = OUT / f"door_impact_{i:02d}.wav"
        wavfile.write(path, RATE, (data * 32767).astype(np.int16))
        print(f"wrote {path.name}: {data.size / RATE:.2f} s")


if __name__ == "__main__":
    main()
