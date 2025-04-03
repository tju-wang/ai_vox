#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/aes.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../audio_input_device.h"
#include "../audio_output_device.h"

class AudioInputEngine;
class AudioOutputEngine;

class AudioSession {
 public:
  enum class Event : uint8_t {
    kOnOutputDataComsumed = 1 << 0,
  };

  using EventHandler = std::function<void(Event)>;

  AudioSession(const std::string& host,
               const uint16_t port,
               const std::string& aes_key,
               const std::string& nonce,
               const std::string& session_id,
               const EventHandler& handler);
  virtual ~AudioSession();
  bool Open();
  bool Close();
  void OpenAudioInput(std::shared_ptr<ai_vox::AudioInputDevice> audio_input_device);
  void CloseAudioInput();
  void OpenAudioOutput(std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device);
  void CloseAudioOutput();
  void NotifyOutputDataEnd();

  const std::string& session_id() const {
    return session_id_;
  }

 private:
  static void RecevieLoop(void* self);
  static void Loop(void* self);
  void RecevieLoop();
  void Loop();
  void OnTransmit(std::vector<uint8_t>&& data);

  // State state_ = State::kIdle;
  int udp_fd_ = -1;
  const std::string host_;
  const uint16_t port_;
  const std::vector<uint8_t> aes_key_;
  const std::vector<uint8_t> aes_nonce_;
  const std::string session_id_;
  mbedtls_aes_context aes_ctx_;
  uint32_t send_sequence_ = 0;
  uint32_t expected_sequence_ = 1;
  SemaphoreHandle_t sem_ = nullptr;
  std::shared_ptr<AudioInputEngine> audio_input_stream_;
  // std::shared_ptr<AudioOutputStream> audio_output_stream_;
  std::shared_ptr<AudioOutputEngine> audio_output_stream_;
  EventHandler handler_;
  // SemaphoreHandle_t mutex_ = nullptr;
  // QueueHandle_t queue_ = nullptr;
};