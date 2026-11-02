#!/usr/bin/env python3
"""Analyze a raw audio dump written by the game's --audio_dump option.

The dump holds every frame the guest submitted with XAudioSubmitRenderDriverFrame:
6 channels x 256 samples, channel-major, big-endian float32, 48 kHz.

Prints, per second of audio, the RMS level and the share of near-silent frames,
which tells apart a starved stream (music interleaved with silence) from a
continuous one. Optionally writes a stereo 16-bit WAV (front L/R) to listen to.

Usage: analyze_audio_dump.py <dump> [--wav out.wav]
"""
import argparse
import array
import math
import sys
import wave

FRAME_SAMPLES = 256
CHANNELS = 6
RATE = 48000
FRAME_BYTES = FRAME_SAMPLES * CHANNELS * 4
FRAMES_PER_SEC = RATE // FRAME_SAMPLES  # 187 (187.5)
SILENCE_RMS = 1e-4


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--wav")
    args = ap.parse_args()

    data = open(args.dump, "rb").read()
    n = len(data) // FRAME_BYTES
    print(f"{n} frames = {n * FRAME_SAMPLES / RATE:.1f} s of audio")
    wav = None
    if args.wav:
        wav = wave.open(args.wav, "wb")
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(RATE)

    sec_rms, sec_silent, sec_frames = 0.0, 0, 0
    print("  sec   rms     silent-frames")
    for i in range(n):
        f = array.array("f", data[i * FRAME_BYTES:(i + 1) * FRAME_BYTES])
        if sys.byteorder == "little":
            f.byteswap()
        left = f[0:FRAME_SAMPLES]
        right = f[FRAME_SAMPLES:2 * FRAME_SAMPLES]
        acc = 0.0
        for c in range(CHANNELS):
            ch = f[c * FRAME_SAMPLES:(c + 1) * FRAME_SAMPLES]
            acc += sum(x * x for x in ch)
        rms = math.sqrt(acc / (FRAME_SAMPLES * CHANNELS))
        sec_rms += rms
        sec_silent += rms < SILENCE_RMS
        sec_frames += 1
        if wav:
            out = array.array("h")
            for l, r in zip(left, right):
                out.append(max(-32768, min(32767, int(l * 32767))))
                out.append(max(-32768, min(32767, int(r * 32767))))
            if sys.byteorder == "big":
                out.byteswap()
            wav.writeframes(out.tobytes())
        if sec_frames == FRAMES_PER_SEC or i == n - 1:
            sec = i * FRAME_SAMPLES // RATE
            bar = "#" * int(sec_silent * 40 / sec_frames)
            print(f"{sec:5d}  {sec_rms / sec_frames:.4f}  {sec_silent:3d}/{sec_frames} {bar}")
            sec_rms, sec_silent, sec_frames = 0.0, 0, 0
    if wav:
        wav.close()
        print(f"wrote {args.wav}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
