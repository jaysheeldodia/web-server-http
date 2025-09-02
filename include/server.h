#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include "http_request.h"
#include "file_handler.h"
#include "thread_pool.h"
#include "json_handler.h"
#include "websocket_handler.h"
#include "http2_handler.h"

class WebServer {
private:
    int server_fd;
    int port;
    struct sockaddr_in address;
    std::string document_root;
    std::unique_ptr<FileHandler> file_handler;
    std::unique_ptr<ThreadPool> thread_pool;
    std::unique_ptr<WebSocketHandler> websocket_handler;
    std::shared_ptr<PerformanceMetrics> performance_metrics;
    
    // Keep-Alive support with proper thread safety
    std::atomic<bool> keep_alive_enabled;
    std::chrono::seconds connection_timeout;
    std::unordered_map<int, std::chrono::steady_clock::time_point> connection_timestamps;
    mutable std::timed_mutex connection_mutex; // Protects connection_timestamps
    
    // Request logging
    std::atomic<size_t> total_requests;
    mutable std::timed_mutex log_mutex; // Protects console output
    
    // API data storage (in-memory for demo purposes)
    std::vector<std::map<std::string, std::string>> users_data;
    std::atomic<int> next_user_id;
    mutable std::timed_mutex data_mutex; // Protects users_data
    
    // Performance monitoring thread
    std::thread metrics_thread;
    std::atomic<bool> metrics_running;
    
    // HTTP/2 support
    std::atomic<bool> http2_enabled;
    
public:
    WebServer(int port = 8080, const std::string& doc_root = "./www", size_t thread_count = 4);
    ~WebServer();
    
    // Non-copyable
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    
    bool initialize();
    void start();
    void cleanup();
    
    void handle_client_task_safe(int client_socket);
    int extract_status_code(const std::string& response) const;
    bool read_request_with_timeout(int socket, std::string& headers_data, std::chrono::seconds timeout);
    bool send_response_safe(int socket, const std::string& response);
    void add_connection_safe(int socket);
    void update_connection_timestamp_safe(int socket);
    void remove_connection_safe(int socket);
    void enable_keep_alive(bool enable, int timeout_seconds = 5);
    void manage_connections(); // Should be called periodically
    
    // HTTP/2 support
    void enable_http2(bool enable);
    bool is_http2_enabled() const { return http2_enabled.load(); }
    
    // Statistics
    size_t get_total_requests() const { return total_requests.load(); }
    size_t get_active_connections() const;
    
private:
    void handle_client_task(int client_socket);
    std::string read_request(int client_socket, const HttpRequest& parsed_request);
    void send_response(int client_socket, const std::string& response);
    
    // HTTP/2 handling
    bool detect_http2_preface(int client_socket);
    void handle_http2_connection(int client_socket, const char* initial_data = nullptr, size_t initial_len = 0);
    bool send_http2_upgrade_response(int client_socket);
    
    // HTTP response builders
    std::string build_http_response(int status_code, const std::string& status_text, 
                                  const std::string& content_type, const std::string& body, 
                                  bool keep_alive = false, bool add_cors = false);
    
    // Request handlers
    std::string handle_get_request(const HttpRequest& request, bool& keep_alive);
    std::string handle_post_request(const HttpRequest& request, bool& keep_alive);
    std::string handle_request(const HttpRequest& request, bool& keep_alive);
    
    // WebSocket handlers
    bool handle_websocket_upgrade(int client_socket, const HttpRequest& request);
    std::string generate_client_id() const;
    
    // API endpoint handlers
    std::string handle_api_request(const HttpRequest& request, bool& keep_alive);
    std::string handle_users_api(const HttpRequest& request);
    std::string handle_user_api(const HttpRequest& request, const std::string& user_id);
    std::string handle_server_stats_api(const HttpRequest& request);
    std::string handle_api_docs(const HttpRequest& request);
    std::string handle_dashboard_request(const HttpRequest& request);
    
    // CORS support
    std::string handle_options_request(const HttpRequest& request);
    void add_cors_headers(std::string& response);
    
    // Error responses
    std::string get_error_response(int status_code, const std::string& status_text, const std::string& message, bool add_cors = false);
    std::string get_404_response();
    std::string get_400_response();
    std::string get_405_response();
    
    // Connection management
    void add_connection(int socket);
    void update_connection_timestamp(int socket);
    void remove_connection(int socket);
    bool should_keep_alive(const HttpRequest& request) const;
    
    // Logging
    void log_request(const std::string& method, const std::string& path, int status_code, 
                    const std::chrono::milliseconds& duration) const;
    void safe_cout(const std::string& message) const;
    
    // Data management helpers
    void initialize_sample_data();
    std::map<std::string, std::string> create_user(const std::string& name, const std::string& email);
    std::vector<std::string> split_path(const std::string& path) const;
    bool is_api_path(const std::string& path) const;
    bool is_websocket_path(const std::string& path) const;
    
    // Performance monitoring
    void start_metrics_collection();
    void stop_metrics_collection();
    void metrics_collection_loop();
    void record_request_metric(const std::string& method, const std::string& path, 
                              int status_code, double response_time_ms);
};

#endif // SERVER_H