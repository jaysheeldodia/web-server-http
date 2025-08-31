#include "../include/thread_pool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) : stop_flag(false) {
    // Create worker threads
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(&ThreadPool::worker, this);
    }
    
    std::cout << "Thread pool created with " << num_threads << " threads" << std::endl;
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(std::function<void()> task) {
    // Add task to queue with thread safety
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        // Don't add tasks if we're stopping
        if (stop_flag) {
            return;
        }
        
        tasks.push(task);
    }
    
    // Notify one waiting thread that there's work
    condition.notify_one();
}

void ThreadPool::stop() {
    // Set stop flag
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop_flag = true;
    }
    
    // Wake up all threads
    condition.notify_all();
    
    // Wait for all threads to finish
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    std::cout << "Thread pool stopped" << std::endl;
}

size_t ThreadPool::get_thread_count() const {
    return workers.size();
}

size_t ThreadPool::get_queue_size() const {
    std::unique_lock<std::mutex> lock(queue_mutex);
    return tasks.size();
}

void ThreadPool::worker() {
    // Each thread runs this function
    while (true) {
        std::function<void()> task;
        
        // Get next task from queue
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // Wait for work or stop signal
            condition.wait(lock, [this] { 
                return stop_flag || !tasks.empty(); 
            });
            
            // Exit if stopping and no more tasks
            if (stop_flag && tasks.empty()) {
                break;
            }
            
            // Get task from queue
            if (!tasks.empty()) {
                task = tasks.front();
                tasks.pop();
            }
        }
        
        // Execute task outside of lock
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "Worker thread exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Worker thread unknown exception" << std::endl;
            }
        }
    }
}