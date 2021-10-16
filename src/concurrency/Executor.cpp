#include <afina/concurrency/Executor.h>

namespace Afina
{
namespace Concurrency
{

Executor::~Executor()
{
    Stop(true);  // Stopping thread's pool
}

void Executor::Start()
{
    std::unique_lock<std::mutex> _lock(mutex);
    if (state == State::kRun)
    {
        return; //if already running => return
    }
    while (state != State::kStopped) 
    {
        Stop_condition.wait(_lock);
    }
    for (std::size_t i = 0; i < _low_watermark; i++)
    {
        std::thread([this](){ return perform(this); }).detach();
    }
    state = State::kRun; // Running pool
    CountOfThreads = _low_watermark;
    NumOfFreeThreads = _low_watermark;
}

void Executor::Stop(bool await)
{
    std::unique_lock<std::mutex> _lock(mutex);
    if (state == State::kStopped)
    {
        return; //Stopping but not stoped
    }
    if (state == State::kRun) // if is running
    {
        state = State::kStopping; // => Stopping
        Empty_Condition.notify_all(); 
        if (CountOfThreads == 0)
        {
            state = State::kStopped;
        }
    }
    if (await)
    {
        while (state != State::kStopped)
        {
            Stop_condition.wait(_lock); //Stopping
        }
    }
}

} // namespace Concurrency
} // namespace Afina
