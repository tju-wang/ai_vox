#pragma once

#ifndef _SSD1306_DISPLAY_H_
#define _SSD1306_DISPLAY_H_

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_ssd1306.h>

#include <string>

struct lv_display_t;
struct lv_obj_t;

class Ssd1306Display {
 public:
  Ssd1306Display(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, bool mirror_x, bool mirror_y);
  ~Ssd1306Display();
  void Start();
  void SetChatMessage(std::string content);
  void ShowStatus(const char* status);
  void SetEmotion(const std::string& emotion);

 private:
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  lv_display_t* display_ = nullptr;
  lv_obj_t* container_ = nullptr;
  lv_obj_t* status_bar_ = nullptr;
  lv_obj_t* content_ = nullptr;
  lv_obj_t* content_left_ = nullptr;
  lv_obj_t* content_right_ = nullptr;
  lv_obj_t* emotion_label_ = nullptr;
  lv_obj_t* chat_message_label_ = nullptr;
  lv_obj_t* network_label_ = nullptr;
  lv_obj_t* notification_label_ = nullptr;
  lv_obj_t* status_label_ = nullptr;
  lv_obj_t* mute_label_ = nullptr;
};

#endif