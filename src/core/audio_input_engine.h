#pragma once

#ifndef _AUDIO_INPUT_ENGINE_H_
#define _AUDIO_INPUT_ENGINE_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <vector>

#include "../audio_input_device.h"
#include "audio_session.h"
#include "messaging/message.h"
#include "messaging/message_queue.h"

struct OpusDecoder;
class AudioInputEngine {
 public:
  using DataHandler = std::function<void(std::vector<uint8_t> &&)>;
  AudioInputEngine(std::shared_ptr<ai_vox::AudioInputDevice> audio_input_device, const AudioInputEngine::DataHandler &handler);
  ~AudioInputEngine();

 private:
  static void Loop(void *self);
  void Loop();

  enum class MessageType : uint8_t {
    kClose,
    kProcessNext,
  };

  using Message = Message<MessageType>;
  using MessageQueue = MessageQueue<MessageType>;

  DataHandler const handler_;
  std::shared_ptr<ai_vox::AudioInputDevice> audio_input_device_;
  std::mutex mutex_;
  std::condition_variable cv_;
  MessageQueue message_queue_;
  struct OpusEncoder *opus_encoder_ = nullptr;
};

#endif