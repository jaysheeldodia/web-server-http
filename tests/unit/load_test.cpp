#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <iomanip>

class LoadTester {
private:
    std::string host;
    int port;
    std::string path;
    int total_requests;
    int concurrent_threads;
    
    // Statistics
    std::atomic<int> completed_requests{0};
    std::atomic<int> failed_requests{0};
    std::atomic<int> successful_requests{0};
    
    std::mutex stats_mutex;
    std::vector<double> response_times;
    
public:
    LoadTester(const std::string& host, int port, const std::string& path, 
               int total_requests, int concurrent_threads)
        : host(host), port(port), path(path), 
          total_requests(total_requests), concurrent_threads(concurrent_threads) {
        response_times.reserve(total_requests);
    }
    
    void run_test() {
        std::cout << "Starting load test:" << std::endl;
        std::cout << "  Target: http://" << host << ":" << port << path << std::endl;
        std::cout << "  Total requests: " << total_requests << std::endl;
        std::cout << "  Concurrent threads: " << concurrent_threads << std::endl;
        std::cout << "  Requests per thread: " << total_requests / concurrent_threads << std::endl;
        std::cout << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Create worker threads
        std::vector<std::thread> workers;
        int requests_per_thread = total_requests / concurrent_threads;
        int remaining_requests = total_requests % concurrent_threads;
        
        for (int i = 0; i < concurrent_threads; ++i) {
            int thread_requests = requests_per_thread;
            if (i < remaining_requests) {
                thread_requests++; // Distribute remaining requests
            }
            
            workers.emplace_back(&LoadTester::worker_thread, this, thread_requests, i);
        }
        
        // Progress reporting thread
        std::thread progress_thread(&LoadTester::progress_reporter, this);
        
        // Wait for all workers to complete
        for (auto& worker : workers) {
            worker.join();
        }
        
        progress_thread.detach();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        print_results(duration.count());
    }
    
private:
    void worker_thread(int requests, int /* thread_id */) {
        for (int i = 0; i < requests; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            bool success = send_request();
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration<double, std::milli>(end - start);
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                response_times.push_back(duration.count());
            }
            
            if (success) {
                successful_requests++;
            } else {
                failed_requests++;
            }
            
            completed_requests++;
        }
    }
    
    bool send_request() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return false;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
        
        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            return false;
        }
        
        // Send HTTP request
        std::string request = "GET " + path + " HTTP/1.1\r\n";
        request += "Host: " + host + ":" + std::to_string(port) + "\r\n";
        request += "Connection: close\r\n\r\n";
        
        if (send(sock, request.c_str(), request.length(), 0) < 0) {
            close(sock);
            return false;
        }
        
        // Read response
        char buffer[4096];
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        
        close(sock);
        
        return bytes_received > 0;
    }
    
    void progress_reporter() {
        while (completed_requests < total_requests) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int completed = completed_requests.load();
            int failed = failed_requests.load();
            double progress = (double)completed / total_requests * 100.0;
            
            std::cout << "\rProgress: " << completed << "/" << total_requests 
                      << " (" << std::fixed << std::setprecision(1) << progress << "%) "
                      << "Failed: " << failed << std::flush;
        }
        std::cout << std::endl;
    }
    
    void print_results(long duration_ms) {
        std::cout << std::endl;
        std::cout << "=== Load Test Results ===" << std::endl;
        std::cout << "Total time: " << duration_ms << " ms (" << duration_ms/1000.0 << " seconds)" << std::endl;
        std::cout << "Successful requests: " << successful_requests.load() << std::endl;
        std::cout << "Failed requests: " << failed_requests.load() << std::endl;
        std::cout << "Success rate: " << (double)successful_requests / total_requests * 100.0 << "%" << std::endl;
        
        if (duration_ms > 0) {
            double rps = (double)successful_requests * 1000.0 / duration_ms;
            std::cout << "Requests per second: " << std::fixed << std::setprecision(2) << rps << std::endl;
        }
        
        // Response time statistics
        if (!response_times.empty()) {
            std::sort(response_times.begin(), response_times.end());
            
            double sum = std::accumulate(response_times.begin(), response_times.end(), 0.0);
            double mean = sum / response_times.size();
            
            double median = response_times[response_times.size() / 2];
            double p95 = response_times[(int)(response_times.size() * 0.95)];
            double p99 = response_times[(int)(response_times.size() * 0.99)];
            double min_time = response_times.front();
            double max_time = response_times.back();
            
            std::cout << std::endl;
            std::cout << "=== Response Time Statistics (ms) ===" << std::endl;
            std::cout << "Mean: " << std::fixed << std::setprecision(2) << mean << std::endl;
            std::cout << "Median: " << median << std::endl;
            std::cout << "95th percentile: " << p95 << std::endl;
            std::cout << "99th percentile: " << p99 << std::endl;
            std::cout << "Min: " << min_time << std::endl;
            std::cout << "Max: " << max_time << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string path = "/";
    int total_requests = 10000;
    int concurrent_threads = 50;
    
    if (argc > 1) total_requests = std::stoi(argv[1]);
    if (argc > 2) concurrent_threads = std::stoi(argv[2]);
    if (argc > 3) port = std::stoi(argv[3]);
    if (argc > 4) path = argv[4];
    
    LoadTester tester(host, port, path, total_requests, concurrent_threads);
    tester.run_test();
    
    return 0;
}