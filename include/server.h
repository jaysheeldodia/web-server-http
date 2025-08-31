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
#include "http_request.h"
#include "file_handler.h"
#include "thread_pool.h"

class WebServer {
private:
    int server_fd;
    int port;
    struct sockaddr_in address;
    std::string document_root;
    std::unique_ptr<FileHandler> file_handler;
    std::unique_ptr<ThreadPool> thread_pool;
    
    // Keep-Alive support with proper thread safety
    std::atomic<bool> keep_alive_enabled;
    std::chrono::seconds connection_timeout;
    std::unordered_map<int, std::chrono::steady_clock::time_point> connection_timestamps;
    mutable std::mutex connection_mutex; // Protects connection_timestamps
    
    // Request logging
    std::atomic<size_t> total_requests;
    mutable std::mutex log_mutex; // Protects console output
    
public:
    WebServer(int port = 8080, const std::string& doc_root = "./www", size_t thread_count = 4);
    ~WebServer();
    
    // Non-copyable
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    
    bool initialize();
    void start();
    void cleanup();
    
    void enable_keep_alive(bool enable, int timeout_seconds = 5);
    void manage_connections(); // Should be called periodically
    
    // Statistics
    size_t get_total_requests() const { return total_requests.load(); }
    
private:
    void handle_client_task(int client_socket);
    std::string read_request(int client_socket);
    void send_response(int client_socket, const std::string& response);
    
    // HTTP response builders
    std::string build_http_response(int status_code, const std::string& status_text, 
                                  const std::string& content_type, const std::string& body, 
                                  bool keep_alive = false);
    
    // Request handlers
    std::string handle_get_request(const HttpRequest& request, bool& keep_alive);
    std::string handle_request(const HttpRequest& request, bool& keep_alive);
    
    // Error responses
    std::string get_error_response(int status_code, const std::string& status_text, const std::string& message);
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
};

#endif // SERVER_H