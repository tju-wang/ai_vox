#pragma once

#ifndef _AI_VOX_OBSERVER_H_
#define _AI_VOX_OBSERVER_H_

#include <deque>
#include <mutex>
#include <variant>

#include "ai_vox_engine.h"

namespace ai_vox {

class Observer {
 public:
  static constexpr size_t kMaxQueueSize = 10;

  struct StateChangedEvent {
    Engine::State old_state;
    Engine::State new_state;
  };

  struct ChatMessageEvent {
    Engine::Role role;
    std::string content;
  };

  struct ActivationEvent {
    std::string code;
    std::string message;
  };

  struct EmotionEvent {
    std::string emotion;
  };

  using Event = std::variant<StateChangedEvent, ActivationEvent, ChatMessageEvent, EmotionEvent>;

  Observer() = default;
  ~Observer() = default;

  std::deque<Event> PopEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(event_queue_);
  }

  void PushEvent(Event&& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event_queue_.size() >= kMaxQueueSize) {
      event_queue_.pop_front();
    }
    event_queue_.emplace_back(std::move(event));
  }

 private:
  Observer(const Observer&) = delete;
  Observer& operator=(const Observer&) = delete;

  mutable std::mutex mutex_;
  std::deque<Event> event_queue_;
};

}  // namespace ai_vox

#endif