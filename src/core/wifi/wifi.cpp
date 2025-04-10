#include "wifi.h"

#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "../clogger/clogger.h"

namespace {
enum Status : EventBits_t {
  kStationStarted = 1 << 0,
  kWifiConnected = 1 << 1,
};
}  // namespace

Wifi& Wifi::GetInstance() {
  static std::once_flag s_once_flag;
  Wifi* s_instance = nullptr;
  std::call_once(s_once_flag, [&s_instance]() { s_instance = new Wifi(); });
  return *s_instance;
}

Wifi::Wifi() : event_group_(xEventGroupCreate()) {
  CLOG_TRACE();
  ESP_ERROR_CHECK(esp_netif_init());
  esp_event_loop_create_default();
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &IpEventHandler, this));
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  cfg.static_tx_buf_num = 0;
  cfg.dynamic_tx_buf_num = 32;
  cfg.tx_buf_type = 1;

  cfg.static_rx_buf_num = 2;
  cfg.dynamic_rx_buf_num = 32;

  cfg.cache_tx_buf_num = 4;

  cfg.rx_mgmt_buf_type = 1;
  cfg.rx_mgmt_buf_num = 0;
  cfg.mgmt_sbuf_num = 10;

  cfg.nano_enable = 1;

  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

void Wifi::SetEventHandler(const std::function<EventHandler>& event_handler) {
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
      xEventGroupSetBits(event_group_, kStationStarted);
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      CLOG("WIFI_EVENT_STA_DISCONNECTED");
      xEventGroupClearBits(event_group_, kWifiConnected);
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
  if (0 != (xEventGroupGetBits(event_group_) && kStationStarted)) {
    CLOG("wifi sta already started");
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
  xEventGroupWaitBits(event_group_, kStationStarted, pdFALSE, pdFALSE, portMAX_DELAY);
  ESP_ERROR_CHECK(esp_wifi_connect());
  CLOG("wifi connecting");
}

bool Wifi::Reconnect() {
  if (0 == (xEventGroupGetBits(event_group_) && kStationStarted)) {
    CLOG("wifi sta not started");
    return false;
  }

  ESP_ERROR_CHECK(esp_wifi_disconnect());
  ESP_ERROR_CHECK(esp_wifi_connect());
  CLOG("wifi reconnecting");
  return true;
}