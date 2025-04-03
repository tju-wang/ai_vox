#include "opus_audio_decoder.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clogger/clogger.h"
#ifdef ARDUINO
#include "libopus/opus.h"
#else
#include "opus.h"
#endif

namespace {
struct Message {
  enum class Type {
    kData,
    kNotify,
    kAbort,
  };

  Type type;
  void* data;
};

enum Status : EventBits_t {
  kAborting = 1 << 1,
  kAbort = 1 << 2,
};
}  // namespace

OpusAudioDecoder::OpusAudioDecoder(const uint32_t sample_rate,
                                   const uint8_t channels,
                                   const uint32_t duration_ms,
                                   const std::function<void(std::vector<int16_t>&&)>& handler)
    :  // message_queue_handle_(xQueueCreate(2, sizeof(std::vector<uint8_t>*))),
      handler_(handler),
      frame_size_(sample_rate / 1000 * channels * duration_ms),
      message_queue_handle_(xQueueCreate(10, sizeof(Message))),
      // mutex_(xSemaphoreCreateMutex()),
      event_group_(xEventGroupCreate()) {
  CLOG_TRACE();
  int error = -1;
  opus_decoder_ = opus_decoder_create(sample_rate, channels, &error);
  handler_ = handler;
  xTaskCreate(&OpusAudioDecoder::Loop, "DecoderThread", 4096 * 4, this, tskIDLE_PRIORITY, nullptr);
  CLOG("OK");
}

OpusAudioDecoder::~OpusAudioDecoder() {
  CLOG_TRACE();
  Ablort();
  WaitAbort();
  opus_decoder_destroy(opus_decoder_);
  ClearQueue();
  vQueueDelete(message_queue_handle_);
  CLOG("OK");
}

void OpusAudioDecoder::Write(std::vector<uint8_t>&& data) {
  if (xEventGroupGetBits(event_group_) & (Status::kAbort | Status::kAborting)) {
    return;
  }
  Message message{
      .type = Message::Type::kData,
      .data = new std::vector<uint8_t>(std::move(data)),
  };
  if (pdPASS != xQueueSend(message_queue_handle_, &message, 0)) {
    delete reinterpret_cast<std::vector<uint8_t>*>(message.data);
  }
}

void OpusAudioDecoder::WaitForDataComsumed() {
  CLOG_TRACE();
  Message message{
      .type = Message::Type::kNotify,
      .data = xSemaphoreCreateBinary(),
  };
  xQueueSend(message_queue_handle_, &message, portMAX_DELAY);
  xSemaphoreTake(reinterpret_cast<SemaphoreHandle_t>(message.data), portMAX_DELAY);
  vSemaphoreDelete(reinterpret_cast<SemaphoreHandle_t>(message.data));
  CLOG("OK");
}

void OpusAudioDecoder::Ablort() {
  CLOG_TRACE();
  xEventGroupSetBits(event_group_, Status::kAborting);
  CLOG("OK");
}

void OpusAudioDecoder::WaitAbort() {
  CLOG_TRACE();
  if (xEventGroupGetBits(event_group_) & Status::kAbort) {
    return;
  }
  Ablort();
  Message message{.type = Message::Type::kAbort, .data = nullptr};
  xQueueSend(message_queue_handle_, &message, portMAX_DELAY);
  xEventGroupWaitBits(event_group_, Status::kAbort, pdFALSE, pdFALSE, portMAX_DELAY);
  CLOG("OK");
}

void OpusAudioDecoder::Loop(void* self) {
  reinterpret_cast<OpusAudioDecoder*>(self)->Loop();
  vTaskDelete(nullptr);
}

void OpusAudioDecoder::Loop() {
  CLOG_TRACE();
Loop:
  Message message;
  if (xQueueReceive(message_queue_handle_, &message, portMAX_DELAY) == pdTRUE) {
    switch (message.type) {
      case Message::Type::kData: {
        std::vector<int16_t> pcm(frame_size_);
        auto data = reinterpret_cast<std::vector<uint8_t>*>(message.data);
        if (xEventGroupGetBits(event_group_) & Status::kAborting) {
          delete data;
          break;
        }
        auto ret = opus_decode(opus_decoder_, data->data(), data->size(), pcm.data(), pcm.size(), 0);
        delete data;
        handler_(std::move(pcm));
        break;
      }
      case Message::Type::kNotify: {
        xSemaphoreGive(reinterpret_cast<SemaphoreHandle_t>(message.data));
        break;
      }
      case Message::Type::kAbort: {
        xEventGroupSetBits(event_group_, Status::kAbort);
        return;
      }
      default: {
        break;
      }
    }
  }
  goto Loop;
}

void OpusAudioDecoder::ClearQueue() {
  CLOG_TRACE();
  assert(xEventGroupGetBits(event_group_) & Status::kAbort);
  Message message;
  while (xQueueReceive(message_queue_handle_, &message, 0) == pdTRUE) {
    CLOG("clear data in queue");
    switch (message.type) {
      case Message::Type::kData: {
        delete reinterpret_cast<std::vector<uint8_t>*>(message.data);
        break;
      }
      default: {
        vSemaphoreDelete(reinterpret_cast<SemaphoreHandle_t>(message.data));
        break;
      }
    }
  }
  CLOG("OK");
}
