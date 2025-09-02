#include <Arduino.h>
#include <WiFi.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include "ai_vox_engine.h"
#include "ai_vox_observer.h"
#include "audio_device/audio_device_es8311.h"
#include "display.h"
#include "led_strip.h"

#ifndef ARDUINO_ESP32S3_DEV
#error "This example only supports ESP32S3-Dev board."
#endif

#ifndef CONFIG_SPIRAM_MODE_OCT
#error "This example requires PSRAM to OPI PSRAM. Please enable it in Arduino IDE."
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "unknown"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "unknown"
#endif

namespace {
constexpr auto kTriggerPin = GPIO_NUM_0;  // Button

constexpr auto kLcdBacklightPin = GPIO_NUM_16;

// https://docs.espressif.com/projects/esp-iot-solution/zh_CN/latest/display/lcd/spi_lcd.html#line
constexpr auto kSt7789Sda = GPIO_NUM_21;  // SPI MOSI
constexpr auto kSt7789Scl = GPIO_NUM_17;  // SPI SCLK
constexpr auto kSt7789Csx = GPIO_NUM_15;  // SPI CS
constexpr auto kSt7789Dcx = GPIO_NUM_14;  // SPI DC

constexpr auto kWs2812LedPin = GPIO_NUM_41;

constexpr auto kEs8311Mclk = GPIO_NUM_11;   // ES8311 Master clock (MCLK) -- I2S MCLK/MCK
constexpr auto kEs8311Sclk = GPIO_NUM_10;   // ES8311 Serial data bit clock/DMIC bit clock (SCLK/DMIC_SCL) -- I2S SCLK/BCK
constexpr auto kEs8311Lrck = GPIO_NUM_8;    // ES8311 Serial data left and right channel frame clock (LRCK) --  I2S WS
constexpr auto kEs8311Dsdin = GPIO_NUM_7;   // ES8311 DAC serial data input (DSDIN) -- I2S DOUT
constexpr auto kEs8311Asdout = GPIO_NUM_9;  // ES8311 ADC serial data output (ASDOUT) -- I2S DIN

constexpr auto kI2cScl = GPIO_NUM_12;  // ES8311 CCLK
constexpr auto kI2cSda = GPIO_NUM_13;  // ES8311 CDATA

constexpr auto kI2CPort = I2C_NUM_1;

constexpr auto kDisplaySpiMode = 0;
constexpr uint32_t kDisplayWidth = 240;
constexpr uint32_t kDisplayHeight = 240;
constexpr bool kDisplayMirrorX = false;
constexpr bool kDisplayMirrorY = false;
constexpr bool kDisplayInvertColor = true;
constexpr bool kDisplaySwapXY = false;
constexpr auto kDisplayRgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB;

constexpr uint8_t kEs8311I2cAddress = 0x30;
constexpr uint32_t kAudioSampleRate = 16000;

i2c_master_bus_handle_t g_i2c_master_bus_handle = nullptr;
std::shared_ptr<ai_vox::iot::Entity> g_led_iot_entity;
std::shared_ptr<ai_vox::iot::Entity> g_screen_iot_entity;
std::shared_ptr<ai_vox::iot::Entity> g_speaker_iot_entity;
std::shared_ptr<ai_vox::AudioDeviceEs8311> g_audio_device_es8311;
std::unique_ptr<Display> g_display;
auto g_observer = std::make_shared<ai_vox::Observer>();

led_strip_handle_t g_led_strip;

void InitI2cBus() {
  const i2c_master_bus_config_t i2c_master_bus_config = {
      .i2c_port = kI2CPort,
      .sda_io_num = kI2cSda,
      .scl_io_num = kI2cScl,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .intr_priority = 0,
      .trans_queue_depth = 0,
      .flags =
          {
              .enable_internal_pullup = 1,
              .allow_pd = 0,
          },
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_bus_config, &g_i2c_master_bus_handle));
  printf("g_i2c_master_bus: %p\n", g_i2c_master_bus_handle);
}

