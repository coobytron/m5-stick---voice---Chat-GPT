# XIAO ESP32S3 Sense pocket voice recorder

**Standalone**, with no M5Stick, Wi-Fi, cloud account, Mac, or iPhone needed during recording. The Seeed Studio XIAO ESP32S3 Sense kit includes a PDM microphone and a microSD socket on its **Sense expansion board**. The camera is not required.

## Parts and wiring

- **XIAO ESP32S3 Sense kit**, including stacked Sense expansion board (not the plain XIAO).
- **32GB microSDHC**, formatted FAT32, inserted into Sense expansion board.
- Protected 3.7V LiPo cell (500–1000mAh) connected by soldering to BAT+ and BAT- pads; **no JST battery socket on the XIAO itself**.
- External momentary pushbutton: **D1/GPIO2 -> switch -> GND**.
- Optional external status LED: **D0/GPIO1 -> 330-ohm resistor -> LED anode (+); LED cathode (-) -> GND**.
- An insulated enclosure and optional appropriate switch in battery-positive lead, plus USB-C data cable.

Mic is wired on the expansion board (GPIO42 clock / GPIO41 data), as is microSD (GPIO7 SCK / GPIO8 MISO / GPIO9 MOSI / chip-select). No external mic or external SD reader module needed.

IMPORTANT: The on-board USER LED uses GPIO21 and can conflict with the expansion SD chip-select on some revisions. The firmware **does not drive the onboard LED**. Use an optional external LED on D0 instead. Seeed's SD tutorial documents microSD CS as GPIO21, though some Sense expansion revisions may use GPIO3; check the revision markings/schematic and set SD_CS_PIN in platformio.ini accordingly.

Battery caution: confirm BAT+/BAT- polarity with a meter, use a single-cell protected LiPo with compatible charging characteristics, do not solder while powered, insulate joints, and never expose battery pads to 5V. If you aren't comfortable soldering LiPo wires, have an experienced assembler connect them. USB-C powers the board during initial testing and charges an attached compatible cell. Use a suitable battery disconnect switch for portable use.

## Firmware: compile / flash

Install PlatformIO CLI or VS Code extension and connect the board over a USB-C data cable.

    cd xiao-voice-recorder
    pio run
    pio run --target upload
    pio device monitor --baud 115200

The target is seeed_xiao_esp32s3 and the initial SD_CS_PIN is 21. If SD initialization fails on your board revision, check the schematic and consider SD_CS_PIN=3. Verify the SD card is FAT32. Some macOS USB port configurations require a bootloader button sequence; follow Seeed's guide if no serial port is detected.

## Operation

1. Press external D1 button once to start recording; external D0 LED turns on.
2. Press it again to stop, finalize the WAV, and turn LED off.
3. Each recording gets REC0001.WAV, REC0002.WAV, etc. on the card.
4. Errors flash the external LED and print details to USB Serial.

Recording format is **16kHz / mono / 16-bit PCM WAV**; 1 hour per file maximum. Each 60 seconds takes roughly 1.92 MB. Audio capture runs in a separate FreeRTOS task, buffered between the microphone and SD storage. A WAV header is checkpointed approximately every 5 seconds for better interrupted-file recovery, but always stop before removing the card. Extreme SD-card write stalls can overflow the buffer; a partial recording and ERR file are then preserved where possible.

No built-in speech transcription, clock/time sync, display, or battery gauge in v1.

## Transcribe on your Mac

The current repository already includes a local Mac whisper.cpp integration and SD import script. After inserting the card into a USB-C reader:

    # At the repository root:
    brew install whisper.cpp
    mkdir -p models
    curl -fL --retry 3 https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin -o models/ggml-base.en.bin
    python3 -m server.import_sd --card /Volumes/YOUR_CARD --model models/ggml-base.en.bin

The importer copies audio; it does not erase the microSD. The card can also be read from an iPhone via a suitable USB-C microSD reader, although an iPhone transcription companion app is not yet implemented.

## References

- [Seeed microphone documentation](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/)
- [Seeed microSD documentation](https://wiki.seeedstudio.com/xiao_esp32s3_sense_filesystem/)
- [Seeed battery documentation](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [PlatformIO board definition](https://docs.platformio.org/en/latest/boards/espressif32/seeed_xiao_esp32s3.html)

Physical recording, charging, and endurance must be verified with your assembled hardware; compile checks alone do not establish these.
