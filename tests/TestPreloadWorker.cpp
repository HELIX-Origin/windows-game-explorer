#include "TestHarness.h"
#include "../src/service/PreloadWorker.h"

#include <atomic>
#include <future>

namespace {

using namespace std::chrono_literals;

struct ReleaseGuard {
    std::promise<void>& promise;
    ~ReleaseGuard() { if (armed) promise.set_value(); }
    bool armed{ true };
};

} // namespace

TEST_CASE(PreloadWorker_RejectsMissingCallbacks) {
    bool missingWork = false;
    bool missingErrorHandler = false;
    try {
        GameExplorer::PreloadWorker worker({}, [](std::exception_ptr) {});
    } catch (const std::invalid_argument&) {
        missingWork = true;
    }
    try {
        GameExplorer::PreloadWorker worker([] {}, {});
    } catch (const std::invalid_argument&) {
        missingErrorHandler = true;
    }
    ASSERT_TRUE(missingWork);
    ASSERT_TRUE(missingErrorHandler);
}

TEST_CASE(PreloadWorker_IdleStopRejectsRequests) {
    std::atomic<int> scans{ 0 };
    GameExplorer::PreloadWorker worker([&] { ++scans; }, [](std::exception_ptr) {});
    worker.Stop();
    ASSERT_FALSE(worker.Request());
    worker.Stop();
    ASSERT_EQ(scans.load(), 0);
}

TEST_CASE(PreloadWorker_CoalescesBurstAndSerializesScans) {
    std::promise<void> firstStarted;
    auto started = firstStarted.get_future();
    std::promise<void> secondFinished;
    auto finished = secondFinished.get_future();
    std::promise<void> unblock;
    auto unblocked = unblock.get_future().share();
    std::atomic<int> scans{ 0 };
    std::atomic<int> active{ 0 };
    std::atomic<bool> overlapped{ false };
    std::atomic<int> errors{ 0 };

    GameExplorer::PreloadWorker worker([&] {
        if (++active != 1) overlapped = true;
        const int scan = ++scans;
        if (scan == 1) {
            firstStarted.set_value();
            if (unblocked.wait_for(5s) != std::future_status::ready) {
                --active;
                throw std::runtime_error("Timed out waiting to release preload");
            }
        }
        --active;
        if (scan == 2) secondFinished.set_value();
    }, [&](std::exception_ptr) { ++errors; });

    // Release before worker destruction even when an assertion fails.
    ReleaseGuard guard{ unblock };

    ASSERT_TRUE(worker.Request());
    ASSERT_EQ(started.wait_for(5s), std::future_status::ready);
    for (int i = 0; i < 200; ++i) ASSERT_TRUE(worker.Request());
    ASSERT_EQ(scans.load(), 1);
    unblock.set_value();
    guard.armed = false;
    ASSERT_EQ(finished.wait_for(5s), std::future_status::ready);
    worker.Stop();
    ASSERT_EQ(scans.load(), 2);
    ASSERT_EQ(active.load(), 0);
    ASSERT_FALSE(overlapped.load());
    ASSERT_EQ(errors.load(), 0);
}

TEST_CASE(PreloadWorker_ReportsErrorsAndAcceptsSubsequentWork) {
    std::promise<std::exception_ptr> reported;
    auto error = reported.get_future();
    std::promise<void> recovered;
    auto recovery = recovered.get_future();
    std::atomic<int> scans{ 0 };
    GameExplorer::PreloadWorker worker([&] {
        if (++scans == 1) throw std::runtime_error("fixture scan failure");
        recovered.set_value();
    }, [&](std::exception_ptr failure) { reported.set_value(failure); });

    ASSERT_TRUE(worker.Request());
    ASSERT_EQ(error.wait_for(5s), std::future_status::ready);
    bool originalError = false;
    try {
        std::rethrow_exception(error.get());
    } catch (const std::runtime_error& ex) {
        originalError = std::string(ex.what()) == "fixture scan failure";
    }
    ASSERT_TRUE(originalError);
    ASSERT_TRUE(worker.Request());
    ASSERT_EQ(recovery.wait_for(5s), std::future_status::ready);
    worker.Stop();
    ASSERT_EQ(scans.load(), 2);
}

TEST_CASE(PreloadWorker_DestructionJoinsActiveScan) {
    std::promise<void> entered;
    auto started = entered.get_future();
    std::atomic<bool> finished{ false };
    {
        GameExplorer::PreloadWorker worker([&] {
            entered.set_value();
            finished = true;
        }, [](std::exception_ptr) {});
        ASSERT_TRUE(worker.Request());
        ASSERT_EQ(started.wait_for(5s), std::future_status::ready);
    }
    ASSERT_TRUE(finished.load());
}

TEST_CASE(PreloadWorker_StopWaitsForActiveScanAndDiscardsPendingWork) {
    std::promise<void> entered;
    auto started = entered.get_future();
    std::promise<void> unblock;
    auto unblocked = unblock.get_future().share();
    std::atomic<int> scans{ 0 };
    std::atomic<int> errors{ 0 };
    GameExplorer::PreloadWorker worker([&] {
        ++scans;
        entered.set_value();
        if (unblocked.wait_for(5s) != std::future_status::ready) {
            throw std::runtime_error("Timed out waiting to release preload");
        }
    }, [&](std::exception_ptr) { ++errors; });
    std::future<void> stopped;
    ReleaseGuard guard{ unblock };

    ASSERT_TRUE(worker.Request());
    ASSERT_EQ(started.wait_for(5s), std::future_status::ready);
    ASSERT_TRUE(worker.Request());
    stopped = std::async(std::launch::async, [&] { worker.Stop(); });

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    bool rejected = false;
    do {
        rejected = !worker.Request();
        if (rejected) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    ASSERT_TRUE(rejected);
    ASSERT_EQ(stopped.wait_for(0s), std::future_status::timeout);
    unblock.set_value();
    guard.armed = false;
    ASSERT_EQ(stopped.wait_for(5s), std::future_status::ready);
    stopped.get();
    ASSERT_FALSE(worker.Request());
    ASSERT_EQ(scans.load(), 1);
    ASSERT_EQ(errors.load(), 0);
}
