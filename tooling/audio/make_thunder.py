"""Writes the thunder the game plays after lightning: assets/audio/sfx/
thunder_close.wav (a crack, then a rolling rumble) and thunder_far.wav (a
low rumble only). Drawn by this script from seeded noise, so the same every
run: no recorded or licensed audio. Standard library only.

    py -3 make_thunder.py
"""

import math
import os
import random
import struct
import wave

RATE = 22050
ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'assets', 'audio', 'sfx'))


def low_pass(samples, cutoff_hz):
    """A one-pole low-pass at `cutoff_hz`."""
    a = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / RATE)
    out = []
    y = 0.0
    for x in samples:
        y += a * (x - y)
        out.append(y)
    return out


def rumble(rng, seconds, cutoff_hz):
    """Brown-ish noise: white noise low-passed twice."""
    white = [rng.uniform(-1.0, 1.0) for _ in range(int(seconds * RATE))]
    return low_pass(low_pass(white, cutoff_hz), cutoff_hz * 1.5)


def swells(rng, seconds, count, onset):
    """An envelope: a rise over `onset` seconds, then `count` rolls dying away."""
    rolls = sorted(rng.uniform(0.1, 0.75) * seconds for _ in range(count))
    env = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        rise = min(1.0, t / onset) if onset > 0 else 1.0
        decay = math.exp(-2.2 * t / seconds)
        roll = sum(0.6 * math.exp(-((t - r) / (0.12 * seconds)) ** 2) for r in rolls)
        env.append(rise * decay * (0.55 + roll))
    return env


def crack(rng, seconds):
    """The close strike's crack: bright noise bursts in the first moments."""
    out = [0.0] * int(seconds * RATE)
    for burst in range(5):
        start = int(rng.uniform(0.0, 0.18) * RATE) if burst else 0
        length = int(rng.uniform(0.03, 0.09) * RATE)
        strength = 1.0 if burst == 0 else rng.uniform(0.35, 0.7)
        for i in range(length):
            if start + i < len(out):
                out[start + i] += rng.uniform(-1.0, 1.0) * strength * (1.0 - i / length) ** 2
    return low_pass(out, 3200.0)


def normalised(samples, peak):
    top = max(abs(s) for s in samples) or 1.0
    # A soft clip, as an old tape would.
    return [math.tanh(1.6 * s / top) / math.tanh(1.6) * peak for s in samples]


def write(name, samples):
    path = os.path.join(ROOT, name)
    with wave.open(path, 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b''.join(struct.pack('<h', int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))
    print('wrote', path)


def main():
    rng = random.Random(1989)
    seconds = 6.0
    body = rumble(rng, seconds, 140.0)
    env = swells(rng, seconds, 4, 0.02)
    snap = crack(rng, seconds)
    close = [b * e + 0.9 * c for b, e, c in zip(body, env, snap)]
    write('thunder_close.wav', normalised(close, 0.9))

    seconds = 7.0
    body = rumble(rng, seconds, 70.0)
    env = swells(rng, seconds, 3, 0.6)
    write('thunder_far.wav', normalised([b * e for b, e in zip(body, env)], 0.6))


if __name__ == '__main__':
    main()
