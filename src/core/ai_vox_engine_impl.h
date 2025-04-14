#pragma once

#ifndef _AI_VOX_ENGINE_IMPL_H_
#define _AI_VOX_ENGINE_IMPL_H_

#include <esp_event_base.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include <condition_variable>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ai_vox_engine.h"
#include "audio_session.h"
#include "messaging/message_queue.h"

struct button_dev_t;

namespace ai_vox {

class EngineImpl : public Engine {
 public:
  static EngineImpl &GetInstance();
  EngineImpl();
  ~EngineImpl();
  void SetObserver(std::shared_ptr<Observer> observer) override;
  void SetTrigger(const gpio_num_t gpio) override;
  void Start(std::shared_ptr<AudioInputDevice> audio_input_device, std::shared_ptr<AudioOutputDevice> audio_output_device) override;
  State state() const override;

 private:
  enum class MessageType : uint8_t {
    kOnButtonClick,
    kOnEncodedAudioData,
    kOnSpeakingStart,
    kOnSpeakingStop,
    kOnMqttConnected,
    kOnMqttDisconnected,
    kOnMqttEventData,
    kOnOutputDataComsumed,
  };

  using Message = Message<MessageType>;
  using MessageQueue = MessageQueue<MessageType>;

  static void Loop(void *self);
  static void OnButtonClick(void *button_handle, void *usr_data);
  static void OnMqttEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

  void Loop();
  void OnButtonClick();
  void OnMqttEvent(esp_event_base_t base, int32_t event_id, void *event_data);
  void OnMqttData(const std::string &message);

  void OpenAudioSession();
  void CloseAudioSession(const std::string &session_id);
  void CloseAudioSession();
  void AbortSpeaking();
  void StartAudioTransmission();
  bool ConnectMqtt();
  void ChangeState(State new_state);

  mutable std::mutex mutex_;
  MessageQueue message_queue_;
  State state_ = State::kIdle;
  button_dev_t *button_handle_ = nullptr;
  esp_mqtt_client_handle_t mqtt_client_ = nullptr;
  gpio_num_t trigger_pin_ = GPIO_NUM_0;
  std::string mqtt_publish_topic_;
  std::string mqtt_subscribe_topic_;
  std::map<std::string, std::string> mqtt_event_data_;
  std::shared_ptr<AudioSession> audio_session_;
  std::shared_ptr<AudioInputDevice> audio_input_device_;
  std::shared_ptr<AudioOutputDevice> audio_output_device_;
  std::shared_ptr<Observer> observer_;
};
}  // namespace ai_vox

#endif