// M5StickC Plus v1.1 voice recorder. Wi-Fi streams 16 kHz mono PCM.
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>
#include "secrets.h"

static_assert(sizeof(VOICE_TOKEN) == 33, "VOICE_TOKEN must be exactly 32 ASCII chars");

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kSamplesPerBlock = 512;  // 32 ms
constexpr uint16_t kBookmark = 0xFFFF;
constexpr uint16_t kAbort = 0xFFFE;
constexpr uint32_t kNetworkTimeoutMs = 5000;

struct AudioBlock { int16_t samples[kSamplesPerBlock]; };
QueueHandle_t audioQueue = nullptr;
int16_t micBuffers[2][kSamplesPerBlock] = {};
std::atomic<bool> capturing { false };
std::atomic<bool> captureError { false };
std::atomic<uint16_t> latestPeak { 0 };
WiFiClient client;

enum class Mode { Ready, Recording, Processing, Complete, Error };
Mode mode = Mode::Ready;
String notice = "Press A to record";
uint32_t startedAt = 0;
uint32_t lastPaint = 0;
uint16_t bookmarkCount = 0;

void micReleased(void*, void* data, size_t count) {
  if (!capturing.load(std::memory_order_acquire)) return;
  if (count != kSamplesPerBlock) {
    captureError.store(true);
    return;
  }
  AudioBlock block;
  memcpy(block.samples, data, sizeof(block.samples));
  uint16_t peak = 0;
  for (size_t n = 0; n < count; n += 8) {
    int32_t amplitude = block.samples[n];
    if (amplitude < 0) amplitude = -amplitude;
    if (amplitude > peak) peak = static_cast<uint16_t>(min(amplitude, 32767L));
  }
  latestPeak.store(peak, std::memory_order_relaxed);
  if (xQueueSend(audioQueue, &block, 0) != pdTRUE) captureError.store(true);
  // Mic_Class explicitly permits requeuing this freed buffer from its callback.
  if (capturing.load(std::memory_order_acquire) &&
      !M5.Mic.record(static_cast<int16_t*>(data), kSamplesPerBlock)) {
    captureError.store(true);
  }
}

void draw(bool force = false) {
  if (!force && millis() - lastPaint < 200) return;
  lastPaint = millis();
  uint16_t color = TFT_CYAN;
  const char* label = "READY";
  if (mode == Mode::Recording) { color = TFT_RED; label = "RECORDING"; }
  if (mode == Mode::Processing) { color = TFT_YELLOW; label = "TRANSCRIBING"; }
  if (mode == Mode::Complete) { color = TFT_GREEN; label = "SAVED"; }
  if (mode == Mode::Error) { color = TFT_ORANGE; label = "ERROR"; }
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(9, 7);
  M5.Display.print(label);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(9, 38);
  M5.Display.printf("Mac: %s:%d", SERVER_HOST, SERVER_PORT);
  if (mode == Mode::Recording) {
    const uint32_t seconds = (millis() - startedAt) / 1000;
    M5.Display.setTextSize(2);
    M5.Display.setCursor(9, 59);
    M5.Display.printf("%02lu:%02lu", static_cast<unsigned long>(seconds / 60),
                      static_cast<unsigned long>(seconds % 60));
    M5.Display.setTextSize(1);
    M5.Display.setCursor(145, 65);
    M5.Display.printf("MARK %u", bookmarkCount);
    M5.Display.drawRect(9, 89, 221, 10, TFT_DARKGREY);
    const int meter = min(218, static_cast<int>((latestPeak.load() * 218UL) / 16000UL));
    M5.Display.fillRect(10, 90, meter, 8, TFT_GREEN);
    M5.Display.setCursor(9, 115);
    M5.Display.print("A: Stop       B: Bookmark");
  } else {
    M5.Display.setCursor(9, 68);
    M5.Display.print(notice.substring(0, 36));
    M5.Display.setCursor(9, 115);
    M5.Display.print("A: Start / retry");
  }
}

bool writeAll(const uint8_t* bytes, size_t size) {
  uint32_t deadline = millis() + kNetworkTimeoutMs;
  while (size > 0) {
    if (!client.connected() || static_cast<int32_t>(millis() - deadline) >= 0) return false;
    const size_t n = client.write(bytes, size);
    if (n) {
      bytes += n;
      size -= n;
      deadline = millis() + kNetworkTimeoutMs;
    } else {
      delay(2);
    }
  }
  return true;
}

bool writeLength(uint16_t count) {
  const uint8_t payload[2] = { static_cast<uint8_t>(count & 0xFF),
                                static_cast<uint8_t>(count >> 8) };
  return writeAll(payload, 2);
}

