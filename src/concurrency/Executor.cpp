#include <afina/concurrency/Executor.h>

namespace Afina {
namespace Concurrency {

Executor::~Executor() {
    Stop(true);
}

void Executor::Start() {
    std::unique_lock<std::mutex> _lock(mutex);
    if (state == State::kRun) {
        return;
    }
    while (state != State::kStopped) {
        stopping_condition.wait(_lock);
    }
    for (std::size_t i = 0; i < _low_watermark; i++) {
        std::thread([this](){ return perform(this); }).detach();
    }
    state = State::kRun;
    threads_count = _low_watermark;
    free_threads_count = _low_watermark;
}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> _lock(mutex);
    if (state == State::kStopped) {
        return;
    }
    if (state == State::kRun) {
        state = State::kStopping;
        empty_condition.notify_all();
        if (threads_count == 0) {
            state = State::kStopped;
        }
    }
    if (await) {
        while (state != State::kStopped) {
            stopping_condition.wait(_lock);
        }
    }
}

} // namespace Concurrency
} // namespace Afina
