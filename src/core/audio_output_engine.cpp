#include "audio_output_engine.h"

#include "clogger/clogger.h"
#ifdef ARDUINO
#include "libopus/opus.h"
#else
#include "opus.h"
#endif

AudioOutputEngine::AudioOutputEngine(const EventHandler& handler) : handler_(handler) {
}

AudioOutputEngine::~AudioOutputEngine() {
  Close();
}

void AudioOutputEngine::Open(std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device) {
  std::lock_guard lock(mutex_);
  if (state_ == State::kRunning) {
    return;
  }
  uint32_t sample_rate = 24000;
  uint32_t channels = 1;
  uint32_t duration_ms = 60;
  frame_size_ = sample_rate / 1000 * channels * duration_ms;

  int error = -1;
  opus_decoder_ = opus_decoder_create(sample_rate, channels, &error);
  assert(opus_decoder_ != nullptr);

  const auto ret = xTaskCreate(&AudioOutputEngine::Loop, "AO", 4096 * 4, this, tskIDLE_PRIORITY, nullptr);
  assert(ret == pdPASS);

  Message message(MessageType::kOpen);
  message.Write(std::move(audio_output_device));
  message_queue_.Send(std::move(message));
  state_ = State::kRunning;
  CLOG("OK");
}

void AudioOutputEngine::Close() {
  std::lock_guard lock(mutex_);
  if (state_ == State::kIdle) {
    return;
  }

  message_queue_.Clear();
  Message message(MessageType::kClose);
  SemaphoreHandle_t sem = xSemaphoreCreateBinary();
  message.Write<uintptr_t>(reinterpret_cast<uintptr_t>(sem));
  message_queue_.Send(std::move(message));
  xSemaphoreTake(sem, portMAX_DELAY);
  vSemaphoreDelete(sem);

  opus_decoder_destroy(opus_decoder_);
  opus_decoder_ = nullptr;
  state_ = State::kIdle;
  CLOG("OK");
}

void AudioOutputEngine::Write(std::vector<uint8_t>&& data) {
  std::lock_guard lock(mutex_);
  if (state_ == State::kIdle) {
    return;
  }
  Message message(MessageType::kData);
  message.Write(std::make_shared<std::vector<uint8_t>>(std::move(data)));
  message_queue_.Send(std::move(message));
}

void AudioOutputEngine::NotifyDataEnd() {
  CLOG_TRACE();
  std::lock_guard lock(mutex_);
  if (state_ == State::kIdle) {
    return;
  }
  message_queue_.Send(MessageType::kDataEnd);
}

void AudioOutputEngine::Loop(void* self) {
  reinterpret_cast<AudioOutputEngine*>(self)->Loop();
  vTaskDelete(nullptr);
}

void AudioOutputEngine::Loop() {
  CLOG("running");
  std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device;

loop_start:
  auto message_opt = message_queue_.Recevie(true);
  if (!message_opt.has_value()) {
    goto loop_start;
  }

  auto message = std::move(*message_opt);

  switch (message.type()) {
    case MessageType::kOpen: {
      CLOG("open");
      audio_output_device = *message.Read<std::shared_ptr<ai_vox::AudioOutputDevice>>();
      audio_output_device->Open(24000);
      break;
    }
    case MessageType::kClose: {
      CLOG("close");
      if (audio_output_device) {
        audio_output_device->Close();
        audio_output_device = nullptr;
      }
      auto sem = reinterpret_cast<SemaphoreHandle_t>(*message.Read<uintptr_t>());
      xSemaphoreGive(sem);
      return;
    }
    case MessageType::kData: {
      auto data = *message.Read<std::shared_ptr<std::vector<uint8_t>>>();
      std::vector<int16_t> pcm(frame_size_);
      auto ret = opus_decode(opus_decoder_, data->data(), data->size(), pcm.data(), pcm.size(), 0);
      if (audio_output_device) {
        audio_output_device->Write(std::move(pcm));
      }
      break;
    }
    case MessageType::kDataEnd: {
      CLOG("data end");
      if (handler_) {
        handler_(Event::kOnDataComsumed);
      }
      break;
    }

    default: {
      break;
    }
  }

  goto loop_start;
}