// Pocket Voice Recorder - Seeed Studio XIAO ESP32S3 Sense, original Sense expansion.
// PDM mic (GPIO42 clock, GPIO41 data), microSD SPI (7/8/9, CS 21 by default).
// External momentary button GPIO2 (D1) -> GND.
// Optional external status LED GPIO1 (D0) -> 330R -> LED anode; LED cathode -> GND.
// IMPORTANT: don't use the onboard USER_LED, its GPIO21 can overlap microSD CS.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>

#ifndef SD_CS_PIN
#define SD_CS_PIN 21
#endif

namespace {
constexpr gpio_num_t kMicClk = GPIO_NUM_42;
constexpr gpio_num_t kMicData = GPIO_NUM_41;
constexpr int kButton = 2;          // D1
constexpr int kStatusLed = 1;       // D0: EXTERNAL LED, not built-in LED
constexpr int kSdSck = 7;           // D8
constexpr int kSdMiso = 8;          // D9
constexpr int kSdMosi = 9;          // D10
constexpr uint32_t kRate = 16000;
constexpr uint32_t kMaxSeconds = 3600; // one hour per file
constexpr uint32_t kCheckpointMs = 5000;
constexpr size_t kBlockBytes = 1024; // 32ms / 16-bit mono
constexpr uint32_t kDebounceMs = 45;

struct AudioBlock {
  uint16_t size = 0;
  uint8_t pcm[kBlockBytes];
};

enum class State : uint8_t { Ready, Recording, Saved, Fault };
State state = State::Ready;
SPIClass sdSPI(FSPI);
QueueHandle_t queue = nullptr;
File file;
char name[24] = {};
char errorName[24] = {};
uint32_t audioBytes = 0;
uint32_t startedAt = 0;
uint32_t checkpointAt = 0;
uint32_t lastPrintAt = 0;
bool sdMounted = false;
std::atomic<bool> captureEnabled {false};
std::atomic<bool> captureBusy {false};
std::atomic<bool> captureOverflow {false};

// Little-endian WAV file header, updated during recording and at close.
void put16(uint8_t* out, uint16_t x) {
  out[0] = x & 0xff; out[1] = (x >> 8) & 0xff;
}
void put32(uint8_t* out, uint32_t x) {
  for (int i = 0; i < 4; ++i) out[i] = (x >> (i * 8)) & 0xff;
}
bool writeHeader() {
  if (!file || audioBytes > UINT32_MAX - 36) return false;
  uint8_t hdr[44] = {};
  memcpy(hdr, "RIFF", 4);
  put32(hdr + 4, 36 + audioBytes);
  memcpy(hdr + 8, "WAVEfmt ", 8);
  put32(hdr + 16, 16);           // fmt chunk
  put16(hdr + 20, 1);            // PCM
  put16(hdr + 22, 1);            // mono
  put32(hdr + 24, kRate);
  put32(hdr + 28, kRate * 2);
  put16(hdr + 32, 2);
  put16(hdr + 34, 16);
  memcpy(hdr + 36, "data", 4);
  put32(hdr + 40, audioBytes);
  if (!file.seek(0)) return false;
  if (file.write(hdr, sizeof(hdr)) != sizeof(hdr)) return false;
  file.flush();
  return file.seek(44 + audioBytes);
}

bool mountSD() {
  if (sdMounted) return true;
  sdSPI.begin(kSdSck, kSdMiso, kSdMosi, SD_CS_PIN);
  sdMounted = SD.begin(SD_CS_PIN, sdSPI, 4000000);
  if (sdMounted && SD.cardType() == CARD_NONE) sdMounted = false;
  return sdMounted;
}

bool findNextName() {
  for (uint32_t n = 1; n < 10000; ++n) {
    snprintf(name, sizeof(name), "/REC%04lu.WAV", static_cast<unsigned long>(n));
    if (!SD.exists(name)) {
      snprintf(errorName, sizeof(errorName), "/REC%04lu.ERR", static_cast<unsigned long>(n));
      return true;
    }
  }
  return false;
}

bool initMic() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
  config.sample_rate = kRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 512;
  config.use_apll = false;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_PIN_NO_CHANGE;
  pins.ws_io_num = static_cast<int>(kMicClk);
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = static_cast<int>(kMicData);
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  // PDM capture does not start until Record is pressed.
  i2s_stop(I2S_NUM_0);
  return true;
}

