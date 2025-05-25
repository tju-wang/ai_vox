#include <ai_vox_engine.h>
#include <ai_vox_observer.h>
#include <audio_input_device.h>
#include <audio_output_device.h>
#include <i2s_std_audio_input_device.h>
#include <i2s_std_audio_output_device.h>
#include <iot_entity.h>
#include <ESP32Servo.h>

#include <WiFi.h>

#include "ai_vox_engine.h"
#include "ai_vox_observer.h"
#include "i2s_std_audio_input_device.h"
#include "i2s_std_audio_output_device.h"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C mylcd(0x27,16,2);

#include <FastLED.h>

#ifndef WIFI_SSID
#define WIFI_SSID "尚奇青少年创新学院"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "12345678"
#endif

namespace {
#if defined(ARDUINO_ESP32_DEV)
constexpr gpio_num_t kMicPinBclk = GPIO_NUM_27;
constexpr gpio_num_t kMicPinWs = GPIO_NUM_16;
constexpr gpio_num_t kMicPinDin = GPIO_NUM_14;

constexpr gpio_num_t kSpeakerPinBclk = GPIO_NUM_4;
constexpr gpio_num_t kSpeakerPinWs = GPIO_NUM_32;
constexpr gpio_num_t kSpeakerPinDout = GPIO_NUM_2;

constexpr gpio_num_t kTriggerPin = GPIO_NUM_25;
constexpr gpio_num_t kLedPin = GPIO_NUM_26;
constexpr gpio_num_t kFanPin = GPIO_NUM_18;
constexpr gpio_num_t kWindowServoPin = GPIO_NUM_13;
constexpr gpio_num_t kDoorServoPin = GPIO_NUM_12;
constexpr gpio_num_t kBuzzerPin = GPIO_NUM_5;

constexpr gpio_num_t kPeopleDetect = GPIO_NUM_34;
constexpr gpio_num_t kRGB6812Pin = GPIO_NUM_17;

//TodoList...
//少气体传感器
//NFC卡
//蜂鸣器


#elif defined(ARDUINO_ESP32S3_DEV)
constexpr gpio_num_t kMicPinBclk = GPIO_NUM_19;
constexpr gpio_num_t kMicPinWs = GPIO_NUM_46;
constexpr gpio_num_t kMicPinDin = GPIO_NUM_20;

constexpr gpio_num_t kSpeakerPinBclk = GPIO_NUM_2;
constexpr gpio_num_t kSpeakerPinWs = GPIO_NUM_14;
constexpr gpio_num_t kSpeakerPinDout = GPIO_NUM_1;

constexpr gpio_num_t kTriggerPin = GPIO_NUM_0;
constexpr gpio_num_t kLedPin = GPIO_NUM_48;
constexpr gpio_num_t kFanPin = GPIO_NUM_18;
constexpr gpio_num_t kWindowServerPin = GPIO_NUM_13;
#endif

auto g_observer = std::make_shared<ai_vox::Observer>();
std::shared_ptr<ai_vox::iot::Entity> g_led_iot_entity;
std::shared_ptr<ai_vox::iot::Entity> g_fan_iot_entity;
std::shared_ptr<ai_vox::iot::Entity> g_window_iot_entity;
std::shared_ptr<ai_vox::iot::Entity> g_door_iot_entity;
std::shared_ptr<ai_vox::iot::Entity> g_SK6812_iot_entity;
std::shared_ptr<ai_vox::iot::Entity> g_speaker_iot_entity;
auto g_audio_output_device = std::make_shared<ai_vox::I2sStdAudioOutputDevice>(kSpeakerPinBclk, kSpeakerPinWs, kSpeakerPinDout);
Servo myservoWindow;
Servo myservoDoor;
bool SK6812State = false;
int64_t SK6812Mode = 0;


#define LED_COUNT 4
CRGB leds[LED_COUNT];

std::string RoleToString(const ai_vox::ChatRole role) {
  switch (role) {
    case ai_vox::ChatRole::kAssistant:
      return "assistant";
    case ai_vox::ChatRole::kUser:
      return "user";
  }
  return "unknown";
}

void InitIot() {
  printf("InitIot\n");
  auto& ai_vox_engine = ai_vox::Engine::GetInstance();

  // Speaker
  // 1.Define the properties for the speaker entity
  std::vector<ai_vox::iot::Property> speaker_properties({
      {
          "volume",                        // property name
          "当前音量值",                    // property description
          ai_vox::iot::ValueType::kNumber  // property type: number, string or bool
      },
      // add more properties as needed
  });

  // 2.Define the functions for the speaker entity
  std::vector<ai_vox::iot::Function> speaker_functions({
      {"SetVolume",  // function name
       "设置音量",   // function description
       {
           {
               "volume",                         // parameter name
               "0到100之间的整数",               // parameter description
               ai_vox::iot::ValueType::kNumber,  // parameter type
               true                              // parameter required
           },
           // add more parameters as needed
       }},
      // add more functions as needed
  });

  // 3.Create the speaker entity
  g_speaker_iot_entity = std::make_shared<ai_vox::iot::Entity>("Speaker",                      // name
                                                               "扬声器",                       // description
                                                               std::move(speaker_properties),  // properties
                                                               std::move(speaker_functions)    // functions
  );

  // 4.Initialize the speaker entity with default values
  g_speaker_iot_entity->UpdateState("volume", g_audio_output_device->volume());

  // 5.Register the speaker entity with the AI Vox engine
  ai_vox_engine.RegisterIotEntity(g_speaker_iot_entity);

  // LED
  // 1.Define the properties for the LED entity
  std::vector<ai_vox::iot::Property> led_properties({
      {
          "state",                       // property name
          "LED灯开关状态",               // property description
          ai_vox::iot::ValueType::kBool  // property type
      },
      // add more properties as needed
  });

  // 2.Define the functions for the LED entity
  std::vector<ai_vox::iot::Function> led_functions({
      {"TurnOn",     // function name
       "打开LED灯",  // function description
       {
           // no parameters
       }},
      {"TurnOff",    // function name
       "关闭LED灯",  // function description
       {
           // no parameters
       }},
      // add more functions as needed
  });

  // 3.Create the LED entity
  g_led_iot_entity = std::make_shared<ai_vox::iot::Entity>("Led",                      // name
                                                           "LED灯",                    // description
                                                           std::move(led_properties),  // properties
                                                           std::move(led_functions)    // functions
  );

  // 4.Initialize the LED entity with default values
  g_led_iot_entity->UpdateState("state", false);

  // 5.Register the LED entity with the AI Vox engine
  ai_vox_engine.RegisterIotEntity(g_led_iot_entity);

  //fan 风扇定义
  // 1.Define the properties for the fan entity
  std::vector<ai_vox::iot::Property> fan_properties({
      {
          "state",                       // property name
          "风扇开关状态",               // property description
          ai_vox::iot::ValueType::kBool  // property type
      },
      // add more properties as needed
  });

  // 2.Define the functions for the fan entity
  std::vector<ai_vox::iot::Function> fan_functions({
      {"TurnOn",     // function name
       "打开风扇",  // function description
       {
           // no parameters
       }},
      {"TurnOff",    // function name
       "关闭风扇",  // function description
       {
           // no parameters
       }},
      // add more functions as needed
  });

  // 3.Create the fan entity
  g_fan_iot_entity = std::make_shared<ai_vox::iot::Entity>("fan",                      // name
                                                           "散热风扇",                    // description
                                                           std::move(fan_properties),  // properties
                                                           std::move(fan_functions)    // functions
  );

  // 4.Initialize the fan entity with default values
  g_fan_iot_entity->UpdateState("state", false);

  // 5.Register the fan entity with the AI Vox engine
  ai_vox_engine.RegisterIotEntity(g_fan_iot_entity);

  //智能家居窗户
  // 1.Define the properties for the window entity
  std::vector<ai_vox::iot::Property> window_properties({
      {
          "state",                       // property name
          "窗户开关的状态",               // property description
          ai_vox::iot::ValueType::kBool  // property type
      },
      // add more properties as needed
  });

  // 2.Define the functions for the window entity
  std::vector<ai_vox::iot::Function> window_functions({
      {"TurnOn",     // function name
       "打开窗户",  // function description
       {
           // no parameters
       }},
      {"TurnOff",    // function name
       "关闭窗户",  // function description
       {
           // no parameters
       }},
      // add more functions as needed
  });

  // 3.Create the window entity
  g_window_iot_entity = std::make_shared<ai_vox::iot::Entity>("window",                      // name
                                                           "家里的自动窗户",                    // description
                                                           std::move(window_properties),  // properties
                                                           std::move(window_functions)    // functions
  );

  // 4.Initialize the window entity with default values
  g_window_iot_entity->UpdateState("state", false);

  // 5.Register the window entity with the AI Vox engine
  ai_vox_engine.RegisterIotEntity(g_window_iot_entity);

  //智能家居门锁
  // 1.Define the properties for the door entity
  std::vector<ai_vox::iot::Property> door_properties({
      {
          "state",                       // property name
          "门锁开关状态",               // property description
          ai_vox::iot::ValueType::kBool  // property type
      },
      // add more properties as needed
  });

  // 2.Define the functions for the window entity
  std::vector<ai_vox::iot::Function> door_functions({
      {"TurnOn",     // function name
       "打开门锁",  // function description
       {
           // no parameters
       }},
      {"TurnOff",    // function name
       "关闭门锁",  // function description
       {
           // no parameters
       }},
      // add more functions as needed
  });

  // 3.Create the door entity
  g_door_iot_entity = std::make_shared<ai_vox::iot::Entity>("door",                      // name
                                                           "家里的门锁",                    // description
                                                           std::move(door_properties),  // properties
                                                           std::move(door_functions)    // functions
  );

  // 4.Initialize the door entity with default values
  g_door_iot_entity->UpdateState("state", false);

  // 5.Register the door entity with the AI Vox engine
  ai_vox_engine.RegisterIotEntity(g_door_iot_entity);

  //娱乐灯光系统
  // 1.Define the properties for the door entity
  std::vector<ai_vox::iot::Property> SK6812_properties({
      {
          "mode",                           // property name
          "客厅的霓虹灯的模式",               // property description
          ai_vox::iot::ValueType::kNumber   // property type
      },
      {
          "state",                       // property name
          "霓虹灯开关状态",               // property description
          ai_vox::iot::ValueType::kBool  // property type
      },
      // add more properties as needed
  });

  // 2.Define the functions for the SK6812 entity
  std::vector<ai_vox::iot::Function> SK6812_functions({
      {"SetMode",  // function name
       "设置霓虹灯模式",   // function description
       {
          {
              "mode",                         // parameter name
              "整数0或1",                     // parameter description
              ai_vox::iot::ValueType::kNumber,  // parameter type
              true                              // parameter required
          },
          // add more parameters as needed
       }},
      {"TurnOff",  // function name
       "关闭霓虹灯",   // function description
       {
        {
        },
        // add more parameters as needed
       }},
      {"TurnOn",  // function name
       "打开霓虹灯",   // function description
       {
        {
        },
        // add more parameters as needed
       }},
      // add more functions as needed
  });

  // 3.Create the SK6812 entity
  g_SK6812_iot_entity = std::make_shared<ai_vox::iot::Entity>("SK6812",                      // name
                                                           "客厅的霓虹灯，用于放松、招待客人", // description
                                                           std::move(SK6812_properties),  // properties
                                                           std::move(SK6812_functions)    // functions
  );

  // 4.Initialize the SK6812 entity with default values
  g_SK6812_iot_entity->UpdateState("mode", 0);
  g_SK6812_iot_entity->UpdateState("state", false);

  // 5.Register the SK6812 entity with the AI Vox engine
  ai_vox_engine.RegisterIotEntity(g_SK6812_iot_entity);
}

void PrintMemInfo() {
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
    const auto total_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const auto free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const auto min_free_size = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    printf("SPIRAM total size: %zu B (%zu KB), free size: %zu B (%zu KB), minimum free size: %zu B (%zu KB)\n",
           total_size,
           total_size >> 10,
           free_size,
           free_size >> 10,
           min_free_size,
           min_free_size >> 10);
  }

