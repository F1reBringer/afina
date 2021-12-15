  
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
#include <iostream>

namespace Afina {
namespace Concurrency {

/**
 * # Thread pool
 */
class Executor
{
public:
 //  enum State must be public
    enum class State
    {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun, 

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadpool is stopped
        kStopped
    };

    Executor(std::string name,
             std::size_t low_watermark = 2, //Min Count of threads in pool
             std::size_t high_watermark = 10, //Max Count of threads in pool
             std::size_t max_queue_size = 50, // Max size of Queue
             std::size_t idle_time = 2048) // Maximum task waiting time
             : _name(std::move(name))
             , _low_watermark(low_watermark) 
             , _high_watermark(high_watermark) //Constructor
             , _max_queue_size(max_queue_size)
             , _idle_time(idle_time)
             , state(State::kStopped)
              {}

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
    template <typename F, typename... Types> bool Execute(F &&func, Types... args)
    {
        // Prepare "task"
        auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);
        std::unique_lock<std::mutex> _lock(this->mutex);
        if (state != State::kRun || tasks.size() >= _max_queue_size) // Cannot add new task
        {
            return false;
        }

        // Enqueue new task
        tasks.push_back(exec);
        if (NumOfFreeThreads == 0) // creating of new thread
        {
            CountOfThreads++;
            NumOfFreeThreads++;
            std::thread([this](){ return perform(this); }).detach();
        }
        Empty_Condition.notify_one(); 
        return true;
    }



    /**
    * Main function that all pool threads are running. It polls internal task queue and execute tasks
    */
    friend void perform(Executor *executor)
    {
        std::unique_lock<std::mutex> _lock(executor->mutex); 

        while (true)
        {
            auto Begin = std::chrono::steady_clock::now();
            while (executor->tasks.empty() && executor->state == State::kRun) // When we have no tasks and Executor already running
            {

                if (executor->Empty_Condition.wait_until(_lock, Begin + std::chrono::milliseconds(executor->_idle_time))== std::cv_status::timeout // timeout idle_time
                && executor->CountOfThreads > executor->_low_watermark) // NumOfThreads must be bigger than low_watermark
                {
                    executor->CountOfThreads--; //delete one of threads
                    executor->NumOfFreeThreads--; // delete on of threads
                    return;
                }
            }
            if (!executor->tasks.empty()) // if we have task in pool => processing of task
            {
                executor->NumOfFreeThreads--; // one thread will be busy
                auto task = executor->tasks.front(); 
                executor->tasks.pop_front(); 
                _lock.unlock();
                
                
            	try
            	{
            		task();
            	} 
            	catch(...)
            	{
                	std::cout << "Detected exception, Error!" << std::endl;
            	}
               _lock.lock();
               executor->NumOfFreeThreads++; // this thread is free now
            }
            else if (executor->state == State::kStopping) //if we have no tasks and state=stopping
            {
                if (!(--executor->CountOfThreads))
                {
                    executor->state = State::kStopped; // Stopping pool
                    executor->Stop_condition.notify_all();
                }
                return;
            }
        }
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
    std::condition_variable Empty_Condition; 

    //Condition variable  = Condition, which needs for stopping thread's pool 
    std::condition_variable Stop_condition;

    /**
     * Size of actual threads that perform execution
     I removed vector of threads and replace it on two variable size: CountOfThreads and NumOfFreeThreads
     */
    std::size_t CountOfThreads;
    std::size_t NumOfFreeThreads;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> tasks; // Queue of tasks

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
