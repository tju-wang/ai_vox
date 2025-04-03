#pragma once

#ifndef _AUDIO_INPUT_DEVICE_H_
#define _AUDIO_INPUT_DEVICE_H_

#include <cstdint>
#include <vector>

namespace ai_vox {
class AudioInputDevice {
 public:
  virtual ~AudioInputDevice() = default;
  virtual bool Open(uint32_t sample_rate) = 0;
  virtual void Close() = 0;
  virtual std::vector<int16_t> Read(uint32_t samples) = 0;
};
}  // namespace ai_vox

#endif