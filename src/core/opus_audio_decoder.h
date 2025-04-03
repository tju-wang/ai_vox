#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <functional>
#include <list>
#include <vector>

struct OpusDecoder;

class OpusAudioDecoder {
 public:
  OpusAudioDecoder(const uint32_t frame_rate,
                   const uint8_t channels,
                   const uint32_t duration_ms,
                   const std::function<void(std::vector<int16_t>&&)>& handler);
  ~OpusAudioDecoder();
  void Write(std::vector<uint8_t>&& data);
  void WaitForDataComsumed();
  void Ablort();
  void WaitAbort();

 private:
  static void Loop(void* self);
  void Loop();
  void ClearQueue();

  struct OpusDecoder* opus_decoder_ = nullptr;
  std::function<void(std::vector<int16_t>&&)> handler_;
  uint32_t frame_size_ = 0;
  QueueHandle_t message_queue_handle_ = nullptr;
  EventGroupHandle_t event_group_ = nullptr;
};