// Capture on a separate core so SD card writes do not normally interrupt the microphone.
void micTask(void*) {
  AudioBlock block;
  for (;;) {
    if (!captureEnabled.load(std::memory_order_acquire)) {
      captureBusy.store(false, std::memory_order_release);
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    captureBusy.store(true, std::memory_order_release);
    size_t bytesRead = 0;
    const esp_err_t status = i2s_read(I2S_NUM_0, block.pcm, sizeof(block.pcm),
                                       &bytesRead, pdMS_TO_TICKS(100));
    if (captureEnabled.load(std::memory_order_acquire)) {
      if (status != ESP_OK && status != ESP_ERR_TIMEOUT) captureOverflow.store(true);
      if (bytesRead) {
        if (bytesRead % 2 || bytesRead > sizeof(block.pcm)) {
          captureOverflow.store(true);
        } else {
          block.size = static_cast<uint16_t>(bytesRead);
          if (xQueueSend(queue, &block, 0) != pdTRUE) captureOverflow.store(true);
        }
      }
    }
    captureBusy.store(false, std::memory_order_release);
  }
}

bool drainAudio(size_t maxBlocks = 16) {
  AudioBlock block;
  while (maxBlocks-- && xQueueReceive(queue, &block, 0) == pdTRUE) {
    if (!file || file.write(block.pcm, block.size) != block.size) return false;
    audioBytes += block.size;
  }
  return true;
}

void statusLED() {
  if (state == State::Recording) {
    digitalWrite(kStatusLed, HIGH);
  } else if (state == State::Fault) {
    digitalWrite(kStatusLed, (millis() / 300) % 2 ? HIGH : LOW);
  } else {
    digitalWrite(kStatusLed, LOW);
  }
}

void stopCapture() {
  captureEnabled.store(false, std::memory_order_release);
  // micTask's read has a 100 ms timeout; wait for its final write to finish.
  const uint32_t start = millis();
  while (captureBusy.load(std::memory_order_acquire) && millis() - start < 500)
    delay(2);
  i2s_stop(I2S_NUM_0);
}

void fault(const char* why) {
  stopCapture();
  // Drain any queued PCM before closing; even faulty takes retain a useful WAV.
  if (file) {
    drainAudio(128);
    writeHeader();
    file.close();
  }
  if (sdMounted && name[0] != '\0') {
    File err = SD.open(errorName, FILE_WRITE);
    if (err) { err.println(why); err.close(); }
  }
  state = State::Fault;
  Serial.printf("ERROR: %s\n", why);
}

bool startRecording() {
  name[0] = '\0';
  if (!mountSD()) { fault("microSD mount failed (check FAT32/card CS pin)"); return false; }
  if (!findNextName()) { fault("No available filenames REC0001..REC9999"); return false; }
  file = SD.open(name, FILE_WRITE);
  if (!file) { fault("Can't create WAV file"); return false; }
  audioBytes = 0;
  if (!writeHeader()) { fault("WAV header init failed"); return false; }
  xQueueReset(queue);
  captureOverflow.store(false, std::memory_order_release);
  if (i2s_start(I2S_NUM_0) != ESP_OK) { fault("Microphone failed to start"); return false; }
  startedAt = millis();
  checkpointAt = millis();
  state = State::Recording;
  captureEnabled.store(true, std::memory_order_release);
  Serial.printf("RECORDING %s\n", name);
  return true;
}

void stopRecording() {
  stopCapture();
  while (uxQueueMessagesWaiting(queue)) {
    if (!drainAudio(32)) { fault("microSD write failed while stopping"); return; }
  }
  if (captureOverflow.load()) { fault("Capture queue overflow, partial WAV preserved"); return; }
  if (!writeHeader()) { fault("Failed final WAV header"); return; }
  file.close();
  state = State::Saved;
  Serial.printf("SAVED %s (%lu bytes; %lus)\n", name,
    static_cast<unsigned long>(audioBytes), static_cast<unsigned long>(audioBytes / (kRate * 2)));
}

void checkButton() {
  static bool lastRaw = HIGH;
  static bool stable = HIGH;
  static uint32_t changed = 0;
  const bool raw = digitalRead(kButton);
  if (raw != lastRaw) { lastRaw = raw; changed = millis(); }
  if (millis() - changed < kDebounceMs || raw == stable) return;
  stable = raw;
  if (stable == LOW) {
    if (state == State::Recording) stopRecording();
    else if (queue) startRecording();
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kButton, INPUT_PULLUP);
  pinMode(kStatusLed, OUTPUT);
  digitalWrite(kStatusLed, LOW);
  // No Wi-Fi: record privately offline, minimize power use.
  queue = xQueueCreate(48, sizeof(AudioBlock)); // ~1.5 seconds of PCM buffering
  if (!queue) { state = State::Fault; Serial.println("Queue allocation failed"); return; }
  if (!initMic()) { state = State::Fault; Serial.println("PDM microphone init failed"); return; }
  if (xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 2, nullptr, 1) != pdPASS) {
    state = State::Fault; Serial.println("Capture task init failed"); return;
  }
  if (!mountSD()) {
    state = State::Fault;
    Serial.println("microSD not mounted; insert FAT32 card and press record to retry");
  } else {
    Serial.println("READY: D1-to-GND button toggles record. D0 LED indicates recording.");
  }
}

void loop() {
  checkButton();
  if (state == State::Recording) {
    if (captureOverflow.load()) {
      fault("Capture queue overflow");
    } else if (!drainAudio()) {
      fault("SD write failed");
    } else if (audioBytes >= kMaxSeconds * kRate * 2UL) {
      stopRecording();
    } else if (millis() - checkpointAt >= kCheckpointMs) {
      if (!writeHeader()) fault("WAV checkpoint failed");
      checkpointAt = millis();
    } else if (millis() - lastPrintAt >= 1000) {
      lastPrintAt = millis();
      Serial.printf("REC %lus | buffered=%u blocks\n",
                    static_cast<unsigned long>(audioBytes / (kRate * 2)),
                    static_cast<unsigned>(uxQueueMessagesWaiting(queue)));
    }
  }
  statusLED();
  delay(2);
}