void InitEs8311() {
  g_audio_device_es8311 = std::make_shared<ai_vox::AudioDeviceEs8311>(g_i2c_master_bus_handle,
                                                                      kEs8311I2cAddress,  // ES8311 I2C address
                                                                      kI2CPort,           // I2C port
                                                                      kAudioSampleRate,   // sample rate
                                                                      kEs8311Mclk,        // ES8311 Master clock (MCLK)
                                                                      kEs8311Sclk,    // ES8311 Serial data bit clock/DMIC bit clock (SCLK/DMIC_SCL)
                                                                      kEs8311Lrck,    // ES8311 Serial data left and right channel frame clock (LRCK)
                                                                      kEs8311Asdout,  // ES8311 ADC serial data output (ASDOUT)
                                                                      kEs8311Dsdin    // ES8311 DAC serial data input (DSDIN)
  );
}

void InitDisplay() {
  pinMode(kLcdBacklightPin, OUTPUT);
  analogWrite(kLcdBacklightPin, 255);

  //  https://docs.espressif.com/projects/esp-iot-solution/zh_CN/latest/display/lcd/spi_lcd.html#id4
  spi_bus_config_t buscfg{
      .mosi_io_num = kSt7789Sda,
      .miso_io_num = GPIO_NUM_NC,
      .sclk_io_num = kSt7789Scl,
      .quadwp_io_num = GPIO_NUM_NC,
      .quadhd_io_num = GPIO_NUM_NC,
      .data4_io_num = GPIO_NUM_NC,
      .data5_io_num = GPIO_NUM_NC,
      .data6_io_num = GPIO_NUM_NC,
      .data7_io_num = GPIO_NUM_NC,
      .data_io_default_level = false,
      .max_transfer_sz = kDisplayWidth * kDisplayHeight * sizeof(uint16_t),
      .flags = 0,
      .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
      .intr_flags = 0,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t panel_io = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
  // 液晶屏控制IO初始化
  ESP_LOGD(TAG, "Install panel IO");

  // https://docs.espressif.com/projects/esp-iot-solution/zh_CN/latest/display/lcd/spi_lcd.html#id8
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = kSt7789Csx;  // SPI CS
  io_config.dc_gpio_num = kSt7789Dcx;  // SPI DC
  io_config.spi_mode = kDisplaySpiMode;
  io_config.pclk_hz = 40 * 1000 * 1000;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

  // 初始化液晶屏驱动芯片
  ESP_LOGD(TAG, "Install LCD driver");
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = -1;
  panel_config.rgb_ele_order = kDisplayRgbElementOrder;
  panel_config.bits_per_pixel = 16;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

  esp_lcd_panel_reset(panel);

  esp_lcd_panel_init(panel);
  esp_lcd_panel_invert_color(panel, kDisplayInvertColor);
  esp_lcd_panel_swap_xy(panel, kDisplaySwapXY);
  esp_lcd_panel_mirror(panel, kDisplayMirrorX, kDisplayMirrorY);

  g_display = std::make_unique<Display>(panel_io, panel, kDisplayWidth, kDisplayHeight, 0, 0, kDisplayMirrorX, kDisplayMirrorY, kDisplaySwapXY);
  g_display->Start();
}

void InitLed() {
  // LED strip general initialization, according to your led board design
  led_strip_config_t strip_config = {.strip_gpio_num = kWs2812LedPin,  // The GPIO that connected to the LED strip's data line
                                     .max_leds = 1,                    // The number of LEDs in the strip,
                                     .led_model = LED_MODEL_WS2812,    // LED strip model
                                     .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,  // The color order of the strip: GRB
                                     .flags = {
                                         .invert_out = false,  // don't invert the output signal
                                     }};

  // LED strip backend configuration: RMT
  led_strip_rmt_config_t rmt_config = {.clk_src = RMT_CLK_SRC_DEFAULT,     // different clock source can lead to different power consumption
                                       .resolution_hz = 10 * 1000 * 1000,  // RMT counter clock frequency
                                       .mem_block_symbols = 0,             // the memory block size used by the RMT channel
                                       .flags = {
                                           .with_dma = 0,  // Using DMA can improve performance when driving more LEDs
                                       }};
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip));
  ESP_ERROR_CHECK(led_strip_clear(g_led_strip));
}

