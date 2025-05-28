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
  uint32_t duration_ms = 20;
  frame_size_ = sample_rate / 1000 * channels * duration_ms;

  int error = -1;
  opus_decoder_ = opus_decoder_create(sample_rate, channels, &error);
  assert(opus_decoder_ != nullptr);

  const uint32_t task_heap_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0 ? 1024 * 9 : 1024 * 15;
  const auto ret = xTaskCreate(&AudioOutputEngine::Loop, "AudioOutput", task_heap_size, this, tskIDLE_PRIORITY + 1, nullptr);
  assert(ret == pdPASS);
  if (ret != pdPASS) {
    CLOG("xTaskCreate failed: %d", ret);
    return;
  }

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
#if 0  // debug
  static size_t s_max_size = 0;
  const auto message_queue_size = message_queue_.Size();
  if (message_queue_size > s_max_size) {
    CLOG("message queue size: %zu", message_queue_size);
    CLOG("max message queue size: %zu", s_max_size);
    CLOG("stack high water mark: %d", uxTaskGetStackHighWaterMark(nullptr));
    s_max_size = message_queue_size;
  }
#endif
}

void AudioOutputEngine::NotifyDataEnd() {
  CLOGI();
  std::lock_guard lock(mutex_);
  if (state_ == State::kIdle) {
    return;
  }
  message_queue_.Send(MessageType::kDataEnd);
}

void AudioOutputEngine::Loop(void* self) {
  reinterpret_cast<AudioOutputEngine*>(self)->Loop();
  CLOGD("uxTaskGetStackHighWaterMark: %d", uxTaskGetStackHighWaterMark(nullptr));
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
      const auto ret = opus_decode(opus_decoder_, data->data(), data->size(), pcm.data(), pcm.size(), 0);
      data.reset();
      if (ret >= 0 && audio_output_device) {
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