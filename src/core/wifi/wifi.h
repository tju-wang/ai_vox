
#pragma once

#ifndef _AI_VOX_WIFI_H_
#define _AI_VOX_WIFI_H_

#include <esp_wifi.h>

#include <functional>
#include <mutex>
#include <string>

class Wifi {
 public:
  enum class Event : uint8_t {
    kConnected,
    kDisconnected,
  };

  using EventHandler = void(Event event);

  static Wifi& GetInstance();
  void SetEventHandler(const std::function<EventHandler>& event_handler);
  void Connect(const std::string& ssid, const std::string& password);

 private:
  enum class State : uint8_t {
    kIdle,
    kConnecting,
    kConnected,
  };

  Wifi();
  static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
  static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

  void WifiEventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data);
  void IpEventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data);

  static Wifi* s_instance_;
  static std::once_flag s_once_flag_;
  std::mutex mutex_;
  State state_ = State::kIdle;
  std::function<EventHandler> event_handler_;
};

#endif