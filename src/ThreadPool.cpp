#include "rawproc/ThreadPool.h"

namespace rawproc {

ThreadPool::ThreadPool(size_t n) {
    if (n == 0) n = 1;
    threads_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        threads_.emplace_back([this]{ worker(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lk(m_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) if (t.joinable()) t.join();
}

void ThreadPool::worker() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this]{ return stop_ || !q_.empty(); });
            if (stop_ && q_.empty()) return;
            job = std::move(q_.front());
            q_.pop();
        }
        job();
    }
}

} // namespace rawproc

