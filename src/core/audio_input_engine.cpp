#include "audio_input_engine.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef CLOGGER_SEVERITY
#define CLOGGER_SEVERITY CLOGGER_SEVERITY_WARN
#endif

#include "clogger/clogger.h"

#ifdef ARDUINO
#include "libopus/opus.h"
#else
#include "opus.h"
#endif

namespace {
constexpr size_t kMaxOpusPacketSize = 1500;
constexpr uint32_t kFrameDuration = 20;                          // ms
constexpr uint32_t kDefaultSampleRate = 16000;                   // Hz
constexpr uint32_t kDefaultChannels = 1;                         // Mono
constexpr size_t kMaxFrameSize = 16000 / 1000 * kFrameDuration;  // 16000 Hz * 20 ms
}  // namespace

AudioInputEngine::AudioInputEngine(std::shared_ptr<ai_vox::AudioInputDevice> audio_input_device, const AudioInputEngine::DataHandler& handler)
    : handler_(handler), audio_input_device_(std::move(audio_input_device)) {
  int error = 0;
  opus_encoder_ = opus_encoder_create(kDefaultSampleRate, kDefaultChannels, OPUS_APPLICATION_VOIP, &error);
  assert(opus_encoder_ != nullptr);
  if (opus_encoder_ == nullptr) {
    CLOG("opus_encoder_create failed: %d", error);
    abort();
    return;
  }

  opus_encoder_ctl(opus_encoder_, OPUS_SET_DTX(1));
  opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(0));
  opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(8000));

  audio_input_device_->Open(kDefaultSampleRate);
  task_queue_ = new TaskQueue("AudioInput", heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0 ? 1024 * 18 : 1024 * 25, tskIDLE_PRIORITY + 1);
  task_queue_->Enqueue([this]() { PullData(); });
  CLOGI("OK");
}

AudioInputEngine::~AudioInputEngine() {
  delete task_queue_;
  audio_input_device_->Close();
  opus_encoder_destroy(opus_encoder_);
  CLOG("OK");
}

void AudioInputEngine::PullData() {
  auto pcm = audio_input_device_->Read(kMaxFrameSize);
  std::vector<uint8_t> data(kMaxOpusPacketSize);
  const auto ret = opus_encode(opus_encoder_, pcm.data(), pcm.size(), data.data(), data.size());
  if (ret > 0) {
    data.resize(ret);  // Resize to the actual size of the encoded data
    handler_(std::move(data));
  } else {
    CLOGE("opus_encode failed with: %d", ret);
  }

  task_queue_->Enqueue([this]() { PullData(); });
}