  if (heap_caps_get_total_size(MALLOC_CAP_INTERNAL) > 0) {
    const auto total_size = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    const auto free_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const auto min_free_size = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    printf("IRAM total size: %zu B (%zu KB), free size: %zu B (%zu KB), minimum free size: %zu B (%zu KB)\n",
           total_size,
           total_size >> 10,
           free_size,
           free_size >> 10,
           min_free_size,
           min_free_size >> 10);
  }

  if (heap_caps_get_total_size(MALLOC_CAP_DEFAULT) > 0) {
    const auto total_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    const auto free_size = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    const auto min_free_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    printf("DRAM total size: %zu B (%zu KB), free size: %zu B (%zu KB), minimum free size: %zu B (%zu KB)\n",
           total_size,
           total_size >> 10,
           free_size,
           free_size >> 10,
           min_free_size,
           min_free_size >> 10);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  printf("Init\n");

  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
    WiFi.useStaticBuffers(true);
  } else {
    WiFi.useStaticBuffers(false);
  }

  printf("Connecting to WiFi, ssid: %s, password: %s\n", WIFI_SSID, WIFI_PASSWORD);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    printf("Connecting to WiFi, ssid: %s, password: %s\n", WIFI_SSID, WIFI_PASSWORD);
  }

  printf("WiFi connected, IP address: %s\n", WiFi.localIP().toString().c_str());

  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);

  pinMode(kFanPin, OUTPUT);
  digitalWrite(kFanPin, LOW);

  myservoWindow.attach(kWindowServoPin, 500, 2500);
  myservoWindow.write(0);

  myservoDoor.attach(kDoorServoPin, 500, 2500);
  myservoDoor.write(0);

  pinMode(kBuzzerPin, OUTPUT);
  digitalWrite(kBuzzerPin, LOW);

  mylcd.init();
  mylcd.backlight();
  mylcd.clear();

  mylcd.setCursor(0, 0);
  mylcd.print("shangqi AI+ home");

  pinMode(kPeopleDetect, INPUT);

  FastLED.addLeds<SK6812, kRGB6812Pin, GRB>(leds, LED_COUNT);

  InitIot();

  auto audio_input_device = std::make_shared<ai_vox::I2sStdAudioInputDevice>(kMicPinBclk, kMicPinWs, kMicPinDin);

  auto& ai_vox_engine = ai_vox::Engine::GetInstance();
  ai_vox_engine.SetObserver(g_observer);
  ai_vox_engine.SetTrigger(kTriggerPin);
  ai_vox_engine.Start(audio_input_device, g_audio_output_device);
  printf("AI Vox engine started\n");
}

