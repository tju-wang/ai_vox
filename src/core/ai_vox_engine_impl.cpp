#include "ai_vox_engine_impl.h"

#ifdef ARDUINO
#include "espressif_button/button_gpio.h"
#include "espressif_button/iot_button.h"
#include "espressif_esp_lvgl_port/esp_lvgl_port.h"
#else
#include <button_gpio.h>
#include <esp_lvgl_port.h>
#include <iot_button.h>
#endif

#include <cJSON.h>
#include <driver/i2c_master.h>
#include <esp_crt_bundle.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include "clogger/clogger.h"
// #include "display/lcd_display.h"
#include "display/oled_display.h"
#include "fetch_config.h"
#include "wifi/wifi.h"

LV_FONT_DECLARE(font_puhui_14_1);
// LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_14_1);
// LV_FONT_DECLARE(font_awesome_16_4);

namespace ai_vox {

EngineImpl &EngineImpl::GetInstance() {
  static std::once_flag s_once_flag;
  static EngineImpl *s_instance = nullptr;
  std::call_once(s_once_flag, [&s_instance]() { s_instance = new EngineImpl; });
  return *s_instance;
}

EngineImpl::EngineImpl() {
  CLOG_TRACE();
}

EngineImpl::~EngineImpl() {
  CLOG_TRACE();
  // TODO
}

void EngineImpl::SetWifi(std::string ssid, std::string password) {
  std::lock_guard lock(mutex_);
  if (state_ != State::kIdle) {
    return;
  }

  wifi_ssid_ = std::move(ssid);
  wifi_password_ = std::move(password);
}

void EngineImpl::SetTrigger(const gpio_num_t gpio) {
  std::lock_guard lock(mutex_);
  if (state_ != State::kIdle) {
    return;
  }

  trigger_pin_ = gpio;
}

void EngineImpl::InitDisplay(
    esp_lcd_panel_io_handle_t lcd_panel_io, esp_lcd_panel_handle_t lcd_panel, uint32_t width, uint32_t height, bool mirror_x, bool mirror_y) {
  std::lock_guard lock(mutex_);
  if (state_ != State::kIdle) {
    return;
  }
  display_ =
      std::make_shared<OledDisplay>(lcd_panel_io, lcd_panel, width, height, mirror_x, mirror_y, DisplayFonts{&font_puhui_14_1, &font_awesome_14_1});
}

void EngineImpl::Start(std::shared_ptr<AudioInputDevice> audio_input_device, std::shared_ptr<AudioOutputDevice> audio_output_device) {
  CLOG_TRACE();
  std::lock_guard lock(mutex_);
  if (state_ != State::kIdle) {
    return;
  }

  audio_input_device_ = std::move(audio_input_device);
  audio_output_device_ = std::move(audio_output_device);

  button_config_t btn_cfg = {
      .long_press_time = 1000,
      .short_press_time = 50,
  };

  button_gpio_config_t gpio_cfg = {
      .gpio_num = trigger_pin_,
      .active_level = 0,
      .enable_power_save = true,
      .disable_pull = false,
  };

  ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &button_handle_));
  ESP_ERROR_CHECK(iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, nullptr, OnButtonClick, this));

  if (wifi_ssid_ && wifi_password_) {
    state_ = State::kNetworkConnecting;
    auto &wifi = Wifi::GetInstance();
    wifi.SetEventHandler(std::bind(&EngineImpl::OnWifiEvent, this, std::placeholders::_1));
    CLOG("wifi ssid: %s", wifi_ssid_->c_str());
    CLOG("wifi password: %s", wifi_password_->c_str());
    wifi.Connect(*wifi_ssid_, *wifi_password_);
    if (display_) {
      display_->SetStatus("Wifi Connecting");
    }
  } else {
    state_ = State::kNetworkConnected;
    ConnectMqtt();
  }

  auto ret = xTaskCreate(Loop, "AiVoxMain", 1024 * 4, this, tskIDLE_PRIORITY + 1, nullptr);
  assert(ret == pdPASS);
}

void EngineImpl::OnButtonClick() {
  message_queue_.Send(MessageType::kOnButtonClick);
}

void EngineImpl::OnButtonClick(void *button_handle, void *self) {
  reinterpret_cast<EngineImpl *>(self)->OnButtonClick();
}

void EngineImpl::Loop(void *self) {
  reinterpret_cast<EngineImpl *>(self)->Loop();
  vTaskDelete(nullptr);
}

void EngineImpl::OnMqttEvent(void *self, esp_event_base_t base, int32_t event_id, void *event_data) {
  reinterpret_cast<EngineImpl *>(self)->OnMqttEvent(base, event_id, event_data);
}

