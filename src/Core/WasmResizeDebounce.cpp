#include "WasmResizeDebounce.h"
#include <QtGlobal>
#ifdef Q_OS_WASM
#include "ViewUtils.h"
#include <QDebug>

namespace {

bool wasmResizeDebugEnabled() {
    static int enabled = -1;
    if (enabled == -1) {
#ifdef SPRAT_WASM_RESIZE_DEBUG
        enabled = 1;
#else
        enabled = qEnvironmentVariableIsSet("SPRAT_WASM_RESIZE_DEBUG") ? 1 : 0;
#endif
    }
    return enabled == 1;
}

void logResize(const char* tag, const char* message) {
    if (!wasmResizeDebugEnabled()) {
        return;
    }
    qInfo().noquote() << "[sprat][resize]" << tag << message;
}

} // namespace
#endif

namespace WasmResizeDebounce {

void schedule(State& state,
              QObject* owner,
              const QSize& size,
              const QSize& oldSize,
              int intervalMs,
              const char* debugTag,
              ResizeWork work) {
#ifdef Q_OS_WASM
    state.pendingSize = size;
    state.pendingOldSize = oldSize;

    if (!state.timer) {
        state.timer = new QTimer(owner);
        state.timer->setSingleShot(true);
        state.timer->setInterval(intervalMs);
        QObject::connect(state.timer, &QTimer::timeout, owner, [&state, owner, debugTag, work]() {
            // Check if async work is in progress or JSPI is available. If so, defer and reschedule.
            // Note: jsHaveJspi() returns true if the browser supports JSPI (Chrome 120+),
            // not if JSPI is active. This causes permanent deferral on JSPI-capable browsers,
            // but prevents the Asyncify assertion crash from Qt's internal VisualViewport handler.
            if (jsIsAsyncBusy() || jsHaveJspi()) {
                if (!state.deferred) {
                    state.deferred = true;
                    state.deferralCount = 0;
                    logResize(debugTag, "deferred");
                }
                // Increment deferral count and check if we've exceeded max.
                ++state.deferralCount;
                if (state.deferralCount >= kMaxResizeDeferrals) {
                    logResize(debugTag, "max deferrals reached, giving up");
                    state.deferred = false;
                    state.deferralCount = 0;
                    // Give up - let the next resize event handle it
                    return;
                }
                state.timer->start();
                return;
            }

            if (state.deferred) {
                state.deferred = false;
                state.deferralCount = 0;
                logResize(debugTag, "replay");
            }

            // Post work as a deferred call instead of executing immediately.
            // This avoids triggering Qt's async resize handling from within an active async context.
            QTimer::singleShot(0, owner, [size = state.pendingSize, oldSize = state.pendingOldSize, work]() {
                work(size, oldSize);
            });
        });
    }

    state.timer->start();
#else
    Q_UNUSED(state);
    Q_UNUSED(owner);
    Q_UNUSED(size);
    Q_UNUSED(oldSize);
    Q_UNUSED(intervalMs);
    Q_UNUSED(debugTag);
    work(size, oldSize);
#endif
}

} // namespace WasmResizeDebounce
