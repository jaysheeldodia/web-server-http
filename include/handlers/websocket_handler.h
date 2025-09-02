#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>
#include <queue>

class WebSocketConnection {
public:
    int socket;
    std::string client_id;
    std::chrono::steady_clock::time_point last_ping;
    bool is_authenticated;
    
    WebSocketConnection(int sock, const std::string& id) 
        : socket(sock), client_id(id), last_ping(std::chrono::steady_clock::now()), is_authenticated(false) {}
};

class PerformanceMetrics {
public:
    struct RequestMetric {
        std::chrono::steady_clock::time_point timestamp;
        double response_time_ms;
        int status_code;
        std::string method;
        std::string path;
    };
    
    struct SystemMetric {
        std::chrono::steady_clock::time_point timestamp;
        size_t memory_usage_mb;
        double cpu_usage_percent;
        size_t active_connections;
        size_t total_requests;
        double requests_per_second;
        size_t queue_size;
        size_t thread_count;
    };
    
private:
    mutable std::mutex metrics_mutex;
    std::queue<RequestMetric> request_history;
    std::queue<SystemMetric> system_history;
    std::atomic<size_t> total_requests{0};
    std::atomic<size_t> requests_last_minute{0};
    std::chrono::steady_clock::time_point last_minute_reset;
    
    // Keep last 1000 requests and 300 system metrics (5 minutes at 1 per second)
    static const size_t MAX_REQUEST_HISTORY = 1000;
    static const size_t MAX_SYSTEM_HISTORY = 300;
    
public:
    PerformanceMetrics() : last_minute_reset(std::chrono::steady_clock::now()) {}
    
    void record_request(const std::string& method, const std::string& path, 
                       int status_code, double response_time_ms);
    void record_system_metrics(size_t memory_mb, double cpu_percent, 
                              size_t active_connections, size_t queue_size, size_t thread_count);
    
    std::string get_metrics_json() const;
    std::string get_request_rate_json() const;
    std::string get_system_metrics_json() const;
    
    size_t get_total_requests() const { return total_requests.load(); }
    size_t get_requests_per_minute() const { return requests_last_minute.load(); }
    
private:
    void update_request_rate();
    size_t get_memory_usage() const;
    double get_cpu_usage() const;
};

class WebSocketHandler {
private:
    std::map<std::string, std::shared_ptr<WebSocketConnection>> connections;
    mutable std::timed_mutex connections_mutex;
    std::atomic<bool> running{false};
    std::thread broadcast_thread;
    std::thread ping_thread;
    std::shared_ptr<PerformanceMetrics> metrics;
    
    // WebSocket protocol constants
    static const uint8_t WS_OPCODE_CONTINUATION = 0x0;
    static const uint8_t WS_OPCODE_TEXT = 0x1;
    static const uint8_t WS_OPCODE_BINARY = 0x2;
    static const uint8_t WS_OPCODE_CLOSE = 0x8;
    static const uint8_t WS_OPCODE_PING = 0x9;
    static const uint8_t WS_OPCODE_PONG = 0xa;
    
public:
    WebSocketHandler();
    ~WebSocketHandler();
    
    // WebSocket protocol handling
    bool is_websocket_request(const std::map<std::string, std::string>& headers) const;
    std::string generate_websocket_response(const std::map<std::string, std::string>& headers) const;
    bool handle_websocket_connection(int client_socket, const std::string& client_id);
    
    // Connection management
    void add_connection(int socket, const std::string& client_id);
    void remove_connection(const std::string& client_id);
    void broadcast_message(const std::string& message);
    void send_message_to_client(const std::string& client_id, const std::string& message);
    
    // Performance metrics
    void set_metrics(std::shared_ptr<PerformanceMetrics> perf_metrics);
    void record_request(const std::string& method, const std::string& path, 
                       int status_code, double response_time_ms);
    
    // Control
    void start();
    void stop();
    size_t get_connection_count() const;
    
private:
    // WebSocket frame parsing and creation
    struct WebSocketFrame {
        bool fin;
        uint8_t opcode;
        bool masked;
        uint64_t payload_length;
        uint8_t mask[4];
        std::vector<uint8_t> payload;
    };
    
    WebSocketFrame parse_frame(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> create_frame(uint8_t opcode, const std::string& payload) const;
    bool send_frame(int socket, uint8_t opcode, const std::string& payload) const;
    bool send_ping(int socket) const;
    bool send_pong(int socket) const;
    
    // Background tasks
    void broadcast_loop();
    void ping_loop();
    
    // Utilities
    std::string generate_websocket_key() const;
    std::string base64_encode(const std::string& input) const;
    std::string sha1_hash(const std::string& input) const;
    void cleanup_dead_connections();


    void broadcast_message_safe(const std::string& message);
    void send_message_to_client_safe(const std::string& client_id, const std::string& message);
    size_t get_connection_count_safe() const;
    bool handle_websocket_connection_safe(int client_socket, const std::string& client_id);
    void broadcast_loop_safe();
    void ping_loop_safe();
};

#endif // WEBSOCKET_HANDLER_H