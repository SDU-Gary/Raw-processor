#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <vector>

namespace rawproc {

class ThreadPool {
public:
    explicit ThreadPool(size_t n = std::thread::hardware_concurrency());
    ~ThreadPool();

    template <class F>
    auto enqueue(F&& f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task->get_future();
        {
            std::unique_lock<std::mutex> lk(m_);
            q_.emplace([task]{ (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    void worker();

    std::mutex m_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> q_;
    std::vector<std::thread> threads_;
    bool stop_ = false;
};

} // namespace rawproc

