  
#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <chrono>

namespace Afina {
namespace Concurrency {

/**
 * # Thread pool
 */
class Executor {
public:
    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };

    Executor(std::string name,
             std::size_t low_watermark = 4,
             std::size_t high_watermark = 8,
             std::size_t max_queue_size = 64,
             std::size_t idle_time = 1024)
             : _name(name)
             , _low_watermark(low_watermark)
             , _high_watermark(high_watermark)
             , _max_queue_size(max_queue_size)
             , _idle_time(idle_time)
             , state(State::kStopped) {}

    ~Executor();

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */
    void Stop(bool await = false);
    void Start();

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
        // Prepare "task"
        auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

        std::unique_lock<std::mutex> _lock(this->mutex);
        if (state != State::kRun || tasks.size() >= _max_queue_size) {
            return false;
        }

        // Enqueue new task
        tasks.push_back(exec);
        if (tasks.size() > free_threads_count && threads_count < _high_watermark) {
            threads_count++;
            free_threads_count++;
            std::thread([this](){ return perform(this); }).detach();
        }
        empty_condition.notify_one();
        return true;
    }

    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    friend void perform(Executor *executor) {
        std::unique_lock<std::mutex> _lock(executor->mutex);

        while (true) {
            while (executor->tasks.empty() && executor->state == State::kRun) {
                if (executor->empty_condition.wait_for(_lock, std::chrono::milliseconds(executor->_idle_time)) ==
                    std::cv_status::timeout && executor->threads_count > executor->_low_watermark) {
                    executor->threads_count--;
                    executor->free_threads_count--;
                    executor->stopping_condition.notify_all();
                    return;
                }
            }
            if (!executor->tasks.empty()) {
                executor->free_threads_count--;
                auto task = executor->tasks.front();
                executor->tasks.pop_front();
                _lock.unlock();
                task();
                _lock.lock();
                executor->free_threads_count++;
            } else if (executor->state == State::kStopping) {
                if (!(--executor->threads_count)) {
                    executor->state = State::kStopped;
                    executor->stopping_condition.notify_all();
                }
                return;
            }
        } // while (true)
    }

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    /**
     * Mutex to protect state below from concurrent modification
     */
    std::mutex mutex;

    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable empty_condition;

    std::condition_variable stopping_condition;

    /**
     * Size of actual threads that perform execution
     */
    std::size_t threads_count;
    std::size_t free_threads_count;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> tasks;

    /**
     * Flag to stop bg threads
     */
    State state;

    const std::string _name;
    const std::size_t _low_watermark;
    const std::size_t _high_watermark;
    const std::size_t _max_queue_size;
    const std::size_t _idle_time;
};

} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
