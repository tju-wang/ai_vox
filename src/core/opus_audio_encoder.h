#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>

struct OpusEncoder;

class OpusAudioEncoder {
 public:
  OpusAudioEncoder();
  ~OpusAudioEncoder();
  void Start(const uint32_t frame_rate, const uint8_t channels, const std::function<void(std::vector<uint8_t>&&)>& handler);
  void Stop();
  bool QueueFrameFromISR(std::vector<int16_t>&& frame);

 private:
  static void Loop(void* self);
  void Loop();
  struct OpusEncoder* opus_encoder_ = nullptr;
  std::function<void(std::vector<uint8_t>&&)> handler_;
  QueueHandle_t message_queue_handle_ = nullptr;
  TaskHandle_t loop_handle_ = nullptr;
};