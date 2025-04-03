#include "i2s_std_audio_output_device.h"

namespace ai_vox {
I2sStdAudioOutputDevice::I2sStdAudioOutputDevice(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout)
    : gpio_cfg_({
          .mclk = I2S_GPIO_UNUSED,
          .bclk = bclk,
          .ws = ws,
          .dout = dout,
          .din = I2S_GPIO_UNUSED,
          .invert_flags =
              {
                  .mclk_inv = false,
                  .bclk_inv = false,
                  .ws_inv = false,
              },
      }) {
}

I2sStdAudioOutputDevice::I2sStdAudioOutputDevice(const i2s_std_slot_config_t& slot_cfg, const i2s_std_gpio_config_t& gpio_cfg)
    : slot_cfg_(slot_cfg), gpio_cfg_(gpio_cfg) {
}

bool I2sStdAudioOutputDevice::Open(uint32_t sample_rate) {
  Close();

  i2s_chan_config_t tx_chan_cfg = {
      .id = I2S_NUM_0,
      .role = I2S_ROLE_MASTER,
      .dma_desc_num = 2,
      .dma_frame_num = 1440,
      .auto_clear_after_cb = true,
      .auto_clear_before_cb = false,
      .intr_priority = 0,
  };

  ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &i2s_tx_handle_, nullptr));

  i2s_std_config_t tx_std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
      .slot_cfg = slot_cfg_,
      .gpio_cfg = gpio_cfg_,
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle_, &tx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle_));
  return true;
}

void I2sStdAudioOutputDevice::Close() {
  if (i2s_tx_handle_ == nullptr) {
    return;
  }

  i2s_channel_disable(i2s_tx_handle_);
  i2s_del_channel(i2s_tx_handle_);
  i2s_tx_handle_ = nullptr;
}

I2sStdAudioOutputDevice::~I2sStdAudioOutputDevice() {
  Close();
}

size_t I2sStdAudioOutputDevice::Write(std::vector<int16_t>&& pcm) {
  size_t bytes_written = 0;
  i2s_channel_write(i2s_tx_handle_, pcm.data(), pcm.size() * sizeof(pcm[0]), &bytes_written, 1000);
  return bytes_written;
}

}  // namespace ai_vox