bool sendQueued(size_t limit = 8) {
  AudioBlock block;
  while (limit-- && xQueueReceive(audioQueue, &block, 0) == pdTRUE) {
    if (!writeLength(sizeof(block.samples)) ||
        !writeAll(reinterpret_cast<const uint8_t*>(block.samples), sizeof(block.samples))) {
      return false;
    }
  }
  return true;
}

void disconnectWithError(const String& error) {
  capturing.store(false);
  M5.Mic.end();
  if (client.connected()) writeLength(kAbort);
  client.stop();
  notice = error;
  mode = Mode::Error;
  draw(true);
  Serial.printf("Error: %s\n", error.c_str());
}

String readLine(uint32_t waitMs) {
  String response;
  const uint32_t started = millis();
  while (millis() - started < waitMs) {
    M5.update();
    draw();
    if (client.available()) {
      const char c = static_cast<char>(client.read());
      if (c == '\n') return response;
      if (c != '\r' && response.length() < 96) response += c;
    } else if (!client.connected()) {
      return "";
    }
    delay(10);
  }
  return "";
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  notice = "Connecting to Wi-Fi";
  draw(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    M5.update();
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool startSession() {
  if (!connectWiFi()) { disconnectWithError("Wi-Fi not connected"); return false; }
  notice = "Connecting to Mac";
  draw(true);
  client.stop();
  client.setTimeout(2);
  if (!client.connect(SERVER_HOST, SERVER_PORT, 5000)) {
    disconnectWithError("Mac unavailable"); return false;
  }
  uint8_t header[44] = {};
  memcpy(header, "M5VOICE1", 8);
  header[8] = kSampleRate & 0xFF;
  header[9] = (kSampleRate >> 8) & 0xFF;
  header[10] = (kSampleRate >> 16) & 0xFF;
  header[11] = (kSampleRate >> 24) & 0xFF;
  memcpy(header + 12, VOICE_TOKEN, 32);
  if (!writeAll(header, sizeof(header)) || readLine(6000) != "READY") {
    disconnectWithError("Server rejected connection"); return false;
  }
  xQueueReset(audioQueue);
  captureError.store(false);
  latestPeak.store(0);
  M5.Speaker.end();
  M5.Mic.setBufferReleaseCallback(nullptr, micReleased);
  if (!M5.Mic.begin()) {
    disconnectWithError("Microphone failed"); return false;
  }
  capturing.store(true, std::memory_order_release);
  const bool a = M5.Mic.record(micBuffers[0], kSamplesPerBlock, kSampleRate);
  const bool b = a && M5.Mic.record(micBuffers[1], kSamplesPerBlock, kSampleRate);
  if (!a || !b) {
    disconnectWithError("Mic recording failed"); return false;
  }
  bookmarkCount = 0;
  startedAt = millis();
  mode = Mode::Recording;
  notice = "Recording";
  draw(true);
  return true;
}

void stopSession() {
  capturing.store(false, std::memory_order_release);
  M5.Mic.end();
  if (captureError.load()) { disconnectWithError("Mic buffer overflow"); return; }
  while (uxQueueMessagesWaiting(audioQueue)) {
    if (!sendQueued()) { disconnectWithError("Network lost"); return; }
  }
  if (!writeLength(0)) { disconnectWithError("Network lost on save"); return; }
  mode = Mode::Processing;
  notice = "Saving + transcribing";
  draw(true);
  String response = readLine(10000);
  if (response != "TRANSCRIBING") {
    disconnectWithError("No acknowledgement"); return;
  }
  response = readLine(600000);
  client.stop();
  if (response.startsWith("DONE ")) {
    mode = Mode::Complete;
    notice = "Transcript on Mac";
  } else if (response.startsWith("SAVED_AUDIO")) {
    mode = Mode::Error;
    notice = "WAV saved; Whisper failed";
  } else {
    mode = Mode::Error;
    notice = "Check Mac recordings";
  }
  draw(true);
}

void addBookmark() {
  while (uxQueueMessagesWaiting(audioQueue)) {
    if (!sendQueued()) { disconnectWithError("Network lost"); return; }
  }
  if (!writeLength(kBookmark)) { disconnectWithError("Bookmark failed"); return; }
  ++bookmarkCount;
  draw(true);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(90);
  audioQueue = xQueueCreate(64, sizeof(AudioBlock));
  if (!audioQueue) {
    mode = Mode::Error;
    notice = "No queue memory";
  }
  draw(true);
}

void loop() {
  M5.update();
  if (mode == Mode::Recording) {
    if (captureError.load() || WiFi.status() != WL_CONNECTED || !sendQueued()) {
      disconnectWithError("Stream lost / overflow");
    } else if (M5.BtnA.wasClicked()) {
      stopSession();
    } else if (M5.BtnB.wasClicked()) {
      addBookmark();
    }
  } else if (audioQueue && mode != Mode::Processing && M5.BtnA.wasClicked()) {
    startSession();
  }
  draw();
  delay(5);
}
