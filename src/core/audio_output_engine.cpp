#include "audio_output_engine.h"

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
constexpr uint32_t kDefaultSampleRate = 24000;
constexpr uint32_t kDefaultChannels = 1;
constexpr uint32_t kDefaultDurationMs = 20;  // Duration in milliseconds
constexpr uint32_t kDefaultFrameSize = kDefaultSampleRate / 1000 * kDefaultChannels * kDefaultDurationMs;
}  // namespace

AudioOutputEngine::AudioOutputEngine(std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device)
    : audio_output_device_(std::move(audio_output_device)) {
  int error = -1;
  opus_decoder_ = opus_decoder_create(kDefaultSampleRate, kDefaultChannels, &error);
  assert(opus_decoder_ != nullptr);
  audio_output_device_->Open(kDefaultSampleRate);
  task_queue_ = new TaskQueue("AudioOutput", heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0 ? 1024 * 9 : 1024 * 15, tskIDLE_PRIORITY + 1);
  CLOGI("OK");
}

AudioOutputEngine::~AudioOutputEngine() {
  delete task_queue_;
  audio_output_device_->Close();
  opus_decoder_destroy(opus_decoder_);
}

void AudioOutputEngine::Write(std::vector<uint8_t>&& data) {
  task_queue_->Enqueue([this, data = std::move(data)]() mutable { ProcessData(std::move(data)); });
}

void AudioOutputEngine::NotifyDataEnd(std::function<void()>&& callback) {
  task_queue_->Enqueue(std::move(callback));
}

void AudioOutputEngine::ProcessData(std::vector<uint8_t>&& data) {
  std::vector<int16_t> pcm(kDefaultFrameSize);
  const auto ret = opus_decode(opus_decoder_, data.data(), data.size(), pcm.data(), pcm.size(), 0);
  if (ret >= 0) {
    audio_output_device_->Write(std::move(pcm));
  }
}