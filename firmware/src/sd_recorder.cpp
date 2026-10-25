// Offline microSD firmware for the original M5StickC Plus 1.1.
// Wiring: SCK=33 (Grove), MISO=36 (HAT), MOSI=26 (HAT), CS=32 (Grove).
// 3V3 and GND from the HAT. NEVER use the Grove 5V pin for a 3V-only card.
#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>

namespace {
constexpr int kSCK = 33;
constexpr int kMISO = 36;    // input-only; GPIO25 shares its physical port
constexpr int kMOSI = 26;
constexpr int kCS = 32;
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kSamples = 512;  // 32ms, 1024 bytes of PCM16
constexpr uint32_t kMaxSeconds = 3600;  // protect FAT32 WAV size and battery
constexpr uint32_t kCheckpointMs = 5000;

SPIClass cardSPI(HSPI);  // separate host from the internal screen
QueueHandle_t blocks = nullptr;
int16_t micData[2][kSamples] = {};
std::atomic<bool> capturing{false};
std::atomic<bool> overrun{false};
std::atomic<uint16_t> peak{0};
File wav;
File bookmarks;
bool sdMounted = false;
uint32_t dataBytes = 0;
uint32_t startMs = 0;
uint32_t paintedAt = 0;
uint32_t checkedAt = 0;
uint16_t bookmarkCount = 0;
char filename[24] = {};
char bookmarkName[24] = {};
char errorName[24] = {};
String message = "Insert FAT32 microSD";
enum class State { Ready, Recording, Saved, Error };
State state = State::Ready;

struct Block { int16_t samples[kSamples]; };

void onMicBuffer(void*, void* data, size_t length) {
  if (!capturing.load(std::memory_order_acquire)) return;
  if (length != kSamples) { overrun.store(true); return; }
  Block block;
  memcpy(block.samples, data, sizeof(block.samples));
  uint16_t p = 0;
  for (size_t i = 0; i < kSamples; i += 8) {
    int32_t amp = block.samples[i];
    if (amp < 0) amp = -amp;
    if (amp > 32767) amp = 32767;
    if (amp > p) p = static_cast<uint16_t>(amp);
  }
  peak.store(p, std::memory_order_relaxed);
  if (xQueueSend(blocks, &block, 0) != pdTRUE) overrun.store(true);
  // M5Unified allows re-queuing the just-released buffer in this callback.
  if (capturing.load(std::memory_order_acquire) &&
      !M5.Mic.record(static_cast<int16_t*>(data), kSamples)) overrun.store(true);
}

void put32(uint8_t* dst, uint32_t value) {
  for (int i = 0; i < 4; ++i) dst[i] = (value >> (8 * i)) & 255;
}
void put16(uint8_t* dst, uint16_t value) {
  dst[0] = value & 255; dst[1] = value >> 8;
}

bool updateWavHeader() {
  if (!wav || dataBytes > 0xFFFFFFFFUL - 36UL) return false;
  uint8_t header[44] = {};
  memcpy(header, "RIFF", 4);
  put32(header + 4, 36 + dataBytes);
  memcpy(header + 8, "WAVEfmt ", 8);
  put32(header + 16, 16);       // PCM fmt chunk
  put16(header + 20, 1);        // integer PCM
  put16(header + 22, 1);        // mono
  put32(header + 24, kSampleRate);
  put32(header + 28, kSampleRate * 2);
  put16(header + 32, 2);        // block alignment
  put16(header + 34, 16);       // bits/sample
  memcpy(header + 36, "data", 4);
  put32(header + 40, dataBytes);
  const uint32_t endPosition = 44 + dataBytes;
  if (!wav.seek(0) || wav.write(header, sizeof(header)) != sizeof(header)) return false;
  wav.flush();  // update on-card header to make interrupted recordings recoverable
  return wav.seek(endPosition);
}

bool mountCard() {
  if (sdMounted) return true;
  pinMode(25, INPUT); // GPIO25 and GPIO36 share a port on the original Plus.
  pinMode(kCS, OUTPUT);
  digitalWrite(kCS, HIGH);
  cardSPI.begin(kSCK, kMISO, kMOSI, kCS);
  sdMounted = SD.begin(kCS, cardSPI, 4000000);
  if (sdMounted && SD.cardType() == CARD_NONE) sdMounted = false;
  return sdMounted;
}

bool nextFilename() {
  for (unsigned id = 1; id <= 9999; ++id) {
    snprintf(filename, sizeof(filename), "/REC%04u.WAV", id);
    if (!SD.exists(filename)) {
      snprintf(bookmarkName, sizeof(bookmarkName), "/REC%04u.TXT", id);
      snprintf(errorName, sizeof(errorName), "/REC%04u.ERR", id);
      return true;
    }
  }
  return false;
}

void display(bool force = false) {
  if (!force && millis() - paintedAt < 200) return;
  paintedAt = millis();
  const char* title = "READY";
  uint16_t color = TFT_CYAN;
  if (state == State::Recording) { title = "RECORDING"; color = TFT_RED; }
  if (state == State::Saved) { title = "SAVED"; color = TFT_GREEN; }
  if (state == State::Error) { title = "SD ERROR"; color = TFT_ORANGE; }
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.print(title);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  if (state == State::Recording) {
    const uint32_t seconds = (millis() - startMs) / 1000;
    M5.Display.setCursor(8, 38);
    M5.Display.printf("%s  %02lu:%02lu", filename,
        static_cast<unsigned long>(seconds / 60),
        static_cast<unsigned long>(seconds % 60));
    M5.Display.setCursor(8, 62);
    M5.Display.printf("Bookmarks: %u", bookmarkCount);
    M5.Display.drawRect(8, 84, 222, 11, TFT_DARKGREY);
    const int width = min(220, static_cast<int>(peak.load() * 220UL / 16000UL));
    M5.Display.fillRect(9, 85, width, 9, TFT_GREEN);
    M5.Display.setCursor(8, 114);
    M5.Display.print("A: Stop     B: Bookmark");
  } else {
    M5.Display.setCursor(8, 47);
    M5.Display.print(message.substring(0, 38));
    M5.Display.setCursor(8, 77);
    M5.Display.print(sdMounted ? "Storage: microSD" : "No SD card detected");
    M5.Display.setCursor(8, 114);
    M5.Display.print("A: Start / retry");
  }
}

bool saveQueued(size_t limit = 12) {
  Block block;
  while (limit-- && xQueueReceive(blocks, &block, 0) == pdTRUE) {
    const size_t size = sizeof(block.samples);
    if (!wav || wav.write(reinterpret_cast<const uint8_t*>(block.samples), size) != size)
      return false;
    dataBytes += size;
  }
  return true;
}

void saveError(const String& reason) {
  capturing.store(false, std::memory_order_release);
  M5.Mic.end();
  if (wav) { updateWavHeader(); wav.close(); }
  if (bookmarks) bookmarks.close();
  if (filename[0] != '\0') {
    File f = SD.open(errorName, FILE_WRITE);
    if (f) { f.println(reason); f.close(); }
  }
  message = reason;
  state = State::Error;
  Serial.println("Recorder error: " + reason);
  display(true);
}

bool beginRecording() {
  filename[0] = '\0';
  if (!mountCard()) {
    state = State::Error; message = "Check card and wiring"; display(true); return false;
  }
  if (!nextFilename()) {
    state = State::Error; message = "9999 files; archive SD"; display(true); return false;
  }
  wav = SD.open(filename, FILE_WRITE);
  if (!wav) {
    state = State::Error; message = "Cannot create WAV"; display(true); return false;
  }
  bookmarks = SD.open(bookmarkName, FILE_WRITE);
  if (!bookmarks) { saveError("Cannot create bookmarks"); return false; }
  bookmarks.println("seconds");
  dataBytes = 0;
  bookmarkCount = 0;
  if (!updateWavHeader()) { saveError("Cannot initialize WAV"); return false; }
  xQueueReset(blocks);
  overrun.store(false);
  peak.store(0);
  M5.Speaker.end();
  M5.Mic.setBufferReleaseCallback(nullptr, onMicBuffer);
  if (!M5.Mic.begin()) { saveError("Microphone init failed"); return false; }
  capturing.store(true, std::memory_order_release);
  const bool a = M5.Mic.record(micData[0], kSamples, kSampleRate);
  const bool b = a && M5.Mic.record(micData[1], kSamples, kSampleRate);
  if (!a || !b) { saveError("Microphone record failed"); return false; }
  state = State::Recording;
  startMs = millis();
  checkedAt = startMs;
  display(true);
  return true;
}

void finishRecording() {
  capturing.store(false, std::memory_order_release);
  M5.Mic.end();
  if (!overrun.load() && !saveQueued(128)) {
    saveError("SD write failed"); return;
  }
  if (overrun.load()) {
    saveError("Audio buffer overflow; partial WAV saved"); return;
  }
  if (!updateWavHeader()) { saveError("WAV finalize failed"); return; }
  wav.close();
  if (bookmarks) bookmarks.close();
  state = State::Saved;
  message = String(filename) + " saved";
  Serial.println(message);
  display(true);
}

void markTime() {
  if (!saveQueued(128)) { saveError("SD write failed"); return; }
  // Bookmark offset is based on bytes committed to the card, not wall time.
  const float seconds = static_cast<float>(dataBytes) / (kSampleRate * 2);
  bookmarks.printf("%.2f\n", seconds);
  bookmarks.flush();
  ++bookmarkCount;
  display(true);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(85);
  blocks = xQueueCreate(64, sizeof(Block));
  if (!blocks) {
    state = State::Error;
    message = "Out of RAM for queue";
  } else if (mountCard()) {
    message = "A: Record to card";
  } else {
    state = State::Error;
    message = "Insert FAT32 card";
  }
  display(true);
}

void loop() {
  M5.update();
  if (state == State::Recording) {
    if (overrun.load() || !saveQueued()) {
      saveError("Audio dropped or card write failed");
    } else if (millis() - startMs >= kMaxSeconds * 1000UL) {
      finishRecording();
    } else if (M5.BtnA.wasClicked()) {
      finishRecording();
    } else if (M5.BtnB.wasClicked()) {
      markTime();
    } else if (millis() - checkedAt > kCheckpointMs) {
      // A power loss can still lose up to 5s of audio, but older WAV stays readable.
      if (!updateWavHeader()) saveError("SD checkpoint failed");
      checkedAt = millis();
    }
  } else if (blocks && M5.BtnA.wasClicked()) {
    beginRecording();
  }
  display();
  delay(5);
}
