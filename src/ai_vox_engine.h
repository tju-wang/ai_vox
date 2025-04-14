#pragma once

#ifndef _AI_VOX_ENGINE_H_
#define _AI_VOX_ENGINE_H_

#include <driver/gpio.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "audio_input_device.h"
#include "audio_output_device.h"

namespace ai_vox {

class Observer;

class Engine {
 public:
  enum class State {
    kIdle,
    kInited,
    kMqttConnecting,
    kMqttConnected,
    kAudioSessionOpening,
    kListening,
    kSpeaking,
  };

  enum class Role : uint8_t {
    kAssistant,
    kUser,
  };

  static Engine& GetInstance();
  Engine() = default;
  virtual ~Engine() = default;
  virtual void SetObserver(std::shared_ptr<Observer> observer) = 0;
  virtual void SetTrigger(const gpio_num_t gpio) = 0;
  virtual void Start(std::shared_ptr<AudioInputDevice> audio_input_device, std::shared_ptr<AudioOutputDevice> audio_output_device) = 0;
  virtual State state() const = 0;

 private:
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
};

}  // namespace ai_vox

#endif