# M5StickC Plus 1.1 — Pocket Voice Recorder

An original **M5StickC Plus v1.1** captures microphone audio and streams it to a Mac over a **trusted 2.4 GHz Wi-Fi network**. The Mac saves WAV files and transcribes them locally with **whisper.cpp**. No cloud API or ChatGPT key required.

**This project targets the original Plus v1.1, not the Plus2.** The Mac and M5Stick must remain on the same reachable local network while recording. Audio cannot be stored offline on the device.

## 1. Mac installation

Requires Python 3.10+, Homebrew, and a Mac.

    git clone https://github.com/coobytron/m5-stick---voice---Chat-GPT.git
    cd m5-stick---voice---Chat-GPT
    brew install whisper.cpp
    mkdir -p models
    curl -fL --retry 3 https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin -o models/ggml-base.en.bin
    openssl rand -hex 16

Copy the resulting **32-character token** and start the server:

    export VOICE_TOKEN='PASTE_YOUR_32_HEX_CHARACTERS'
    python3 -m server.voice_server --out recordings --model models/ggml-base.en.bin

The receiver listens on TCP **8788** and saves audio to **recordings/**. Allow inbound connections in macOS Firewall if prompted. Keep your Mac awake during recordings. The English-only base model is a starting point; you can select a different downloaded GGML model with --model.

### Test without hardware

Open a second terminal in the project folder with the *same* VOICE_TOKEN environment variable:

    python3 -m server.send_test_audio

This sends a 1-second 440 Hz test tone to test connectivity and the WAV/Whisper pipeline; it is **not speech**. To test actual transcription, send a 16 kHz mono 16-bit PCM WAV:

    ffmpeg -i voice.m4a -ar 16000 -ac 1 -c:a pcm_s16le /tmp/voice-test.wav
    python3 -m server.send_test_audio --wav /tmp/voice-test.wav

ffmpeg is only needed for conversion; the receiver itself has no pip dependencies.

## 2. Configure and flash M5StickC Plus v1.1

Install [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html) or the PlatformIO extension in VS Code. Use a USB cable supporting data.

    cp firmware/include/secrets.h.example firmware/include/secrets.h

Edit **firmware/include/secrets.h** to supply:

- WIFI_SSID and WIFI_PASSWORD: your 2.4 GHz Wi-Fi credentials
- SERVER_HOST: Mac's LAN IPv4 address, not 127.0.0.1; find it in System Settings → Wi-Fi → Details → TCP/IP
- SERVER_PORT: 8788
- VOICE_TOKEN: the exact same 32 characters used by the server

Then:

    cd firmware
    pio run
    pio run --target upload
    pio device monitor --baud 115200

**board = m5stick-c is intentional** for the original Plus with M5Unified. Do not use the Plus2 board. If automatic serial detection fails, use pio device list and specify an upload port.

## Buttons and display

| Mode | Input | Result |
|---|---|---|
| READY | A | Connects, begins recording |
| RECORDING | A | Stops recording and requests transcription |
| RECORDING | B | Inserts a timestamp bookmark |
| TRANSCRIBING | — | Waits for Mac |
| SAVED / ERROR | A | Starts next recording or retries |

The M5Stick screen shows the elapsed time, mic level, bookmark count, and status. **Transcripts are saved on the Mac**, not displayed on the tiny screen.

## Outputs

Each successful take produces a timestamped .wav, .txt, and .md file in recordings/. The Markdown includes the transcript and bookmark timestamps. Incomplete sessions retain a .partial.wav and .error.txt. If Whisper fails, the WAV is preserved along with a .error.txt.

## Tests

    python3 -m unittest discover -s tests -v

These tests use a fake Whisper executable and require no hardware or model. They cover complete audio, bookmarks, wrong token, network interruption and malformed frames. **Actual firmware compilation and microphone capture on a Plus v1.1 have not yet been validated on physical hardware.**

## Troubleshooting

- **Mac unavailable:** verify the Mac IPv4 address, firewall, that the server is running, and that the network does not isolate devices (guest networks commonly do).
- **Server rejected connection:** token must match exactly and be 32 ASCII characters.
- **Mic recording failed:** confirm the original M5StickC Plus v1.1 and current M5Unified library.
- **Stream lost / overflow:** the device has about 2 seconds of audio buffering for Wi-Fi stalls. A partial WAV remains on the Mac when the take is interrupted.
- **WAV saved; Whisper failed:** read the .error.txt file, verify whisper-cli and the downloaded model, and re-transcribe the saved WAV.
- **Battery:** the 120 mAh battery is small. Use USB power for long recordings.

## Privacy and architecture

Wi-Fi data is **unencrypted TCP**. VOICE_TOKEN helps reject unknown clients but **does not encrypt recordings or credentials in transit**. Use only a trusted private LAN; do not port-forward 8788 or expose it to the internet. Do not record other people without their consent. The recordings, downloaded models and device secrets are excluded from Git by .gitignore.

Protocol: client sends 44 bytes (M5VOICE1 + uint32 16000 + 32-byte ASCII token), waits for READY, then sends uint16 little-endian frame lengths followed by PCM16 mono data. 0xFFFF is a bookmark, 0xFFFE aborts, and 0 ends the take. Server responds TRANSCRIBING and then DONE or SAVED_AUDIO.

Relevant references: [M5Unified](https://github.com/m5stack/M5Unified), [whisper.cpp](https://github.com/ggml-org/whisper.cpp).
