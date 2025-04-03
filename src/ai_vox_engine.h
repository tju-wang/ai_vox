#pragma once

#ifndef _AI_VOX_ENGINE_H_
#define _AI_VOX_ENGINE_H_

#include <driver/gpio.h>
#include <esp_lcd_types.h>

#include <memory>
#include <string>

#include "audio_input_device.h"
#include "audio_output_device.h"

namespace ai_vox {

class Engine {
 public:
  static Engine& GetInstance();
  Engine() = default;
  virtual ~Engine() = default;
  virtual void SetWifi(const std::string ssid, const std::string password) = 0;
  virtual void SetTrigger(const gpio_num_t gpio) = 0;
  virtual void InitDisplay(
      esp_lcd_panel_io_handle_t lcd_panel_io, esp_lcd_panel_handle_t lcd_panel, uint32_t width, uint32_t height, bool mirror_x, bool mirror_y) = 0;
  virtual void Start(std::shared_ptr<AudioInputDevice> audio_input_device, std::shared_ptr<AudioOutputDevice> audio_output_device) = 0;

 private:
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
};

}  // namespace ai_vox

#endif