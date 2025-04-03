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
#include "messaging/message.h"
#include "messaging/message_queue.h"

class OpusDecoder;
class AudioOutputEngine {
 public:
  enum class Event : uint8_t {
    kOnDataComsumed = 1 << 0,
  };

  using EventHandler = std::function<void(Event)>;

  AudioOutputEngine(const EventHandler& handler);
  ~AudioOutputEngine();

  void Open(std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device);
  void Close();
  void Write(std::vector<uint8_t>&& data);
  void NotifyDataEnd();

 private:
  enum class State : uint8_t {
    kIdle,
    kRunning,
  };

  enum class MessageType : uint8_t {
    kOpen,
    kClose,
    kData,
    kDataEnd,
  };

  using Message = Message<MessageType>;
  using MessageQueue = MessageQueue<MessageType>;

  static void Loop(void* self);
  void Loop();

  std::mutex mutex_;
  State state_ = State::kIdle;
  struct OpusDecoder* opus_decoder_ = nullptr;
  uint32_t frame_size_ = 0;
  EventHandler handler_;
  MessageQueue message_queue_;
};