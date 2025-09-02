#ifndef SHUTDOWN_COORDINATOR_H
#define SHUTDOWN_COORDINATOR_H

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>

class ShutdownCoordinator {
private:
    std::atomic<bool> shutdown_requested{false};
    std::atomic<int> active_threads{0};
    
    mutable std::mutex shutdown_mutex;
    std::condition_variable shutdown_cv;
    std::condition_variable all_threads_stopped_cv;
    
    // Thread registry for coordinated shutdown
    std::vector<std::weak_ptr<std::thread>> registered_threads;
    mutable std::mutex thread_registry_mutex;
    
public:
    // Singleton pattern for global access
    static ShutdownCoordinator& instance() {
        static ShutdownCoordinator instance;
        return instance;
    }
    
    // Request shutdown and notify all waiting threads
    void request_shutdown() {
        {
            std::lock_guard<std::mutex> lock(shutdown_mutex);
            shutdown_requested.store(true);
        }
        shutdown_cv.notify_all();
    }
    
    // Check if shutdown has been requested
    bool is_shutdown_requested() const {
        return shutdown_requested.load();
    }
    
    // Wait for shutdown request with timeout
    template<typename Rep, typename Period>
    bool wait_for_shutdown(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(shutdown_mutex);
        return shutdown_cv.wait_for(lock, timeout, [this]() {
            return shutdown_requested.load();
        });
    }
    
    // Register a thread for coordinated shutdown
    void register_thread(std::shared_ptr<std::thread> thread) {
        std::lock_guard<std::mutex> lock(thread_registry_mutex);
        registered_threads.push_back(thread);
        active_threads++;
    }
    
    // Notify that a thread is about to exit
    void thread_exiting() {
        int remaining = --active_threads;
        if (remaining == 0) {
            all_threads_stopped_cv.notify_all();
        }
    }
    
    // Wait for all registered threads to finish
    bool wait_for_all_threads(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(shutdown_mutex);
        return all_threads_stopped_cv.wait_for(lock, timeout, [this]() {
            return active_threads.load() == 0;
        });
    }
    
    // Force shutdown of remaining threads
    void force_shutdown_threads() {
        std::lock_guard<std::mutex> lock(thread_registry_mutex);
        
        for (auto it = registered_threads.begin(); it != registered_threads.end();) {
            if (auto thread = it->lock()) {
                if (thread->joinable()) {
                    // Give thread 100ms to exit gracefully
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (thread->joinable()) {
                        thread->detach(); // Force detach if still running
                    }
                }
                ++it;
            } else {
                it = registered_threads.erase(it); // Remove expired weak_ptr
            }
        }
        
        registered_threads.clear();
        active_threads.store(0);
    }
    
private:
    ShutdownCoordinator() = default;
    ~ShutdownCoordinator() = default;
    ShutdownCoordinator(const ShutdownCoordinator&) = delete;
    ShutdownCoordinator& operator=(const ShutdownCoordinator&) = delete;
};

#endif // SHUTDOWN_COORDINATOR_H