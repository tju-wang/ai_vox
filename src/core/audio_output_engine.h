#pragma once

#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <thread>
#include <vector>

#include "../audio_output_device.h"
#include "task_queue/task_queue.h"

class OpusDecoder;
class AudioOutputEngine {
 public:
  AudioOutputEngine(std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device);
  ~AudioOutputEngine();

  void Write(std::vector<uint8_t>&& data);
  void NotifyDataEnd(std::function<void()>&& callback);

 private:
  static void Loop(void* self);
  void Loop();
  void ProcessData(std::vector<uint8_t>&& data);

  std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device_;
  struct OpusDecoder* opus_decoder_ = nullptr;
  TaskQueue* task_queue_ = nullptr;
};