void InitIot() {
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
  g_speaker_iot_entity->UpdateState("volume", g_audio_device_es8311->volume());

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

  // Screen
  // 1.Define the properties for the screen entity
  std::vector<ai_vox::iot::Property> screen_properties({
      {
          "theme",                         // property name
          "主题",                          // property description
          ai_vox::iot::ValueType::kString  // property type
      },
      {
          "brightness",                    // property name
          "当前亮度百分比",                // property description
          ai_vox::iot::ValueType::kNumber  // property type
      },
      // add more properties as needed
  });

  // 2.Define the functions for the screen entity
  std::vector<ai_vox::iot::Function> screen_functions({
      {"SetTheme",      // function name
       "设置屏幕主题",  // function description
       {
           {
               "theme_name",                     // parameter name
               "主题模式, light 或 dark",        // parameter description
               ai_vox::iot::ValueType::kString,  // parameter type
               true                              // parameter required
           },
           // add more parameters as needed
       }},
      {"SetBrightness",  // function name
       "设置亮度",       // function description
       {
           {
               "brightness",                     // parameter name
               "0到100之间的整数",               // parameter description
               ai_vox::iot::ValueType::kNumber,  // parameter type
               true                              // parameter required
           },
           // add more parameters as needed
       }},
      // add more functions as needed
  });

  // 3.Create the screen entity
  g_screen_iot_entity = std::make_shared<ai_vox::iot::Entity>("Screen",                          // name
                                                              "这是一个屏幕，可设置主题和亮度",  // description
                                                              std::move(screen_properties),      // properties
                                                              std::move(screen_functions)        // functions
  );

  // 4.Initialize the screen entity with default values
  g_screen_iot_entity->UpdateState("theme", "light");
  g_screen_iot_entity->UpdateState("brightness", 100);

  // 6.Register the screen entity with the AI Vox engine
  ai_vox_engine.RegisterIotEntity(g_screen_iot_entity);
}

#ifdef PRINT_HEAP_INFO_INTERVAL
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
#endif
}  // namespace

