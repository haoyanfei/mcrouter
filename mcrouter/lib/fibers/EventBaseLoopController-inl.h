/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>

#include "mcrouter/lib/fibers/EventBaseLoopController.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache { namespace mcrouter {

class EventBaseLoopController::ControllerCallback :
      public folly::EventBase::LoopCallback {
 public:
  explicit ControllerCallback(EventBaseLoopController& controller)
     : controller_(controller) {}

  void runLoopCallback() noexcept override {
    controller_.runLoop();
  }
 private:
  EventBaseLoopController& controller_;
};

inline EventBaseLoopController::EventBaseLoopController()
  : callback_(folly::make_unique<ControllerCallback>(*this)) {
}

inline EventBaseLoopController::~EventBaseLoopController() {
  callback_->cancelLoopCallback();
}

inline void EventBaseLoopController::attachEventBase(
  folly::EventBase& eventBase) {

  if (eventBase_ != nullptr) {
    LOG(ERROR) << "Attempt to reattach EventBase to LoopController";
  }

  eventBase_ = &eventBase;

  eventBaseAttached_ = true;

  if (awaitingScheduling_) {
    schedule();
  }
}

inline void EventBaseLoopController::setFiberManager(FiberManager* fm) {
  fm_ = fm;
}

inline void EventBaseLoopController::schedule() {
  if (eventBase_ == nullptr) {
    // In this case we need to postpone scheduling.
    awaitingScheduling_ = true;
  } else {
    // Schedule it to run in current iteration.
    eventBase_->runInLoop(callback_.get(), true);
    awaitingScheduling_ = false;
  }
}

inline void EventBaseLoopController::cancel() {
  callback_->cancelLoopCallback();
}

inline void EventBaseLoopController::runLoop() {
  fm_->loopUntilNoReady();
}

inline void EventBaseLoopController::scheduleThreadSafe() {
  /* The only way we could end up here is if
     1) Fiber thread creates a fiber that awaits (which means we must
        have already attached, fiber thread wouldn't be running).
     2) We move the promise to another thread (this move is a memory fence)
     3) We fulfill the promise from the other thread. */
  assert(eventBaseAttached_);
  eventBase_->runInEventBaseThread([this] () { runLoop(); });
}

inline void EventBaseLoopController::timedSchedule(std::function<void()> func,
                                                   TimePoint time) {
  assert(eventBaseAttached_);

  // We want upper bound for the cast, thus we just add 1
  auto delay_ms = std::chrono::duration_cast<
    std::chrono::milliseconds>(time - Clock::now()).count() + 1;
  // If clock is not monotonic
  delay_ms = std::max(delay_ms, 0L);
  eventBase_->runAfterDelay(func, delay_ms);
}

}}}  // facebook::memcache::mcrouter
