#include "audio_input_engine.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clogger/clogger.h"

#ifdef ARDUINO
#include "libopus/opus.h"
#else
#include "opus.h"
#endif

namespace {
constexpr size_t kMaxOpusPacketSize = 1500;
constexpr size_t kTaskStackSize = 4096 * 8;
}  // namespace

AudioInputEngine::AudioInputEngine(std::shared_ptr<ai_vox::AudioInputDevice> audio_input_device, const AudioInputEngine::DataHandler& handler)
    : audio_input_device_(std::move(audio_input_device)), handler_(handler), task_stack_(new StackType_t[kTaskStackSize]) {
  assert(task_stack_ != nullptr);
  uint32_t frame_rate = 16000;
  uint32_t channels = 1;
  int error = 0;
  opus_encoder_ = opus_encoder_create(frame_rate, channels, OPUS_APPLICATION_VOIP, &error);
  assert(opus_encoder_ != nullptr);
  if (opus_encoder_ == nullptr) {
    CLOG("opus_encoder_create failed: %d", error);
    return;
  }

  opus_encoder_ctl(opus_encoder_, OPUS_SET_DTX(1));
  opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(3));

  audio_input_device_->Open(frame_rate);

  xTaskCreateStatic(Loop, "Loop", kTaskStackSize, this, tskIDLE_PRIORITY, task_stack_, &task_buffer_);
  CLOG("OK");
}

AudioInputEngine::~AudioInputEngine() {
  Message message(MessageType::kClose);
  SemaphoreHandle_t sem = xSemaphoreCreateBinary();
  message.Write<uintptr_t>(reinterpret_cast<uintptr_t>(sem));
  message_queue_.Send(std::move(message));
  xSemaphoreTake(sem, portMAX_DELAY);
  vSemaphoreDelete(sem);

  audio_input_device_->Close();
  opus_encoder_destroy(opus_encoder_);
  delete[] task_stack_;
  CLOG("OK");
}

void AudioInputEngine::Loop(void* self) {
  reinterpret_cast<AudioInputEngine*>(self)->Loop();
  vTaskDelete(nullptr);
}

void AudioInputEngine::Loop() {
  CLOG("running");
loop_start:
  auto message = message_queue_.Recevie(false);
  if (message.has_value()) {
    switch (message->type()) {
      case MessageType::kClose: {
        CLOG("close");
        auto sem_opt = message->Read<uintptr_t>();
        if (sem_opt.has_value()) {
          auto sem = reinterpret_cast<SemaphoreHandle_t>(*sem_opt);
          xSemaphoreGive(sem);
        }
        return;
      }
    }
  } else {
    auto pcm = audio_input_device_->Read(16000 / 1000 * 60);
    std::vector<uint8_t> data(kMaxOpusPacketSize);
    const auto ret = opus_encode(opus_encoder_, pcm.data(), pcm.size(), data.data(), data.size());
    if (ret > 0) {
      data.resize(ret);
      handler_(std::move(data));
    } else {
      CLOG("ret:%d", ret);
    }
  }

  goto loop_start;
}