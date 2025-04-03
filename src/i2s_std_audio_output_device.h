#pragma once

#ifndef _I2S_STD_AUDIO_OUTPUT_DEVICE_H_
#define _I2S_STD_AUDIO_OUTPUT_DEVICE_H_

#include <driver/i2s_std.h>

#include "audio_output_device.h"

namespace ai_vox {
class I2sStdAudioOutputDevice : public AudioOutputDevice {
 public:
  I2sStdAudioOutputDevice(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout);
  I2sStdAudioOutputDevice(const i2s_std_slot_config_t& slot_cfg, const i2s_std_gpio_config_t& gpio_cfg);
  ~I2sStdAudioOutputDevice();

 private:
  bool Open(uint32_t sample_rate) override;
  void Close() override;
  size_t Write(std::vector<int16_t>&& pcm);

  i2s_chan_handle_t i2s_tx_handle_ = nullptr;
  i2s_std_slot_config_t slot_cfg_ = {
      .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
      .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
      .slot_mode = I2S_SLOT_MODE_MONO,
      .slot_mask = I2S_STD_SLOT_BOTH,
      .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
      .ws_pol = false,
      .bit_shift = true,
  };
  i2s_std_gpio_config_t gpio_cfg_;
};
}  // namespace ai_vox

#endif