#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "ServiceMessages.h"

namespace GameExplorer {

class PreloadWorker final {
public:
    PreloadWorker(std::function<void()> preload,
                  std::function<void(std::exception_ptr)> onError)
        : m_preload(std::move(preload)), m_onError(std::move(onError)) {
        if (!m_preload || !m_onError) {
            throw std::invalid_argument(ServiceMessages::kPreloadCallbacksRequired);
        }
        m_thread = std::thread([this] { Run(); });
    }

    ~PreloadWorker() { Stop(); }

    PreloadWorker(const PreloadWorker&) = delete;
    PreloadWorker& operator=(const PreloadWorker&) = delete;

    [[nodiscard]] bool Request() {
        {
            std::lock_guard lock(m_mutex);
            if (m_stopping) return false;
            m_pending = true;
        }
        m_ready.notify_one();
        return true;
    }

    void Stop() {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
            m_pending = false;
        }
        m_ready.notify_one();
        if (m_thread.joinable()) m_thread.join();
    }

private:
    void Run() {
        for (;;) {
            {
                std::unique_lock lock(m_mutex);
                m_ready.wait(lock, [this] { return m_stopping || m_pending; });
                if (m_stopping) return;
                m_pending = false;
            }
            try {
                m_preload();
            } catch (...) {
                m_onError(std::current_exception());
            }
        }
    }

    std::function<void()> m_preload;
    std::function<void(std::exception_ptr)> m_onError;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    bool m_pending{ false };
    bool m_stopping{ false };
    std::thread m_thread;
};

} // namespace GameExplorer
