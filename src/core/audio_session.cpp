#include "audio_session.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#include "audio_input_engine.h"
#include "audio_output_engine.h"

#ifndef CLOGGER_SEVERITY
#define CLOGGER_SEVERITY CLOGGER_SEVERITY_WARN
#endif
#include "clogger/clogger.h"

namespace {
std::vector<uint8_t> HexStringToBytes(const std::string& str) {
  std::vector<uint8_t> result;
  // 检查字符串长度是否为偶数
  if (str.size() % 2 != 0) {
    return result;
  }

  // 辅助函数：将单个字符转换为对应的半字节
  auto char_to_nibble = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0xFF;  // 无效字符标记
  };

  result.reserve(str.size() / 2);

  for (size_t i = 0; i < str.size(); i += 2) {
    uint8_t high = char_to_nibble(str[i]);
    uint8_t low = char_to_nibble(str[i + 1]);

    // 检查字符有效性
    if (high == 0xFF || low == 0xFF) {
      return std::vector<uint8_t>();  // 返回空数组表示错误
    }

    result.push_back((high << 4) | low);
  }

  return result;
}
}  // namespace

AudioSession::AudioSession(const std::string& host,
                           const uint16_t port,
                           const std::string& aes_key,
                           const std::string& aes_nonce,
                           const std::string& session_id,
                           const EventHandler& handler)
    : host_(host),
      port_(port),
      aes_key_(HexStringToBytes(aes_key)),
      aes_nonce_(HexStringToBytes(aes_nonce)),
      session_id_(session_id),
      audio_output_stream_(std::make_shared<AudioOutputEngine>([this](AudioOutputEngine::Event event) {
        if (event == AudioOutputEngine::Event::kOnDataComsumed) {
          handler_(Event::kOnOutputDataComsumed);
        }
      })),
      handler_(handler) {
  CLOG("host: %s", host_.c_str());
  CLOG("port: %u", port_);
  CLOG("aes_key: %s", aes_key.c_str());
  CLOG("aes_nonce: %s", aes_nonce.c_str());
  CLOG("session_id: %s", session_id.c_str());

  mbedtls_aes_init(&aes_ctx_);
  mbedtls_aes_setkey_enc(&aes_ctx_, aes_key_.data(), 128);
}

AudioSession::~AudioSession() {
  Close();
}

bool AudioSession::Open() {
  struct sockaddr_in server_addr;
  bzero(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_);

  struct hostent* server = gethostbyname(host_.c_str());
  memcpy(&server_addr.sin_addr, server->h_addr, server->h_length);

  udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_fd_ < 0) {
    return false;
  }

  int ret = connect(udp_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (ret < 0) {
    CLOG("Failed to connect to %s:%d", host_.c_str(), port_);
    close(udp_fd_);
    udp_fd_ = -1;
    return false;
  }

  sem_ = xSemaphoreCreateBinary();

  const auto task_create_ret = xTaskCreate(RecevieLoop, "RecevieLoop", 1024 * 3, this, tskIDLE_PRIORITY + 1, nullptr);
  assert(task_create_ret == pdPASS);
  return true;
}

bool AudioSession::Close() {
  audio_input_stream_.reset();

  if (udp_fd_ != -1) {
    close(udp_fd_);
    udp_fd_ = -1;
  }

  if (sem_ != nullptr) {
    xSemaphoreTake(sem_, portMAX_DELAY);
    vSemaphoreDelete(sem_);
    sem_ = nullptr;
  }

  return true;
}

void AudioSession::OpenAudioInput(std::shared_ptr<ai_vox::AudioInputDevice> audio_input_device) {
  if (audio_input_stream_) {
    return;
  }

  audio_input_stream_ =
      std::make_shared<AudioInputEngine>(std::move(audio_input_device), std::bind(&AudioSession::OnTransmit, this, std::placeholders::_1));
}

void AudioSession::CloseAudioInput() {
  audio_input_stream_.reset();
}

void AudioSession::OpenAudioOutput(std::shared_ptr<ai_vox::AudioOutputDevice> audio_output_device) {
  audio_output_stream_->Open(std::move(audio_output_device));
}

void AudioSession::CloseAudioOutput() {
  audio_output_stream_->Close();
}

void AudioSession::NotifyOutputDataEnd() {
  audio_output_stream_->NotifyDataEnd();
}

void AudioSession::RecevieLoop(void* self) {
  reinterpret_cast<AudioSession*>(self)->RecevieLoop();
  vTaskDelete(nullptr);
}

void AudioSession::RecevieLoop() {
  while (true) {
    std::vector<uint8_t> data(1500);
    int ret = recv(udp_fd_, data.data(), data.size(), 0);
    if (ret < 0) {
      xSemaphoreGive(sem_);
#if 0
      CLOG("uxTaskGetStackHighWaterMark: %d", uxTaskGetStackHighWaterMark(nullptr));
#endif
      return;
    }
    data.resize(ret);
    if (data.size() < aes_nonce_.size()) {
      continue;
    }

    if (data[0] != 0x01) {
      continue;
    }

    uint32_t sequence = ntohl(*(uint32_t*)&data[12]);

    if (expected_sequence_ != sequence) {
      CLOGW("sequence mismatch. expected sequence_: %u, sequence: %u, gap: %" PRId64,
           expected_sequence_,
           sequence,
           static_cast<int64_t>(sequence) - static_cast<int64_t>(expected_sequence_));
    }

    expected_sequence_ = sequence + 1;
#if 1
    std::vector<uint8_t> decrypted;
    size_t decrypted_size = data.size() - aes_nonce_.size();
    size_t nc_off = 0;
    uint8_t stream_block[16] = {0};
    decrypted.resize(decrypted_size);
    auto nonce = data.data();
    auto encrypted = data.data() + aes_nonce_.size();
    ret = mbedtls_aes_crypt_ctr(&aes_ctx_, decrypted_size, &nc_off, nonce, stream_block, encrypted, decrypted.data());
    // xSemaphoreTake(mutex_, portMAX_DELAY);
    audio_output_stream_->Write(std::move(decrypted));
    // xSemaphoreGive(mutex_);
#else
    Message message{
        .type = Message::Type::kData,
        .data = new std::vector<uint8_t>(std::move(data)),
    };
    xQueueSend(queue_, &message, portMAX_DELAY);
#endif
  }
}

void AudioSession::OnTransmit(std::vector<uint8_t>&& data) {
  std::vector<uint8_t> nonce(aes_nonce_);
  *(uint16_t*)&nonce[2] = htons(data.size());
  *(uint32_t*)&nonce[12] = htonl(++send_sequence_);

  std::vector<uint8_t> encrypted(nonce.size() + data.size());
  memcpy(encrypted.data(), nonce.data(), nonce.size());

  size_t nc_off = 0;
  uint8_t stream_block[16] = {0};
  if (mbedtls_aes_crypt_ctr(&aes_ctx_, data.size(), &nc_off, nonce.data(), stream_block, data.data(), encrypted.data() + nonce.size()) != 0) {
    CLOG("mbedtls_aes_crypt_ctr failed");
    return;
  }

  auto ret = send(udp_fd_, encrypted.data(), encrypted.size(), 0);
  if (ret <= 0) {
    CLOGE("send faied. Error:%s (%d)", strerror(ret), ret);
    return;
  }

  return;
}