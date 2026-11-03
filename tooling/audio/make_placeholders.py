"""Writes a short silence for every placeholder sound in assets/sound/
sounds.json (a .wav whose description starts "Placeholder") that has no
samples yet, so the game has a file to load until the real sound is made.
A file with samples is left alone. Standard library only.

    py -3 make_placeholders.py
"""

import json
import os
import wave

RATE = 22050
SECONDS = 0.1
SOUND_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'assets', 'sound'))


def has_samples(path):
    try:
        with wave.open(path, 'rb') as sound:
            return sound.getnframes() > 0
    except (OSError, wave.Error, EOFError):
        return False


def write_silence(path):
    with wave.open(path, 'wb') as sound:
        sound.setnchannels(1)
        sound.setsampwidth(2)
        sound.setframerate(RATE)
        sound.writeframes(b'\x00\x00' * int(RATE * SECONDS))


def main():
    with open(os.path.join(SOUND_ROOT, 'sounds.json'), encoding='utf-8') as manifest:
        sounds = json.load(manifest)['sounds']
    for sound in sounds:
        path = os.path.join(SOUND_ROOT, sound['file'])
        placeholder = sound.get('description', '').startswith('Placeholder')
        if placeholder and path.endswith('.wav') and not has_samples(path):
            write_silence(path)
            print('wrote', os.path.relpath(path, SOUND_ROOT))


if __name__ == '__main__':
    main()