void EngineImpl::OnMqttEvent(esp_event_base_t base, int32_t event_id, void *event_data) {
  auto event = (esp_mqtt_event_t *)event_data;
  switch (event_id) {
    case MQTT_EVENT_CONNECTED: {
      // CLOG("MQTT_EVENT_CONNECTED");
      message_queue_.Send(MessageType::kOnMqttConnected);
      break;
    }
    case MQTT_EVENT_DISCONNECTED: {
      // CLOG("MQTT_EVENT_DISCONNECTED");
      message_queue_.Send(MessageType::kOnMqttDisconnected);
      break;
    }
    case MQTT_EVENT_DATA: {
      // CLOG("MQTT_EVENT_DATA");
      // CLOG("topic: %s", std::string(event->topic, event->topic_len).c_str());
      // CLOG("payload: %s", std::string(event->data, event->data_len).c_str());
      // CLOG("data_len: %d", event->data_len);
      // CLOG("total_data_len: %d", event->total_data_len);
      // CLOG("current_data_offset: %d", event->current_data_offset);

      if (event->data_len == event->total_data_len && event->current_data_offset == 0) {
        Message message(MessageType::kOnMqttEventData);
        message.Write(std::string(event->data, event->data_len));
        message_queue_.Send(std::move(message));
      } else if (event->current_data_offset == 0) {
        mqtt_event_data_.insert(std::make_pair(std::string(event->topic, event->topic_len), std::string(event->data, event->data_len)));
      } else {
        auto it = mqtt_event_data_.find(std::string(event->topic, event->topic_len));
        if (it == mqtt_event_data_.end()) {
          break;
        }

        if (it->second.size() != event->current_data_offset) {
          mqtt_event_data_.erase(it);
          break;
        }

        it->second.append(event->data, event->data_len);
        if (it->second.size() == event->total_data_len) {
          Message message(MessageType::kOnMqttEventData);
          message.Write(std::move(it->second));
          message_queue_.Send(std::move(message));
          mqtt_event_data_.erase(it);
        }
      }
      break;
    }
    case MQTT_EVENT_BEFORE_CONNECT: {
      break;
    }
    case MQTT_EVENT_SUBSCRIBED: {
      break;
    }
    case MQTT_EVENT_ERROR: {
      break;
    }
    default: {
      break;
    }
  }
}

void EngineImpl::OnMqttData(const std::string &message) {
  auto *const root = cJSON_Parse(message.c_str());
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return;
  }

  std::string type;
  auto *type_json = cJSON_GetObjectItem(root, "type");
  if (cJSON_IsString(type_json)) {
    type = type_json->valuestring;
  } else {
    cJSON_Delete(root);
    return;
  }

  auto session_id_json = cJSON_GetObjectItem(root, "session_id");
  std::string session_id;
  if (cJSON_IsString(session_id_json)) {
    session_id = session_id_json->valuestring;
  }

  if (type == "hello") {
    if (state_ != State::kAudioSessionOpening) {
      cJSON_Delete(root);
      return;
    }

    auto udp_json = cJSON_GetObjectItem(root, "udp");
    if (!cJSON_IsObject(udp_json)) {
      return;
    }

    auto server_json = cJSON_GetObjectItem(udp_json, "server");
    std::string udp_server;
    if (cJSON_IsString(server_json)) {
      udp_server = server_json->valuestring;
    }

    auto udp_port_json = cJSON_GetObjectItem(udp_json, "port");
    uint16_t udp_port = 0;
    if (cJSON_IsNumber(udp_port_json)) {
      udp_port = udp_port_json->valueint;
    }

    auto aes_key_json = cJSON_GetObjectItem(udp_json, "key");
    std::string aes_key;
    if (cJSON_IsString(aes_key_json)) {
      aes_key = aes_key_json->valuestring;
    }

    auto aes_nonce_json = cJSON_GetObjectItem(udp_json, "nonce");
    std::string aes_nonce;
    if (cJSON_IsString(aes_nonce_json)) {
      aes_nonce = aes_nonce_json->valuestring;
    }

    CLOG("udp_server: %s", udp_server.c_str());
    CLOG("udp_port: %u", udp_port);
    CLOG("aes_key: %s", aes_key.c_str());
    CLOG("aes_nonce: %s", aes_nonce.c_str());
    CLOG("session_id: %s", session_id.c_str());

    audio_session_ = std::make_shared<AudioSession>(udp_server, udp_port, aes_key, aes_nonce, session_id, [this](AudioSession::Event event) {
      if (event == AudioSession::Event::kOnOutputDataComsumed) {
        CLOG("kOnOutputDataComsumed");
        message_queue_.Send(MessageType::kOnOutputDataComsumed);
      }
    });

    if (audio_session_->Open()) {
      StartAudioTransmission();
      state_ = State::kListening;
    }
  } else if (type == "goodbye") {
    CloseAudioSession(session_id);
  } else if (type == "tts") {
    auto *state_json = cJSON_GetObjectItem(root, "state");
    if (cJSON_IsString(state_json)) {
      if (strcmp("start", state_json->valuestring) == 0) {
        CLOG("tts start");
        if (audio_session_) {
          audio_session_->CloseAudioInput();
          audio_session_->OpenAudioOutput(audio_output_device_);
          state_ = State::kSpeaking;
          if (display_) {
            display_->SetStatus("说话中...");
          }
        }
      } else if (strcmp("stop", state_json->valuestring) == 0) {
        CLOG("tts stop");
        CLOG("current session: %s", audio_session_->session_id().c_str());
        if (audio_session_) {
          audio_session_->NotifyOutputDataEnd();
        }
      } else if (strcmp("sentence_start", state_json->valuestring) == 0) {
        auto text = cJSON_GetObjectItem(root, "text");
        if (text != nullptr) {
          CLOG("<< %s", text->valuestring);
          if (display_) {
            display_->SetChatMessage("assistant", text->valuestring);
          }
        }
      } else if (strcmp("sentence_end", state_json->valuestring) == 0) {
        // TODO:
      }
    }
  } else if (type == "stt") {
    auto text = cJSON_GetObjectItem(root, "text");
    if (text != nullptr) {
      CLOG(">> %s", text->valuestring);
      if (display_) {
        display_->SetChatMessage("user", text->valuestring);
      }
    }
  } else if (type == "llm") {
    auto emotion = cJSON_GetObjectItem(root, "emotion");
    if (cJSON_IsString(emotion)) {
      if (display_) {
        display_->SetEmotion(emotion->valuestring);
      }
    }
  }

  cJSON_Delete(root);
}

