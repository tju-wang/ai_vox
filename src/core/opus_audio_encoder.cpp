#include "opus_audio_encoder.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "clogger/clogger.h"
#ifdef ARDUINO
#include "libopus/opus.h"
#else
#include "opus.h"
#endif
namespace {
constexpr size_t kMaxOpusPacketSize = 1500;

enum MessageType {
  kRequestExit,
  kPcmFrame,
};

struct Message {
  MessageType type;
  void* data;
};

}  // namespace

OpusAudioEncoder::OpusAudioEncoder() {
}

OpusAudioEncoder::~OpusAudioEncoder() {
  Stop();
}

void OpusAudioEncoder::Start(const uint32_t frame_rate,
                             const uint8_t channels,
                             const std::function<void(std::vector<uint8_t>&&)>& handler) {
  CLOG_TRACE();
  if (loop_handle_ != nullptr) {
    return;
  }

  int error = -1;
  opus_encoder_ = opus_encoder_create(frame_rate, channels, OPUS_APPLICATION_VOIP, &error);
  if (opus_encoder_ == nullptr) {
    CLOG("opus_encoder_create failed: %d", error);
    return;
  }

  handler_ = handler;
  message_queue_handle_ = xQueueCreate(5, sizeof(Message));
  opus_encoder_ctl(opus_encoder_, OPUS_SET_DTX(1));
  opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(3));
  xTaskCreate(&OpusAudioEncoder::Loop, "EncoderThread", 4096 * 8, this, tskIDLE_PRIORITY, &loop_handle_);
  CLOG("OK");
}

void OpusAudioEncoder::Stop() {
  CLOG_TRACE();
  if (loop_handle_ == nullptr) {
    return;
  }

  auto sem = xSemaphoreCreateBinary();
  Message message{
      .type = kRequestExit,
      .data = sem,
  };
  xQueueSend(message_queue_handle_, &message, portMAX_DELAY);
  xSemaphoreTake(sem, portMAX_DELAY);
  vSemaphoreDelete(sem);
  loop_handle_ = nullptr;

  while (xQueueReceive(message_queue_handle_, &message, 0)) {
    CLOG("clear pcm frame in queue");
    if (message.type == kPcmFrame) {
      delete reinterpret_cast<std::vector<int16_t>*>(message.data);
    }
  }

  handler_ = nullptr;
  vQueueDelete(message_queue_handle_);
  message_queue_handle_ = nullptr;
  opus_encoder_destroy(opus_encoder_);
  opus_encoder_ = nullptr;
  CLOG("stop OK");
}

IRAM_ATTR bool OpusAudioEncoder::QueueFrameFromISR(std::vector<int16_t>&& frame) {
  auto pcm = new std::vector<int16_t>(std::move(frame));

  Message message{
      .type = kPcmFrame,
      .data = pcm,
  };

  if (message_queue_handle_ == nullptr) {
    goto failed;
  }

  BaseType_t ret;
  if (pdTRUE == xQueueSendFromISR(message_queue_handle_, &message, &ret)) {
    return ret;
  } else {
    // CLOG("queue full");
    goto failed;
  }
failed:
  // CLOG("failed");
  delete pcm;
  return false;
}

void OpusAudioEncoder::Loop(void* self) {
  reinterpret_cast<OpusAudioEncoder*>(self)->Loop();
  vTaskDelete(nullptr);
}

void OpusAudioEncoder::Loop() {
  CLOG_TRACE();
  opus_encoder_ctl(opus_encoder_, OPUS_RESET_STATE);

loop_start:
  Message message;
  if (xQueueReceive(message_queue_handle_, &message, portMAX_DELAY) == pdTRUE) {
    switch (message.type) {
      case kRequestExit: {
        CLOG("kRequestExit");
        xSemaphoreGive(reinterpret_cast<SemaphoreHandle_t>(message.data));
        return;
      }
      case kPcmFrame: {
        auto pcm = reinterpret_cast<std::vector<int16_t>*>(message.data);
        std::vector<uint8_t> data(kMaxOpusPacketSize);
        const auto ret = opus_encode(opus_encoder_, pcm->data(), pcm->size(), data.data(), data.size());
        delete pcm;
        if (ret > 0) {
          data.resize(ret);
          handler_(std::move(data));
        } else {
          CLOG("ret:%d", ret);
        }
      }
      default: {
        break;
      }
    }
  }

  goto loop_start;
}