void loop() {
#ifdef PRINT_HEAP_INFO_INTERVAL
  static uint32_t s_print_heap_info_time = 0;
  if (s_print_heap_info_time == 0 || millis() - s_print_heap_info_time >= PRINT_HEAP_INFO_INTERVAL) {
    s_print_heap_info_time = millis();
    PrintMemInfo();
  }
#endif

  const auto events = g_observer->PopEvents();
  for (auto& event : events) {
    if (auto activation_event = std::get_if<ai_vox::Observer::ActivationEvent>(&event)) {
      printf("activation code: %s, message: %s\n", activation_event->code.c_str(), activation_event->message.c_str());
    } else if (auto state_changed_event = std::get_if<ai_vox::Observer::StateChangedEvent>(&event)) {
      printf("state changed from %u to %u\n", state_changed_event->old_state, state_changed_event->new_state);
      switch (state_changed_event->new_state) {
        case ai_vox::ChatState::kIdle: {
          printf("Idle\n");
          mylcd.setCursor(0, 1);
          mylcd.print("Idle");
          break;
        }
        case ai_vox::ChatState::kIniting: {
          printf("Initing...\n");
          mylcd.setCursor(0, 1);
          mylcd.print("Initing...");
          break;
        }
        case ai_vox::ChatState::kStandby: {
          printf("Standby\n");
          mylcd.setCursor(0, 1);
          mylcd.print("Standby");
          break;
        }
        case ai_vox::ChatState::kConnecting: {
          printf("Connecting...\n");
          mylcd.setCursor(0, 1);
          mylcd.print("Connecting...\n");
          break;
        }
        case ai_vox::ChatState::kListening: {
          printf("Listening...\n");
          mylcd.setCursor(0, 1);
          mylcd.print("Listening...");
          break;
        }
        case ai_vox::ChatState::kSpeaking: {
          printf("Speaking...\n");
          mylcd.setCursor(0, 1);
          mylcd.print("Speaking...");
          break;
        }
        default: {
          break;
        }
      }
    } else if (auto emotion_event = std::get_if<ai_vox::Observer::EmotionEvent>(&event)) {
      printf("emotion: %s\n", emotion_event->emotion.c_str());
    } else if (auto chat_message_event = std::get_if<ai_vox::Observer::ChatMessageEvent>(&event)) {
      switch (chat_message_event->role) {
        case ai_vox::ChatRole::kAssistant: {
          printf("role: assistant, content: %s\n", chat_message_event->content.c_str());
          break;
        }
        case ai_vox::ChatRole::kUser: {
          printf("role: user, content: %s\n", chat_message_event->content.c_str());
          break;
        }
      }
    } else if (auto iot_message_event = std::get_if<ai_vox::Observer::IotMessageEvent>(&event)) {
      printf("IOT message: %s, function: %s\n", iot_message_event->name.c_str(), iot_message_event->function.c_str());
      for (const auto& [key, value] : iot_message_event->parameters) {
        if (std::get_if<bool>(&value)) {
          printf("key: %s, value: %s\n", key.c_str(), std::get<bool>(value) ? "true" : "false");
        } else if (std::get_if<std::string>(&value)) {
          printf("key: %s, value: %s\n", key.c_str(), std::get<std::string>(value).c_str());
        } else if (std::get_if<int64_t>(&value)) {
          printf("key: %s, value: %lld\n", key.c_str(), std::get<int64_t>(value));
        }
      }

      if (iot_message_event->name == "Led") {
        if (iot_message_event->function == "TurnOn") {
          printf("turn on led\n");
          digitalWrite(kLedPin, HIGH);
          g_led_iot_entity->UpdateState("state", true);  // Note: Must UpdateState after change the device state
        } else if (iot_message_event->function == "TurnOff") {
          printf("turn off led\n");
          digitalWrite(kLedPin, LOW);
          g_led_iot_entity->UpdateState("state", false); // Note: Must UpdateState after change the device state
        }
      } else if (iot_message_event->name == "Speaker") {
        if (iot_message_event->function == "SetVolume") {
          if (const auto it = iot_message_event->parameters.find("volume"); it != iot_message_event->parameters.end()) {
            auto volume = it->second;
            if (std::get_if<int64_t>(&volume)) {
              printf("Speaker volume: %lld\n", std::get<int64_t>(volume));
              g_audio_output_device->SetVolume(std::get<int64_t>(volume));
              g_speaker_iot_entity->UpdateState("volume", std::get<int64_t>(volume)); // Note: Must UpdateState after change the device state
            }
          }
        }
      }
      else if (iot_message_event->name == "fan") {
        if (iot_message_event->function == "TurnOn") {
          printf("turn on fan\n");
          digitalWrite(kFanPin, HIGH);
          g_fan_iot_entity->UpdateState("state", true);  // Note: Must UpdateState after change the device state
        } else if (iot_message_event->function == "TurnOff") {
          printf("turn off fan\n");
          digitalWrite(kFanPin, LOW);
          g_fan_iot_entity->UpdateState("state", false); // Note: Must UpdateState after change the device state
        }
      }
      else if (iot_message_event->name == "window") {
        if (iot_message_event->function == "TurnOn") {
          printf("turn on window\n");
          myservoWindow.write(100);
          g_window_iot_entity->UpdateState("state", true);  // Note: Must UpdateState after change the device state
        } else if (iot_message_event->function == "TurnOff") {
          printf("turn off window\n");
          myservoWindow.write(0);
          g_window_iot_entity->UpdateState("state", false); // Note: Must UpdateState after change the device state
        }
      }
      else if (iot_message_event->name == "door") {
        if (iot_message_event->function == "TurnOn") {
          printf("turn on door\n");
          myservoDoor.write(90);
          g_door_iot_entity->UpdateState("state", true);  // Note: Must UpdateState after change the device state
        } else if (iot_message_event->function == "TurnOff") {
          printf("turn off door\n");
          myservoDoor.write(0);
          g_door_iot_entity->UpdateState("state", false); // Note: Must UpdateState after change the device state
        }
      }
      else if (iot_message_event->name == "SK6812") {
        if (iot_message_event->function == "SetMode") {
          if (const auto it = iot_message_event->parameters.find("mode"); it != iot_message_event->parameters.end()) {
            auto mode = it->second;
            if (std::get_if<int64_t>(&mode)) {
              printf("SK6812 mode: %lld\n", std::get<int64_t>(mode));
              SK6812State = true;
              SK6812Mode = std::get<int64_t>(mode);
              g_SK6812_iot_entity->UpdateState("mode", std::get<int64_t>(mode)); // Note: Must UpdateState after change the device state
              g_SK6812_iot_entity->UpdateState("state", true);
            }
          }
        }
        if (iot_message_event->function == "TurnOff") {
            SK6812State = false;
            g_SK6812_iot_entity->UpdateState("state", false);
        }
        if (iot_message_event->function == "TurnOn") {
            SK6812State = true;
            g_SK6812_iot_entity->UpdateState("state", true);
        }
      }
    }
  }

  //传感器自动化场景
  iotAutoTask();

  taskYIELD();
}

void iotAutoTask()
{
  //人体传感器状态，有问题报警
  boolean peopleDetectVal = digitalRead(kPeopleDetect);
  // printf("peopleDetectVal %d\n", peopleDetectVal);

  //客厅霓虹灯光控制
  if (SK6812State)
  {
    if (SK6812Mode == 0)
    {
      rainbowEffect();
    }
    else if(SK6812Mode == 1)
    {
      meteorShower();
    }
  }
  else
  {
    fill_solid(leds, LED_COUNT, CRGB::Black);  // 参数：LED数组、数量、颜色
    FastLED.show();
  }
}

void rainbowEffect() {
  static uint8_t hue = 0; // 色调值（0-255）
  for(int i=0; i<LED_COUNT; i++) {
    leds[i] = CHSV(hue + (i*2), 255, 255); // 创建彩虹色带
  }
  FastLED.show();
  hue += 1;  // 改变色调值产生动态效果
  delay(20);
}

void meteorShower() {
  fadeToBlackBy(leds, LED_COUNT, 50); // 渐隐效果

  static int pos = 0;
  leds[pos] = CHSV(random8(), 255, 255); // 随机颜色
  pos = (pos + 1) % LED_COUNT;

  FastLED.show();
  delay(20);
}