void EngineImpl::OnWifiEvent(const Wifi::Event event) {
  switch (event) {
    case Wifi::Event::kConnected: {
      CLOG("connceted");
      message_queue_.Send(MessageType::kOnWifiConnected);
      CLOG("done");
      break;
    }
    case Wifi::Event::kDisconnected: {
      CLOG("disconnected");
      message_queue_.Send(MessageType::kOnWifiDisconnected);
      break;
    }
  }
}

void EngineImpl::OnWifiConnected() {
  CLOG_TRACE();
  if (state_ != State::kNetworkConnecting) {
    return;
  }
  state_ = State::kNetworkConnected;
  ConnectMqtt();
}

void EngineImpl::OnWifiDisconnected() {
  CLOG_TRACE();
  audio_session_.reset();
  if (mqtt_client_ != nullptr) {
    esp_mqtt_client_stop(mqtt_client_);
    esp_mqtt_client_destroy(mqtt_client_);
    mqtt_client_ = nullptr;
  }

  state_ = State::kNetworkConnecting;
  Wifi::GetInstance().Reconnect();
  if (display_) {
    display_->SetStatus("Wifi Connecting");
  }
}

void EngineImpl::Loop() {
loop_start:
  auto message = message_queue_.Recevie();
  if (!message.has_value()) {
    goto loop_start;
  }
  switch (message->type()) {
    case MessageType::kOnButtonClick: {
      CLOG("kOnButtonClick");
      if (state_ == State::kNetworkConnected) {
        ConnectMqtt();
      } else if (state_ == State::kMqttConnected) {
        OpenAudioSession();
      } else if (state_ == State::kListening) {
        CloseAudioSession();
      } else if (state_ == State::kSpeaking) {
        AbortSpeaking();
      }
      break;
    }
    case MessageType::kRequestExit: {
      CLOG("kRequestExit");
      return;
    }
    case MessageType::kOnSpeakingStart: {
      // CloseAudioInput();
      break;
    }
    case MessageType::kOnSpeakingStop: {
      // if (state_ == State::kListening) {
      //   OpenAudioInput();
      // }
      break;
    }
    case MessageType::kOnEncodedAudioData: {
      break;
    }
    case MessageType::kOnMqttConnected: {
      CLOG("kOnMqttConnected");
      state_ = State::kMqttConnected;
      if (display_) {
        display_->SetStatus("待命");
        display_->SetEmotion("neutral");
        display_->SetChatMessage("system", "");
      }
      break;
    }
    case MessageType::kOnMqttDisconnected: {
      CLOG("kOnMqttDisconnected");
      audio_session_.reset();
      if (mqtt_client_ != nullptr) {
        esp_mqtt_client_reconnect(mqtt_client_);
        state_ = State::kMqttConnecting;
        if (display_) {
          display_->SetStatus("Mqtt Connecting");
          display_->SetEmotion("neutral");
          display_->SetChatMessage("system", "");
        }
      } else {
        state_ = State::kNetworkConnected;
        ConnectMqtt();
      }
      break;
    }
    case MessageType::kOnMqttEventData: {
      // CLOG("kOnMqttEventData");
      auto data = message->Read<std::string>();
      if (data) {
        OnMqttData(*data);
      }
      break;
    }
    case MessageType::kOnOutputDataComsumed: {
      CLOG("kOnOutputDataComsumed");
      if (state_ == State::kSpeaking) {
        StartAudioTransmission();
        state_ = State::kListening;
      }
      break;
    }
    case MessageType::kOnWifiConnected: {
      OnWifiConnected();
      break;
    }
    case MessageType::kOnWifiDisconnected: {
      OnWifiDisconnected();
      break;
    }
    default:
      break;
  }
  // lock.lock();
  goto loop_start;
}

