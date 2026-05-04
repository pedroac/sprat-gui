#pragma once

#include <QSize>
#include <QTimer>
#include <functional>

namespace WasmResizeDebounce {

constexpr int kDefaultResizeDebounceMs = 200;
constexpr int kMaxResizeDeferrals = 20;  // 4s max with 200ms interval

using ResizeWork = std::function<void(const QSize&, const QSize&)>;

struct State {
    QTimer* timer = nullptr;
    QSize pendingSize;
    QSize pendingOldSize;
    bool deferred = false;
    int deferralCount = 0;
};

void schedule(State& state,
              QObject* owner,
              const QSize& size,
              const QSize& oldSize,
              int intervalMs,
              const char* debugTag,
              ResizeWork work);

} // namespace WasmResizeDebounce
