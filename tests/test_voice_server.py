"""Test the receiver without hardware or a Whisper model."""
import socket
import struct
import tempfile
import threading
import time
import unittest
import wave
from pathlib import Path

from server.voice_server import VoiceTCPServer, HEADER, MAGIC, SAMPLE_RATE, BOOKMARK

TOKEN = "1234567890abcdef1234567890abcdef"


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        model = self.root / "mock-model.bin"
        model.write_text("test-only")
        whisper = self.root / "fake-whisper-cli"
        whisper.write_text("""#!/usr/bin/env python3
import sys
from pathlib import Path
args = sys.argv
out = Path(args[args.index('-of') + 1] + '.txt')
out.write_text('Testing one two three.\\n')
""")
        whisper.chmod(0o755)
        self.server = VoiceTCPServer(("127.0.0.1", 0), TOKEN, self.root / "recordings",
                                     str(whisper), model)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def client(self, token=TOKEN, rate=SAMPLE_RATE):
        sock = socket.create_connection(("127.0.0.1", self.server.server_address[1]))
        sock.settimeout(5)
        sock.sendall(HEADER.pack(MAGIC, rate, token.encode("ascii")))
        return sock

    def read_line(self, sock):
        line = bytearray()
        while not line.endswith(b"\n"):
            data = sock.recv(1)
            if not data:
                raise EOFError()
            line += data
        return line.decode().strip()

    def test_recording_transcript_and_bookmark(self):
        sock = self.client()
        self.assertEqual(self.read_line(sock), "READY")
        samples = struct.pack("<512h", *([1200] * 512))
        sock.sendall(struct.pack("<H", len(samples)) + samples)
        sock.sendall(struct.pack("<H", BOOKMARK))
        sock.sendall(struct.pack("<H", 0))
        self.assertEqual(self.read_line(sock), "TRANSCRIBING")
        self.assertTrue(self.read_line(sock).startswith("DONE "))
        sock.close()
        out = self.root / "recordings"
        files = list(out.glob("*.wav"))
        self.assertEqual(len(files), 1)
        with wave.open(str(files[0]), "rb") as f:
            self.assertEqual((f.getnchannels(), f.getframerate(), f.getsampwidth(), f.getnframes()),
                             (1, 16000, 2, 512))
        notes = list(out.glob("*.md"))
        self.assertEqual(len(notes), 1)
        self.assertIn("Testing one two three", notes[0].read_text())
        self.assertIn("- 00:00", notes[0].read_text())

    def test_wrong_token_rejected_without_files(self):
        sock = self.client("invalid0000000000000000000000000")
        self.assertTrue(self.read_line(sock).startswith("ERR"))
        sock.close()
        self.assertEqual(list((self.root / "recordings").iterdir()), [])

    def test_abrupt_disconnect_retains_partial(self):
        sock = self.client()
        self.assertEqual(self.read_line(sock), "READY")
        sock.sendall(struct.pack("<H", 1024) + b"\x00" * 1024)
        sock.shutdown(socket.SHUT_WR)
        sock.close()
        for _ in range(100):
            if list((self.root / "recordings").glob("*.partial.wav")):
                break
            time.sleep(.02)
        partial = list((self.root / "recordings").glob("*.partial.wav"))
        self.assertEqual(len(partial), 1)
        for _ in range(100):
            try:
                with wave.open(str(partial[0]), "rb") as wav:
                    self.assertEqual(wav.getnframes(), 512)
                break
            except (EOFError, wave.Error):
                time.sleep(.02)
        else:
            self.fail("partial WAV header never finalized")

    def test_odd_frame_rejected(self):
        sock = self.client()
        self.assertEqual(self.read_line(sock), "READY")
        sock.sendall(struct.pack("<H", 3))
        self.assertTrue(self.read_line(sock).startswith("ERR"))
        sock.close()


if __name__ == "__main__":
    unittest.main()
