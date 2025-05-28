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
#include "espressif_esp_websocket_client/esp_websocket_client.h"
#include "iot/iot_manager.h"
#include "messaging/message_queue.h"

struct button_dev_t;
class AudioInputEngine;
class AudioOutputEngine;

namespace ai_vox {

class EngineImpl : public Engine {
 public:
  static EngineImpl &GetInstance();
  EngineImpl();
  ~EngineImpl();
  void SetObserver(std::shared_ptr<Observer> observer) override;
  void SetTrigger(const gpio_num_t gpio) override;
  void SetOtaUrl(const std::string url) override;
  void ConfigWebsocket(const std::string url, const std::map<std::string, std::string> headers) override;
  void RegisterIotEntity(std::shared_ptr<iot::Entity> entity) override;
  void Start(std::shared_ptr<AudioInputDevice> audio_input_device, std::shared_ptr<AudioOutputDevice> audio_output_device) override;

 private:
  enum class State {
    kIdle,
    kInited,
    kLoadingProtocol,
    kWebsocketConnecting,
    kWebsocketConnected,
    kStandby,
    kListening,
    kSpeaking,
  };

  enum class MessageType : uint8_t {
    kOnButtonClick,
    kOnWebsocketConnected,
    kOnWebsocketDisconnected,
    kOnWebsocketEventData,
    kOnWebsocketFinish,
    kOnOutputDataComsumed,
  };

  // using Message = Message<MessageType>;
  // using MessageQueue = MessageQueue<MessageType>;

  static void Loop(void *self);
  static void OnButtonClick(void *button_handle, void *usr_data);
  static void OnWebsocketEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

  void Loop();
  void OnButtonClick();
  void OnWebsocketEvent(esp_event_base_t base, int32_t event_id, void *event_data);
  void OnWebSocketEventData(const uint8_t op_code, std::shared_ptr<std::vector<uint8_t>> &&data);
  void OnJsonData(std::vector<uint8_t> &&data);
  void OnWebSocketConnected();
  void OnAudioOutputDataConsumed();

  void LoadProtocol();
  void StartListening();
  void AbortSpeaking();
  bool ConnectWebSocket();
  void DisconnectWebSocket();
  void SendIotDescriptions();
  void SendIotUpdatedStates(const bool force);
  void ChangeState(const State new_state);

  mutable std::mutex mutex_;
  MessageQueue<MessageType> message_queue_;
  State state_ = State::kIdle;
  button_dev_t *button_handle_ = nullptr;
  gpio_num_t trigger_pin_ = GPIO_NUM_0;
  std::vector<uint8_t> recving_websocket_data_;
  std::shared_ptr<AudioInputDevice> audio_input_device_;
  std::shared_ptr<AudioOutputDevice> audio_output_device_;
  std::shared_ptr<Observer> observer_;
  ai_vox::iot::Manager iot_manager_;
  esp_websocket_client_handle_t web_socket_client_ = nullptr;
  std::string uuid_;
  std::string session_id_;
  std::shared_ptr<AudioInputEngine> audio_input_engine_;
  std::shared_ptr<AudioOutputEngine> audio_output_engine_;
  std::string ota_url_;
  std::string websocket_url_;
  std::map<std::string, std::string> websocket_headers_;
};
}  // namespace ai_vox

#endif