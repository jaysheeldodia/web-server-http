// Enhanced thread_pool.cpp with coordinated shutdown

#include "../include/thread_pool.h"
#include "../include/shutdown_coordinator.h"
#include <iostream>
#include <future>
#include <chrono>

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
    // Don't accept new tasks if stopping
    if (stop_flag.load()) {
        return;
    }
    
    // Add task to queue with thread safety and timeout
    {
        std::unique_lock<std::mutex> lock(queue_mutex, std::try_to_lock);
        
        if (!lock.owns_lock()) {
            // If we can't get the lock quickly during shutdown, drop the task
            if (ShutdownCoordinator::instance().is_shutdown_requested()) {
                return;
            }
            // Otherwise, block and wait for the lock
            lock.lock();
        }
        
        if (stop_flag.load()) {
            return;
        }
        
        tasks.push(std::move(task));
    }
    
    // Notify one waiting thread that there's work
    condition.notify_one();
}

void ThreadPool::stop() {
    // Set stop flag first
    stop_flag.store(true);
    
    // Wake up all threads
    condition.notify_all();
    
    // Use coordinated shutdown
    auto& coordinator = ShutdownCoordinator::instance();
    coordinator.request_shutdown();
    
    std::cout << "Stopping thread pool..." << std::endl;
    
    // Wait for workers to finish with timeout
    const auto timeout = std::chrono::seconds(3);
    auto start_time = std::chrono::steady_clock::now();
    
    for (auto& worker : workers) {
        if (worker.joinable()) {
            auto remaining_time = timeout - (std::chrono::steady_clock::now() - start_time);
            
            if (remaining_time > std::chrono::milliseconds(0)) {
                // Try to join with remaining timeout
                auto future = std::async(std::launch::async, [&worker]() {
                    worker.join();
                });
                
                if (future.wait_for(remaining_time) == std::future_status::ready) {
                    // Thread joined successfully
                    continue;
                }
            }
            
            // Thread didn't join in time, detach it
            std::cout << "Worker thread timeout - detaching" << std::endl;
            worker.detach();
        }
    }
    
    // Clear task queue
    {
        std::unique_lock<std::mutex> lock(queue_mutex, std::try_to_lock);
        
        if (lock.owns_lock()) {
            std::queue<std::function<void()>> empty_queue;
            tasks.swap(empty_queue);
        } else {
            std::cout << "Warning: Could not clear task queue" << std::endl;
        }
    }
    
    std::cout << "Thread pool stopped" << std::endl;
}

size_t ThreadPool::get_thread_count() const {
    return workers.size();
}

size_t ThreadPool::get_queue_size() const {
    std::unique_lock<std::mutex> lock(queue_mutex, std::try_to_lock);
    
    if (lock.owns_lock()) {
        return tasks.size();
    }
    
    // If we can't get the lock quickly, return approximate size
    return 0;
}

void ThreadPool::worker() {
    auto& coordinator = ShutdownCoordinator::instance();
    
    // Each thread runs this function
    while (!stop_flag.load() && !coordinator.is_shutdown_requested()) {
        std::function<void()> task;
        
        // Get next task from queue
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // Wait for work or stop signal with timeout
            bool has_work = condition.wait_for(lock, std::chrono::milliseconds(100), 
                                             [this, &coordinator] { 
                                                 return stop_flag.load() || 
                                                        coordinator.is_shutdown_requested() || 
                                                        !tasks.empty(); 
                                             });
            
            // Exit if stopping
            if (stop_flag.load() || coordinator.is_shutdown_requested()) {
                break;
            }
            
            // Get task from queue if available
            if (has_work && !tasks.empty()) {
                task = std::move(tasks.front());
                tasks.pop();
            } else {
                continue; // No task available, continue waiting
            }
        }
        
        // Execute task outside of lock
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                if (!coordinator.is_shutdown_requested()) {
                    std::cerr << "Worker thread exception: " << e.what() << std::endl;
                }
            } catch (...) {
                if (!coordinator.is_shutdown_requested()) {
                    std::cerr << "Worker thread unknown exception" << std::endl;
                }
            }
        }
        
        // Check if we should exit early due to shutdown
        if (coordinator.is_shutdown_requested()) {
            break;
        }
    }
    
    // Notify coordinator that this thread is exiting
    coordinator.thread_exiting();
}