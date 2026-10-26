# Standalone ESP32-S3 pocket voice recorder

**Current hardware:** Seeed Studio XIAO ESP32S3 **Sense** (with microphone + microSD expansion). No M5Stick required.

- **[Build instructions, wiring and firmware →](xiao-voice-recorder/README.md)**
- [Firmware source](xiao-voice-recorder/src/main.cpp)
- [PlatformIO configuration](xiao-voice-recorder/platformio.ini)
- [Import and transcribe WAV recordings on your Mac](server/import_sd.py)
- [Original M5Stick project, archived](legacy-m5stick.md)

## Features

- One external record button, optional status LED.
- Built-in microphone and microSD for completely offline recording.
- 16 kHz mono PCM WAV, an hour per file, with recovery checkpoints.
- Local Whisper transcription on Mac after removing the microSD card.
- A future iPhone sync/transcription app can use the same WAV files.

### Quick build

```sh
cd xiao-voice-recorder
pio run
pio run --target upload
pio device monitor --baud 115200
```

Hardware required: XIAO ESP32S3 Sense (the Sense variant), 32GB FAT32 microSD, pushbutton on D1/GPIO2 -> GND, optional external LED on D0, and for portability a safely wired protected 3.7V LiPo battery.

This is a firmware project, not a finished commercially assembled recorder. Flashing and real-world microphone/SD validation still require the physical board.
