#pragma once

#include <cstdio>

#include "HeapLog.h"
#include "display/DrawBuffer.h"
#include "screens/IScreen.h"

namespace microreader {

// Stack-based screen manager. Screens are pushed/popped; only the top screen
// is active (receives start/stop and update calls).
class ScreenManager {
 public:
  static constexpr int kMaxDepth = 8;

  // Push a new screen. Stops the current top, starts the new one.
  // The new screen draws into buf; caller handles the actual refresh.
  void push(IScreen* screen, DrawBuffer& buf, IRuntime& runtime) {
    if (depth_ >= kMaxDepth)
      return;
    if (depth_ > 0)
      stack_[depth_ - 1]->stop();
    stack_[depth_++] = screen;
    screen->start(buf, runtime);
  }

  // Pop the top screen(s). Stops the current top, then restarts the new one below.
  void pop(int count, DrawBuffer& buf, IRuntime& runtime) {
    if (count <= 0 || depth_ == 0)
      return;
    if (count > depth_)
      count = depth_;

    stack_[depth_ - 1]->stop();
    HEAP_LOG("pop: after stop");
    depth_ -= count;
    if (depth_ > 0) {
      stack_[depth_ - 1]->start(buf, runtime);
      HEAP_LOG("pop: after prev start");
    }
  }

  void pop(DrawBuffer& buf, IRuntime& runtime) {
    pop(1, buf, runtime);
  }

  // Restart the top screen (stop + start).
  void restart_top(DrawBuffer& buf, IRuntime& runtime) {
    if (depth_ == 0)
      return;
    stack_[depth_ - 1]->stop();
    stack_[depth_ - 1]->start(buf, runtime);
  }

  IScreen* top() const {
    return depth_ > 0 ? stack_[depth_ - 1] : nullptr;
  }
  bool contains(const IScreen* screen) const {
    for (int i = 0; i < depth_; ++i)
      if (stack_[i] == screen)
        return true;
    return false;
  }
  int depth() const {
    return depth_;
  }
  bool empty() const {
    return depth_ == 0;
  }

 private:
  IScreen* stack_[kMaxDepth] = {};
  int depth_ = 0;
};

}  // namespace microreader
