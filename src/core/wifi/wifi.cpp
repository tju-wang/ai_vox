#include "wifi.h"

#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "../clogger/clogger.h"

namespace {
enum {
  kWifiStationStated = 1 << 0,
};
}

Wifi* Wifi::s_instance_ = nullptr;
std::once_flag Wifi::s_once_flag_;

Wifi& Wifi::GetInstance() {
  std::call_once(s_once_flag_, []() { s_instance_ = new Wifi(); });
  return *s_instance_;
}

Wifi::Wifi() {
  CLOG_TRACE();
  ESP_ERROR_CHECK(esp_netif_init());
  esp_event_loop_create_default();
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &IpEventHandler, this));
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

void Wifi::SetEventHandler(const std::function<EventHandler>& event_handler) {
  std::lock_guard lock(mutex_);
  event_handler_ = event_handler;
}

void Wifi::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  reinterpret_cast<Wifi*>(arg)->WifiEventHandler(event_base, event_id, event_data);
}

void Wifi::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  reinterpret_cast<Wifi*>(arg)->IpEventHandler(event_base, event_id, event_data);
}

void Wifi::WifiEventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data) {
  switch (event_id) {
    case WIFI_EVENT_STA_START: {
      CLOG("WIFI_EVENT_STA_START");
      //   xEventGroupSetBits(event_group_, kWifiStationStated);
      ESP_ERROR_CHECK(esp_wifi_connect());
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      CLOG("WIFI_EVENT_STA_DISCONNECTED");
      std::lock_guard lock(mutex_);
      state_ = State::kIdle;
      if (event_handler_) {
        event_handler_(Event::kDisconnected);
      }
      break;
    }
    default: {
      break;
    }
  }
}

void Wifi::IpEventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ip_event_got_ip_t* event;
  switch (event_id) {
    case IP_EVENT_STA_GOT_IP: {
      event = (ip_event_got_ip_t*)event_data;
      CLOG("IP_EVENT_STA_GOT_IP");
      CLOG("got ip:" IPSTR, IP2STR(&event->ip_info.ip));
      std::lock_guard lock(mutex_);
      state_ = State::kConnected;
      if (event_handler_) {
        event_handler_(Event::kConnected);
      }
      break;
    }
    default: {
      break;
    }
  }
}

void Wifi::Connect(const std::string& ssid, const std::string& password) {
  std::lock_guard lock(mutex_);

  if (state_ != State::kIdle) {
    return;
  }

  wifi_config_t wifi_config;
  memset(&wifi_config, 0, sizeof(wifi_config));
  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
  strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password.c_str(), sizeof(wifi_config.sta.password) - 1);
  CLOG("ssid: %s", wifi_config.sta.ssid);
  CLOG("password: %s", wifi_config.sta.password);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  state_ = State::kConnecting;
  CLOG("wifi connecting");
}