#if 0
void EngineImpl::Send(const MessageType type) {
  Send(std::move(MakeMessage(type)));
}

void EngineImpl::Send(Message &&message) {
  std::unique_lock<std::mutex> lock(mutex_);
  message_queue_.emplace_back(std::move(message));
  cv_.notify_all();
}

EngineImpl::Message EngineImpl::MakeMessage(const MessageType type) {
  Message message;
  message.emplace_back(type);
  return message;
}

EngineImpl::MessageType EngineImpl::PopType(EngineImpl::Message &message) {
  const auto type = std::get<MessageType>(message.front());
  message.pop_front();
  return type;
}
#endif

void EngineImpl::OpenAudioSession() {
  auto const message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "type", "hello");
  cJSON_AddNumberToObject(message, "version", 3);
  cJSON_AddStringToObject(message, "transport", "udp");

  auto const audio_params = cJSON_CreateObject();
  cJSON_AddStringToObject(audio_params, "format", "opus");
  cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
  cJSON_AddNumberToObject(audio_params, "channels", 1);
  cJSON_AddNumberToObject(audio_params, "frame_duration", 60);
  cJSON_AddItemToObject(message, "audio_params", audio_params);

  auto text = cJSON_PrintUnformatted(message);

  CLOG("esp_mqtt_client_publish %s, %s", mqtt_publish_topic_.c_str(), text);
  auto err = esp_mqtt_client_publish(mqtt_client_, mqtt_publish_topic_.c_str(), text, strlen(text), 0, 0);
  cJSON_free(text);
  cJSON_Delete(message);

  CLOG("esp_mqtt_client_publish err:%d", err);
  state_ = State::kAudioSessionOpening;
  if (display_) {
    display_->SetStatus("连接中...");
    display_->SetEmotion("neutral");
    display_->SetChatMessage("system", "");
  }
}

void EngineImpl::CloseAudioSession(const std::string &session_id) {
  CLOG("session_id: %s", session_id.c_str());
  if (!session_id.empty() && audio_session_ && audio_session_->session_id() == session_id) {
    audio_session_.reset();
    state_ = State::kMqttConnected;
    if (display_) {
      display_->SetStatus("待命");
      display_->SetEmotion("neutral");
      display_->SetChatMessage("system", "");
    }
  }
}

void EngineImpl::CloseAudioSession() {
  if (nullptr == audio_session_) {
    return;
  }

  auto const message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "session_id", audio_session_->session_id().c_str());
  cJSON_AddStringToObject(message, "type", "goodbye");
  auto text = cJSON_PrintUnformatted(message);
  esp_mqtt_client_publish(mqtt_client_, mqtt_publish_topic_.c_str(), text, strlen(text), 0, 0);
  cJSON_free(text);
  cJSON_Delete(message);
  audio_session_.reset();
  state_ = State::kMqttConnected;

  if (display_) {
    display_->SetStatus("待命");
    display_->SetEmotion("neutral");
    display_->SetChatMessage("system", "");
  }
  CLOG("OK");
}

void EngineImpl::AbortSpeaking() {
  if (nullptr == audio_session_) {
    return;
  }

  auto const message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "session_id", audio_session_->session_id().c_str());
  cJSON_AddStringToObject(message, "type", "abort");
  auto text = cJSON_PrintUnformatted(message);
  esp_mqtt_client_publish(mqtt_client_, mqtt_publish_topic_.c_str(), text, strlen(text), 0, 0);
  cJSON_free(text);
  cJSON_Delete(message);
  CLOG("OK");
}

