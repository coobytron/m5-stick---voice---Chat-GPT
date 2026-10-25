"""No-hardware tests for importing an offline microSD recording."""
import tempfile
import unittest
from pathlib import Path
import wave

from server.import_sd import get_bookmarks, import_card


class CardImportTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = Path(self.dir.name)
        self.card = self.root / "card"
        self.card.mkdir()
        with wave.open(str(self.card / "REC0001.WAV"), "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(16000)
            f.writeframes(b"\x00\x00" * 1600)
        (self.card / "REC0001.TXT").write_text("seconds\n0.05\n0.10\n", encoding="utf-8")

    def test_copy_only_keeps_source_and_sidecars(self):
        batch, count = import_card(self.card, self.root / "output", None)
        self.assertEqual(count, 1)
        self.assertTrue((batch / "REC0001.WAV").is_file())
        self.assertTrue((batch / "REC0001.TXT").is_file())
        self.assertTrue((self.card / "REC0001.WAV").is_file())
        self.assertEqual(get_bookmarks(batch / "REC0001.TXT"), [0.05, 0.10])

    def test_empty_card_is_a_clear_error(self):
        (self.card / "REC0001.WAV").unlink()
        with self.assertRaisesRegex(FileNotFoundError, "No REC"):
            import_card(self.card, self.root / "output", None)

    def test_whisper_import_outputs_markdown(self):
        fake = self.root / "whisper-cli"
        fake.write_text(
            '#!/usr/bin/env python3\n'
            'import sys\n'
            'from pathlib import Path\n'
            'args = sys.argv\n'
            'Path(args[args.index("-of")+1] + ".txt").write_text("Offline note.\\n")\n',
            encoding="utf-8")
        fake.chmod(0o755)
        model = self.root / "model.bin"
        model.write_bytes(b"test")
        batch, count = import_card(self.card, self.root / "output", model, str(fake))
        self.assertEqual(count, 1)
        note = (batch / "REC0001.md").read_text(encoding="utf-8")
        self.assertIn("Offline note.", note)
        self.assertIn("- 00:00", note)


if __name__ == "__main__":
    unittest.main()
