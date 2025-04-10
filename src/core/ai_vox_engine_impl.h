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
#include "wifi/wifi.h"

class Display;
struct button_dev_t;

namespace ai_vox {

class EngineImpl : public Engine {
 public:
  static EngineImpl &GetInstance();
  EngineImpl();
  ~EngineImpl();
  void SetWifi(std::string ssid, std::string password) override;
  void SetTrigger(const gpio_num_t gpio) override;
  void InitDisplay(esp_lcd_panel_io_handle_t lcd_panel_io,
                   esp_lcd_panel_handle_t lcd_panel,
                   uint32_t width,
                   uint32_t height,
                   bool mirror_x,
                   bool mirror_y) override;
  void Start(std::shared_ptr<AudioInputDevice> audio_input_device, std::shared_ptr<AudioOutputDevice> audio_output_device) override;

 private:
  enum class State {
    kIdle,
    kNetworkConnecting,
    kNetworkConnected,
    kMqttConnecting,
    kMqttConnected,
    kAudioSessionOpening,
    kListening,
    kSpeaking,
  };

  enum class MessageType : uint8_t {
    kRequestExit,
    kOnWifiConnected,
    kOnWifiDisconnected,
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
  void OnWifiEvent(const Wifi::Event event);
  void OnWifiConnected();
  void OnWifiDisconnected();

  void OpenAudioSession();
  void CloseAudioSession(const std::string &session_id);
  void CloseAudioSession();
  void AbortSpeaking();
  void StartAudioTransmission();
  bool ConnectMqtt();

  std::mutex mutex_;
  MessageQueue message_queue_;
  State state_ = State::kIdle;
  button_dev_t *button_handle_ = nullptr;
  esp_mqtt_client_handle_t mqtt_client_ = nullptr;
  gpio_num_t trigger_pin_ = GPIO_NUM_0;
  std::string mqtt_publish_topic_;
  std::string mqtt_subscribe_topic_;

  std::map<std::string, std::string> mqtt_event_data_;
  std::shared_ptr<AudioSession> audio_session_;
  std::shared_ptr<Display> display_;

  std::optional<std::string> wifi_ssid_;
  std::optional<std::string> wifi_password_;
  std::optional<esp_lcd_panel_io_handle_t> lcd_panel_io_;
  std::optional<esp_lcd_panel_handle_t> lcd_panel_;
  std::shared_ptr<AudioInputDevice> audio_input_device_;
  std::shared_ptr<AudioOutputDevice> audio_output_device_;
};
}  // namespace ai_vox

#endif