void EngineImpl::StartAudioTransmission() {
  if (!audio_session_) {
    return;
  }

  auto root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "session_id", audio_session_->session_id().c_str());
  cJSON_AddStringToObject(root, "type", "listen");
  cJSON_AddStringToObject(root, "state", "start");
  cJSON_AddStringToObject(root, "mode", "auto");
  auto text = cJSON_PrintUnformatted(root);
  auto err = esp_mqtt_client_publish(mqtt_client_, mqtt_publish_topic_.c_str(), text, strlen(text), 0, 0);
  cJSON_free(text);
  cJSON_Delete(root);
  audio_session_->CloseAudioOutput();
  audio_session_->OpenAudioInput(audio_input_device_);
  if (display_) {
    display_->SetStatus("聆听中...");
    display_->SetEmotion("neutral");
  }
}

bool EngineImpl::ConnectMqtt() {
  CLOG("state: %u", state_);
  if (state_ != State::kNetworkConnected) {
    CLOG("invalid state: %u", state_);
    return false;
  }

  if (display_) {
    display_->SetStatus("Mqtt Connecting");
    display_->SetEmotion("neutral");
    display_->SetChatMessage("system", "");
  }
  auto config = GetConfigFromServer();

  CLOG("mqtt endpoint: %s", config.mqtt.endpoint.c_str());
  CLOG("mqtt client_id: %s", config.mqtt.client_id.c_str());
  CLOG("mqtt username: %s", config.mqtt.username.c_str());
  CLOG("mqtt password: %s", config.mqtt.password.c_str());
  CLOG("mqtt publish_topic: %s", config.mqtt.publish_topic.c_str());
  CLOG("mqtt subscribe_topic: %s", config.mqtt.subscribe_topic.c_str());

  CLOG("activation code: %s", config.activation.code.c_str());
  CLOG("activation message: %s", config.activation.message.c_str());

  if (!config.activation.code.empty()) {
    if (display_) {
      display_->SetStatus("激活设备");
      display_->SetEmotion("happy");
      display_->SetChatMessage("system", config.activation.message.c_str());
    }
    CLOG("激活设备: %s", config.activation.message.c_str());
    return false;
  }

  mqtt_publish_topic_ = config.mqtt.publish_topic;
  mqtt_subscribe_topic_ = config.mqtt.subscribe_topic;

  esp_mqtt_client_config_t mqtt_config = {};
  mqtt_config.broker.address.hostname = config.mqtt.endpoint.c_str();
  mqtt_config.broker.address.port = 8883;
  mqtt_config.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
  mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  mqtt_config.credentials.client_id = config.mqtt.client_id.c_str();
  mqtt_config.credentials.username = config.mqtt.username.c_str();
  mqtt_config.credentials.authentication.password = config.mqtt.password.c_str();
  mqtt_config.session.keepalive = 90;

  mqtt_config.buffer.size = 512;
  mqtt_config.buffer.out_size = 512;
  mqtt_config.task.stack_size = 4096;

  // CLOG("mqtt_config.broker.address.hostname: %s", mqtt_config.broker.address.hostname);
  // CLOG("mqtt_config.broker.address.port: %d", mqtt_config.broker.address.port);
  // CLOG("mqtt_config.broker.address.transport: %d", mqtt_config.broker.address.transport);
  // CLOG("mqtt_config.broker.verification.crt_bundle_attach: %p", mqtt_config.broker.verification.crt_bundle_attach);
  // CLOG("mqtt_config.credentials.client_id: %s", mqtt_config.credentials.client_id);
  // CLOG("mqtt_config.credentials.username: %s", mqtt_config.credentials.username);
  // CLOG("mqtt_config.credentials.authentication.password: %s", mqtt_config.credentials.authentication.password);
  // CLOG("mqtt_config.session.keepalive: %d", mqtt_config.session.keepalive);

  mqtt_client_ = esp_mqtt_client_init(&mqtt_config);
  auto err = esp_mqtt_client_register_event(mqtt_client_, MQTT_EVENT_ANY, &EngineImpl::OnMqttEvent, this);
  if (err != ESP_OK) {
    CLOG("esp_mqtt_client_register_event failed. Error: %s", esp_err_to_name(err));
    return false;
  }

  CLOG("mqtt client start");
  err = esp_mqtt_client_start(mqtt_client_);
  if (err != ESP_OK) {
    CLOG("esp_mqtt_client_start failed. Error: %s", esp_err_to_name(err));
    return false;
  }

  state_ = State::kMqttConnecting;
  CLOG_TRACE();
  return true;
}

}  // namespace ai_vox