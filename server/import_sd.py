"""Import WAV voice notes from an M5Stick microSD card and transcribe on Mac.

Does NOT delete files from the microSD card.
    python3 -m server.import_sd --card /Volumes/M5VOICE --model models/ggml-base.en.bin
"""
from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import shutil
import secrets

from .voice_server import render_markdown, transcribe


def get_bookmarks(text_file: Path) -> list[float]:
    if not text_file.exists():
        return []
    values = []
    for line in text_file.read_text(encoding="utf-8").splitlines():
        try:
            value = float(line.strip())
            if value >= 0:
                values.append(value)
        except ValueError:
            pass  # header line "seconds"
    return values


def import_card(card: Path, out: Path, model: Path | None, whisper_cli: str = "whisper-cli",
                timeout: int = 600) -> tuple[Path, int]:
    if not card.is_dir():
        raise FileNotFoundError(f"microSD path does not exist: {card}")
    files = sorted(p for p in card.iterdir()
                   if p.is_file() and p.suffix.upper() == ".WAV" and p.stem.upper().startswith("REC"))
    if not files:
        raise FileNotFoundError("No REC*.WAV recordings found on this card")
    batch = out / (datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(3))
    batch.mkdir(parents=True)
    for source in files:
        dest = batch / source.name
        shutil.copy2(source, dest)
        for extension in (".TXT", ".ERR"):
            companion = source.with_suffix(extension)
            if companion.exists():
                shutil.copy2(companion, batch / companion.name)
        if model is None:
            print(f"Copied {source.name}")
            continue
        try:
            transcript = transcribe(dest, batch / dest.stem, whisper_cli, model, timeout)
            notes = render_markdown(dest.name, transcript,
                                    get_bookmarks(source.with_suffix(".TXT")))
            (batch / (source.stem + ".md")).write_text(notes, encoding="utf-8")
            print(f"Transcribed {source.name}")
        except Exception as error:
            (batch / (source.stem + ".error.txt")).write_text(str(error) + "\n", encoding="utf-8")
            print(f"Transcription failed for {source.name}: {error}")
    return batch, len(files)


def main():
    parser = argparse.ArgumentParser(description="Import M5Stick microSD voice recordings")
    parser.add_argument("--card", required=True, type=Path,
                        help="mounted card path, e.g. /Volumes/M5VOICE")
    parser.add_argument("--out", type=Path, default=Path("recordings/imported"))
    parser.add_argument("--model", type=Path, default=Path("models/ggml-base.en.bin"))
    parser.add_argument("--whisper-cli", default="whisper-cli")
    parser.add_argument("--copy-only", action="store_true",
                        help="copy files without running transcription")
    args = parser.parse_args()
    if not args.copy_only:
        if not args.model.is_file():
            parser.error(f"Missing Whisper model: {args.model}")
        if not shutil.which(args.whisper_cli) and not Path(args.whisper_cli).is_file():
            parser.error(f"whisper-cli not found: {args.whisper_cli}")
    dest, count = import_card(args.card, args.out,
                              None if args.copy_only else args.model,
                              args.whisper_cli)
    print(f"Imported {count} recording(s) into {dest}")


if __name__ == "__main__":
    main()
