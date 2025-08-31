#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    // Constructor: create pool with specified number of threads
    ThreadPool(size_t num_threads);
    
    // Destructor: stop all threads and clean up
    ~ThreadPool();
    
    // Add a task to the queue
    void enqueue(std::function<void()> task);
    
    // Stop the thread pool
    void stop();
    
    // Get number of active threads
    size_t get_thread_count() const;
    
    // Get number of pending tasks
    size_t get_queue_size() const;

private:
    // Worker threads
    std::vector<std::thread> workers;
    
    // Task queue
    std::queue<std::function<void()>> tasks;
    
    // Synchronization
    mutable std::mutex queue_mutex;           // Protects the task queue
    std::condition_variable condition; // Wakes up sleeping threads
    
    // Control flags
    std::atomic<bool> stop_flag;      // Signal to stop threads
    
    // Worker function that each thread runs
    void worker();
};

#endif // THREAD_POOL_H