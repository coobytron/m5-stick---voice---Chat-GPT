"""M5StickC Plus receiver: TCP PCM16 audio -> WAV -> local whisper.cpp transcript.

Python standard library only. Traffic is plaintext; use a trusted LAN.
"""
from __future__ import annotations

import argparse
import hmac
import os
from pathlib import Path
import secrets
import shutil
import socket
import socketserver
import struct
import subprocess
from datetime import datetime, timezone
import wave

MAGIC = b"M5VOICE1"
HEADER = struct.Struct("<8sI32s")
FRAME_LEN = struct.Struct("<H")
SAMPLE_RATE = 16_000
MAX_FRAME = 2048
BOOKMARK = 0xFFFF
ABORT = 0xFFFE


def exact(sock: socket.socket, count: int) -> bytes:
    chunks = []
    remaining = count
    while remaining:
        data = sock.recv(remaining)
        if not data:
            raise EOFError("connection closed before recording finished")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


def stamp() -> str:
    now = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return f"{now}-{secrets.token_hex(3)}"


def render_markdown(audio_name: str, transcript: str, bookmarks: list[float],
                    incomplete: bool = False) -> str:
    lines = [f"# {audio_name}", "", f"Audio: {audio_name}", "",
             f"Status: {'incomplete recording' if incomplete else 'complete'}", "",
             "## Transcript", "", transcript.strip() or "(No speech detected)", "",
             "## Bookmarks", ""]
    if bookmarks:
        lines += [f"- {int(s // 60):02d}:{int(s % 60):02d}" for s in bookmarks]
    else:
        lines.append("(none)")
    return "\n".join(lines) + "\n"


def transcribe(wav_path: Path, output_base: Path, whisper_cli: str,
               model: Path, timeout: int) -> str:
    proc = subprocess.run(
        [whisper_cli, "-m", str(model), "-f", str(wav_path),
         "-otxt", "-of", str(output_base)],
        capture_output=True, text=True, timeout=timeout, check=False,
    )
    if proc.returncode != 0:
        tail = (proc.stderr or proc.stdout or "no diagnostic")[-1000:]
        raise RuntimeError(f"whisper-cli exit {proc.returncode}: {tail}")
    txt_path = output_base.with_suffix(".txt")
    if not txt_path.exists():
        raise RuntimeError("whisper-cli did not write a .txt file")
    return txt_path.read_text(encoding="utf-8")


class VoiceTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, addr: tuple[str, int], token: str, output: Path,
                 whisper_cli: str, model: Path, max_seconds: int = 3600,
                 transcribe_timeout: int = 600):
        self.token = token.encode("ascii")
        self.output = output
        self.whisper_cli = whisper_cli
        self.model = model
        self.max_bytes = max_seconds * SAMPLE_RATE * 2
        self.transcribe_timeout = transcribe_timeout
        self.output.mkdir(parents=True, exist_ok=True)
        super().__init__(addr, VoiceHandler)


class VoiceHandler(socketserver.BaseRequestHandler):
    def handle(self):
        conn: socket.socket = self.request
        conn.settimeout(15)
        name = stamp()
        out = self.server.output
        temp_wav = out / f"{name}.partial.wav"
        wav_path = out / f"{name}.wav"
        received = 0
        bookmarks: list[float] = []
        accepted = False
        try:
            magic, rate, raw_token = HEADER.unpack(exact(conn, HEADER.size))
            token = raw_token.rstrip(b"\x00")
            if magic != MAGIC or rate != SAMPLE_RATE or not hmac.compare_digest(token, self.server.token):
                conn.sendall(b"ERR authentication or audio format\n")
                return
            accepted = True
            with wave.open(str(temp_wav), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(SAMPLE_RATE)
                conn.sendall(b"READY\n")
                while True:
                    size = FRAME_LEN.unpack(exact(conn, 2))[0]
                    if size == 0:
                        break
                    if size == BOOKMARK:
                        bookmarks.append(received / (SAMPLE_RATE * 2))
                        continue
                    if size == ABORT:
                        raise RuntimeError("device stopped after audio buffer/network failure")
                    if size % 2 or size > MAX_FRAME:
                        raise ValueError(f"invalid audio frame size: {size}")
                    if received + size > self.server.max_bytes:
                        raise ValueError("maximum recording duration reached")
                    data = exact(conn, size)
                    wav.writeframesraw(data)
                    received += size
            if not received:
                raise ValueError("empty recording")
            temp_wav.replace(wav_path)
            conn.settimeout(self.server.transcribe_timeout + 10)
            conn.sendall(b"TRANSCRIBING\n")
            try:
                transcription = transcribe(wav_path, out / name,
                                           self.server.whisper_cli,
                                           self.server.model,
                                           self.server.transcribe_timeout)
                (out / f"{name}.md").write_text(
                    render_markdown(wav_path.name, transcription, bookmarks),
                    encoding="utf-8")
                conn.sendall(f"DONE {name}.md\n".encode("ascii"))
                print(f"[saved] {wav_path.name} + transcript", flush=True)
            except Exception as error:
                (out / f"{name}.error.txt").write_text(str(error) + "\n", encoding="utf-8")
                conn.sendall(b"SAVED_AUDIO transcription failed\n")
                print(f"[transcribe-error] {name}: {error}", flush=True)
        except (EOFError, OSError, ValueError, RuntimeError) as error:
            if accepted:
                print(f"[incomplete] {name}: {error}", flush=True)
            else:
                print(f"[rejected] {self.client_address[0]}: {error}", flush=True)
            if accepted and temp_wav.exists():
                (out / f"{name}.error.txt").write_text(str(error) + "\n", encoding="utf-8")
            try:
                conn.sendall(b"ERR recording interrupted\n")
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser(description="M5Stick voice recording receiver")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8788)
    parser.add_argument("--out", type=Path, default=Path("recordings"))
    parser.add_argument("--model", type=Path, default=Path("models/ggml-base.en.bin"))
    parser.add_argument("--whisper-cli", default="whisper-cli")
    parser.add_argument("--max-seconds", type=int, default=3600)
    args = parser.parse_args()
    token = os.environ.get("VOICE_TOKEN", "")
    if len(token) != 32 or not token.isascii():
        parser.error("set VOICE_TOKEN to a 32-character ASCII token (e.g., openssl rand -hex 16)")
    if not args.model.is_file():
        parser.error(f"missing Whisper model: {args.model}")
    if not shutil.which(args.whisper_cli) and not Path(args.whisper_cli).is_file():
        parser.error(f"whisper-cli not found: {args.whisper_cli}")
    if args.max_seconds <= 0:
        parser.error("--max-seconds must be greater than zero")
    with VoiceTCPServer((args.host, args.port), token, args.out, args.whisper_cli,
                        args.model, args.max_seconds) as server:
        print(f"Listening on {args.host}:{server.server_address[1]}", flush=True)
        print(f"Saving recordings under {args.out.resolve()}", flush=True)
        print("Traffic is unencrypted. Use only on your trusted Wi-Fi network.", flush=True)
        try:
            server.serve_forever(poll_interval=0.2)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
