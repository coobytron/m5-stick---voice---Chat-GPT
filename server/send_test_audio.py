"""Simulate the M5Stick on the Mac before uploading firmware.

    VOICE_TOKEN=... python3 -m server.send_test_audio --host 127.0.0.1
    VOICE_TOKEN=... python3 -m server.send_test_audio --wav 16000hz-mono.wav
"""
from __future__ import annotations
import argparse
import math
import os
import socket
import struct
import wave

from .voice_server import HEADER, MAGIC, SAMPLE_RATE, BOOKMARK


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8788)
    p.add_argument("--wav", help="optional PCM 16-bit mono 16000Hz WAV file")
    args = p.parse_args()
    token = os.environ.get("VOICE_TOKEN", "")
    if len(token) != 32 or not token.isascii():
        p.error("set VOICE_TOKEN to the same 32-character value as the server")
    if args.wav:
        with wave.open(args.wav, "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, SAMPLE_RATE):
                p.error("--wav must be 16 kHz, mono, 16-bit PCM")
            audio = wav.readframes(wav.getnframes())
    else:
        audio = struct.pack("<16000h", *[int(6000 * math.sin(2 * math.pi * 440 * t / SAMPLE_RATE))
                                        for t in range(SAMPLE_RATE)])
    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        sock.settimeout(700)
        sock.sendall(HEADER.pack(MAGIC, SAMPLE_RATE, token.encode("ascii")))
        print("Server:", read_line(sock))
        for start in range(0, len(audio), 1024):
            block = audio[start:start+1024]
            sock.sendall(struct.pack("<H", len(block)) + block)
        sock.sendall(struct.pack("<H", BOOKMARK))
        sock.sendall(b"\x00\x00")
        print("Server:", read_line(sock))
        print("Server:", read_line(sock))


def read_line(sock):
    result = bytearray()
    while True:
        c = sock.recv(1)
        if not c:
            raise EOFError("server closed connection")
        if c == b"\n":
            return result.decode("utf-8")
        result.extend(c)


if __name__ == "__main__":
    main()