void setup() {
  Serial.begin(115200);
  InitI2cBus();
  InitDisplay();
  InitEs8311();

  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
    g_display->SetChatMessage(Display::Role::kSystem, "No SPIRAM available, please check your board.");
    while (true) {
      printf("No SPIRAM available, please check your board.\n");
      delay(1000);
    }
  }

  g_display->ShowStatus("Wifi connecting...");

  WiFi.useStaticBuffers(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    printf("Connecting to WiFi, ssid: %s, password: %s\n", WIFI_SSID, WIFI_PASSWORD);
  }

  printf("WiFi connected, IP address: %s\n", WiFi.localIP().toString().c_str());
  g_display->ShowStatus("Wifi connected");

  InitLed();
  InitIot();

  auto& ai_vox_engine = ai_vox::Engine::GetInstance();
  ai_vox_engine.SetObserver(g_observer);
  ai_vox_engine.SetTrigger(kTriggerPin);
  ai_vox_engine.SetOtaUrl("https://api.tenclass.net/xiaozhi/ota/");
  ai_vox_engine.ConfigWebsocket("wss://api.tenclass.net/xiaozhi/v1/",
                                {
                                    {"Authorization", "Bearer test-token"},
                                });
  ai_vox_engine.Start(g_audio_device_es8311, g_audio_device_es8311);
  g_display->ShowStatus("AI Vox Engine starting...");
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
      g_display->ShowStatus("激活设备");
      g_display->SetChatMessage(Display::Role::kSystem, activation_event->message);
    } else if (auto state_changed_event = std::get_if<ai_vox::Observer::StateChangedEvent>(&event)) {
      switch (state_changed_event->new_state) {
        case ai_vox::ChatState::kIdle: {
          printf("Idle\n");
          break;
        }
        case ai_vox::ChatState::kIniting: {
          printf("Initing...\n");
          g_display->ShowStatus("初始化");
          break;
        }
        case ai_vox::ChatState::kStandby: {
          printf("Standby\n");
          g_display->ShowStatus("待命");
          break;
        }
        case ai_vox::ChatState::kConnecting: {
          printf("Connecting...\n");
          g_display->ShowStatus("连接中...");
          break;
        }
        case ai_vox::ChatState::kListening: {
          printf("Listening...\n");
          g_display->ShowStatus("聆听中");
          break;
        }
        case ai_vox::ChatState::kSpeaking: {
          printf("Speaking...\n");
          g_display->ShowStatus("说话中");
          break;
        }
        default: {
          break;
        }
      }
    } else if (auto emotion_event = std::get_if<ai_vox::Observer::EmotionEvent>(&event)) {
      printf("emotion: %s\n", emotion_event->emotion.c_str());
      g_display->SetEmotion(emotion_event->emotion);
    } else if (auto chat_message_event = std::get_if<ai_vox::Observer::ChatMessageEvent>(&event)) {
      switch (chat_message_event->role) {
        case ai_vox::ChatRole::kAssistant: {
          printf("role: assistant, content: %s\n", chat_message_event->content.c_str());
          g_display->SetChatMessage(Display::Role::kAssistant, chat_message_event->content);
          break;
        }
        case ai_vox::ChatRole::kUser: {
          printf("role: user, content: %s\n", chat_message_event->content.c_str());
          g_display->SetChatMessage(Display::Role::kUser, chat_message_event->content);
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
          ESP_ERROR_CHECK(led_strip_set_pixel(g_led_strip, 0, 200, 200, 200));
          ESP_ERROR_CHECK(led_strip_refresh(g_led_strip));
          g_led_iot_entity->UpdateState("state", true);  //  Note: Must UpdateState after change the device state
        } else if (iot_message_event->function == "TurnOff") {
          printf("turn off led\n");
          ESP_ERROR_CHECK(led_strip_clear(g_led_strip));
          g_led_iot_entity->UpdateState("state", false);  // Note: Must UpdateState after change the device state
        }
      } else if (iot_message_event->name == "Screen") {
        if (iot_message_event->function == "SetTheme") {
          if (const auto it = iot_message_event->parameters.find("theme_name"); it != iot_message_event->parameters.end()) {
            auto theme_name = it->second;
            if (std::get_if<std::string>(&theme_name)) {
              printf("Screen theme: %s\n", std::get<std::string>(theme_name).c_str());
              // TODO: Set the theme
              // g_display->SetTheme(std::get<std::string>(theme_name));
              g_screen_iot_entity->UpdateState("theme", std::get<std::string>(theme_name));  // Note: Must UpdateState after change the device state
            }
          }
        } else if (iot_message_event->function == "SetBrightness") {
          if (const auto it = iot_message_event->parameters.find("brightness"); it != iot_message_event->parameters.end()) {
            auto brightness = it->second;
            if (std::get_if<int64_t>(&brightness)) {
              printf("Screen brightness: %lld\n", std::get<int64_t>(brightness));
              analogWrite(kLcdBacklightPin, 255 * std::get<int64_t>(brightness) / 100);
              g_screen_iot_entity->UpdateState("brightness", std::get<int64_t>(brightness));  // Note: Must UpdateState after change the device state
            }
          }
        }
      } else if (iot_message_event->name == "Speaker") {
        if (iot_message_event->function == "SetVolume") {
          if (const auto it = iot_message_event->parameters.find("volume"); it != iot_message_event->parameters.end()) {
            auto volume = it->second;
            if (std::get_if<int64_t>(&volume)) {
              printf("Speaker volume: %lld\n", std::get<int64_t>(volume));
              g_audio_device_es8311->set_volume(std::get<int64_t>(volume));
              g_speaker_iot_entity->UpdateState("volume", std::get<int64_t>(volume));  // Note: Must UpdateState after change the device state
            }
          }
        }
      }
    }
  }

  taskYIELD();
}