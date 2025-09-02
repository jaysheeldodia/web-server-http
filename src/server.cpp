#include "../include/server.h"
#include "../include/shutdown_coordinator.h"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <thread>
#include <sstream>
#include <fcntl.h>
#include <sys/socket.h>
#include <iomanip>
#include "../include/globals.h"
#include <atomic>
#include <algorithm>

// Global flag for graceful shutdown
// extern std::atomic<bool> g_shutdown_requested{false};

class ResourceManager {
private:
    std::vector<int> sockets;
    mutable std::mutex sockets_mutex;
    
public:
    void register_socket(int socket) {
        std::lock_guard<std::mutex> lock(sockets_mutex);
        sockets.push_back(socket);
    }
    
    void unregister_socket(int socket) {
        std::lock_guard<std::mutex> lock(sockets_mutex);
        sockets.erase(std::remove(sockets.begin(), sockets.end(), socket), sockets.end());
    }
    
    void close_all_sockets() {
        std::lock_guard<std::mutex> lock(sockets_mutex);
        for (int socket : sockets) {
            if (socket >= 0) {
                shutdown(socket, SHUT_RDWR); // Graceful shutdown
                close(socket);
            }
        }
        sockets.clear();
    }
    
    size_t socket_count() const {
        std::lock_guard<std::mutex> lock(sockets_mutex);
        return sockets.size();
    }
};

// Global resource manager instance
static ResourceManager g_resource_manager;

WebServer::WebServer(int port, const std::string& doc_root, size_t thread_count) 
    : server_fd(-1), port(port), document_root(doc_root), 
      keep_alive_enabled(false), connection_timeout(5), total_requests(0), next_user_id(1),
      metrics_running(false), http2_enabled(false), tls_enabled(false), ssl_ctx(nullptr) {
    
    memset(&address, 0, sizeof(address));
    file_handler = std::make_unique<FileHandler>(document_root);
    thread_pool = std::make_unique<ThreadPool>(thread_count);
    
    // Initialize performance metrics and WebSocket handler
    performance_metrics = std::make_shared<PerformanceMetrics>();
    websocket_handler = std::make_unique<WebSocketHandler>();
    websocket_handler->set_metrics(performance_metrics);
    
    initialize_sample_data();
}

WebServer::~WebServer() {
    cleanup();
}

bool WebServer::initialize() {
    // Create socket file descriptor
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return false;
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Setsockopt failed: " << strerror(errno) << std::endl;
        close(server_fd);
        server_fd = -1;
        return false;
    }

    // Configure address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind socket to address
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        close(server_fd);
        server_fd = -1;
        return false;
    }

    // Start listening for connections
    if (listen(server_fd, 128) < 0) {
        std::cerr << "Listen failed: " << strerror(errno) << std::endl;
        close(server_fd);
        server_fd = -1;
        return false;
    }

    safe_cout("Server initialized on port " + std::to_string(port));
    safe_cout("API endpoints available at /api/");
    safe_cout("Performance Dashboard: http://localhost:" + std::to_string(port) + "/dashboard");
    return true;
}

void WebServer::start() {
    auto& coordinator = ShutdownCoordinator::instance();
    
    safe_cout("Server starting on http://localhost:" + std::to_string(port));
    safe_cout("Document root: " + document_root);
    safe_cout("Thread pool size: " + std::to_string(thread_pool->get_thread_count()));
    safe_cout("Keep-Alive: " + std::string(keep_alive_enabled ? "enabled" : "disabled"));

    // Start WebSocket handler and metrics collection
    websocket_handler->start();
    start_metrics_collection();

    // Start connection cleanup thread 
    std::thread cleanup_thread;
    if (keep_alive_enabled) {
        cleanup_thread = std::thread([this]() {
            auto& coord = ShutdownCoordinator::instance();
            
            while (!coord.is_shutdown_requested()) {
                if (coord.wait_for_shutdown(std::chrono::seconds(1))) {
                    break; // Shutdown requested
                }
                manage_connections();
            }
            
            coord.thread_exiting();
        });
        
        // Note: Simplified thread management without registration for now
    }

    // Main accept loop with proper shutdown handling
    while (!coordinator.is_shutdown_requested()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Use select or poll for non-blocking accept with timeout
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;
        
        int select_result = select(server_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (select_result < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, check shutdown
            }
            std::cerr << "Select failed: " << strerror(errno) << std::endl;
            break;
        }
        
        if (select_result == 0) {
            continue; // Timeout, check shutdown and continue
        }
        
        if (!FD_ISSET(server_fd, &read_fds)) {
            continue; // Not our socket
        }
        
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            if (errno == EINTR || coordinator.is_shutdown_requested()) {
                break;
            }
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            continue;
        }

        if (coordinator.is_shutdown_requested()) {
            close(client_socket);
            break;
        }

        // Register socket for cleanup
        g_resource_manager.register_socket(client_socket);

        // Set socket timeout for safety
        struct timeval sock_timeout;
        sock_timeout.tv_sec = 30;
        sock_timeout.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout, sizeof(sock_timeout));
        setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &sock_timeout, sizeof(sock_timeout));

        // Add to connection tracking
        if (keep_alive_enabled) {
            add_connection_safe(client_socket);
        }

        // Add client handling to thread pool with resource cleanup
        thread_pool->enqueue([this, client_socket]() {
            this->handle_client_task_safe(client_socket);
        });
    }

    safe_cout("Server shutting down...");
    
    // Wait for cleanup thread to finish
    if (!coordinator.wait_for_all_threads(std::chrono::seconds(5))) {
        safe_cout("Warning: Some threads did not exit gracefully, forcing shutdown");
        coordinator.force_shutdown_threads();
    }
}

void WebServer::handle_client_task(int client_socket) {
    std::thread::id thread_id = std::this_thread::get_id();
    bool keep_connection = false;
    
    try {
        do {
            // Check for shutdown
            if (g_shutdown_requested) {
                break;
            }
            
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Read initial data to determine protocol
            std::string headers_data;
            char buffer[4096];
            ssize_t initial_bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            
            if (initial_bytes <= 0) {
                if (initial_bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && !g_shutdown_requested) {
                    std::cerr << "Initial recv failed: " << strerror(errno) << std::endl;
                }
                break;
            }
            
            buffer[initial_bytes] = '\0';
            headers_data = std::string(buffer);
            
            // Check if this is HTTP/2 preface
            if (http2_enabled && initial_bytes >= 24 && 
                memcmp(buffer, HTTP2_CONNECTION_PREFACE, 24) == 0) {
                // Handle as HTTP/2 connection - pass the initial data
                safe_cout("🔵 HTTP/2 connection detected - preface matched");
                handle_http2_connection(client_socket, buffer, initial_bytes);
                safe_cout("🔵 HTTP/2 handler returned - connection closed");
                break;
            } else if (http2_enabled) {
                // Debug: show what we received instead
                std::string debug_msg = "🔴 Not HTTP/2 preface. Received " + std::to_string(initial_bytes) + " bytes: ";
                for (int i = 0; i < std::min(initial_bytes, 24L); i++) {
                    char hex[4];
                    snprintf(hex, sizeof(hex), "%02x ", (unsigned char)buffer[i]);
                    debug_msg += hex;
                }
                safe_cout(debug_msg);
                
                // Compare with expected preface
                safe_cout("🔴 Expected preface: 50 52 49 20 2a 20 48 54 54 50 2f 32 2e 30 0d 0a 0d 0a 53 4d 0d 0a 0d 0a");
            }
            
            // Handle as HTTP/1.1 - continue reading headers if needed
            while (headers_data.find("\r\n\r\n") == std::string::npos && !g_shutdown_requested) {
                ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    headers_data += std::string(buffer);
                    
                    if (headers_data.size() > 8192) break; // Prevent abuse
                } else if (bytes_received == 0) {
                    // Client closed connection
                    break;
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK && !g_shutdown_requested) {
                        std::cerr << "Recv failed: " << strerror(errno) << std::endl;
                    }
                    break;
                }
            }
            
            if (headers_data.empty() || g_shutdown_requested) {
                break;
            }

            HttpRequest request;
            if (!request.parse(headers_data)) {
                if (!g_shutdown_requested) {
                    std::string response = get_400_response();
                    send_response(client_socket, response);
                    
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    log_request("INVALID", "INVALID", 400, duration);
                    record_request_metric("INVALID", "INVALID", 400, duration.count());
                }
                break;
            }

            // Check for WebSocket upgrade request
            if (is_websocket_path(request.path) && 
                websocket_handler->is_websocket_request(request.headers) &&
                !g_shutdown_requested) {
                
                if (handle_websocket_upgrade(client_socket, request)) {
                    // WebSocket connection established - don't close socket here
                    remove_connection(client_socket);
                    return; // Exit without closing socket
                } else {
                    break; // Failed to upgrade, close connection
                }
            }

            if (g_shutdown_requested) {
                break;
            }

            // Handle regular HTTP request
            std::string response = handle_request(request, keep_connection);
            if (!g_shutdown_requested) {
                send_response(client_socket, response);
            }
            
            if (keep_connection && keep_alive_enabled && !g_shutdown_requested) {
                update_connection_timestamp(client_socket);
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            int status_code = 200;
            if (response.find("404") != std::string::npos) status_code = 404;
            else if (response.find("400") != std::string::npos) status_code = 400;
            else if (response.find("405") != std::string::npos) status_code = 405;
            
            if (!g_shutdown_requested) {
                log_request(request.method, request.path, status_code, duration);
                record_request_metric(request.method, request.path, status_code, duration.count());
                total_requests++;
            }

        } while (keep_connection && keep_alive_enabled && !g_shutdown_requested);
        
    } catch (const std::exception& e) {
        if (!g_shutdown_requested) {
            std::ostringstream oss;
            oss << "[Thread " << thread_id << "] Exception: " << e.what();
            safe_cout(oss.str());
        }
    }
    
    // Clean up connection
    remove_connection(client_socket);
    close(client_socket);
}

void WebServer::handle_client_task_safe(int client_socket) {
    // RAII wrapper for socket cleanup
    struct SocketGuard {
        int socket;
        WebServer* server;
        
        SocketGuard(int s, WebServer* srv) : socket(s), server(srv) {}
        ~SocketGuard() {
            server->remove_connection_safe(socket);
            g_resource_manager.unregister_socket(socket);
            close(socket);
        }
    } guard(client_socket, this);
    
    auto& coordinator = ShutdownCoordinator::instance();
    
    try {
        if (coordinator.is_shutdown_requested()) {
            return;
        }
        
        // If TLS is enabled, detect if this is a TLS connection
        if (tls_enabled.load()) {
            // Peek at the first byte to detect TLS handshake
            char peek_buffer[1];
            ssize_t peek_result = recv(client_socket, peek_buffer, 1, MSG_PEEK);
            
            if (peek_result > 0) {
                // TLS handshake starts with 0x16 (SSL3_RT_HANDSHAKE)
                if ((unsigned char)peek_buffer[0] == 0x16) {
                    safe_cout("Detected TLS connection, handling with SSL");
                    handle_tls_connection(client_socket);
                    return;
                }
            }
        }
        
        // Handle as regular HTTP connection
        handle_http_connection(client_socket);
        
    } catch (const std::exception& e) {
        safe_cout("Client handling error: " + std::string(e.what()));
    }
}

void WebServer::handle_http_connection(int client_socket) {
    auto& coordinator = ShutdownCoordinator::instance();
    bool keep_connection = false;
    
    try {
        do {
            if (coordinator.is_shutdown_requested()) {
                break;
            }
            
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Read and parse HTTP request with timeout
            std::string headers_data;
            if (!read_request_with_timeout(client_socket, headers_data, std::chrono::seconds(5))) {
                break; // Timeout or error
            }
            
            if (coordinator.is_shutdown_requested()) {
                break;
            }

            HttpRequest request;
            if (!request.parse(headers_data)) {
                if (!coordinator.is_shutdown_requested()) {
                    std::string response = get_400_response();
                    send_response_safe(client_socket, response);
                }
                break;
            }

            // Check for WebSocket upgrade
            if (is_websocket_path(request.path) && 
                websocket_handler->is_websocket_request(request.headers) &&
                !coordinator.is_shutdown_requested()) {
                
                if (handle_websocket_upgrade(client_socket, request)) {
                    return; // WebSocket handler takes over
                } else {
                    break;
                }
            }

            if (coordinator.is_shutdown_requested()) {
                break;
            }

            // Handle regular HTTP request
            std::string response = handle_request(request, keep_connection);
            if (!coordinator.is_shutdown_requested()) {
                send_response_safe(client_socket, response);
            }
            
            if (keep_connection && keep_alive_enabled && !coordinator.is_shutdown_requested()) {
                update_connection_timestamp_safe(client_socket);
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            int status_code = extract_status_code(response);
            
            if (!coordinator.is_shutdown_requested()) {
                log_request(request.method, request.path, status_code, duration);
                record_request_metric(request.method, request.path, status_code, duration.count());
                total_requests++;
            }

        } while (keep_connection && keep_alive_enabled && !coordinator.is_shutdown_requested());
        
    } catch (const std::exception& e) {
        if (!coordinator.is_shutdown_requested()) {
            std::cerr << "Client handler exception: " << e.what() << std::endl;
        }
    }
}

int WebServer::extract_status_code(const std::string& response) const {
    if (response.find("200 OK") != std::string::npos) return 200;
    if (response.find("201 Created") != std::string::npos) return 201;
    if (response.find("400 Bad Request") != std::string::npos) return 400;
    if (response.find("404 Not Found") != std::string::npos) return 404;
    if (response.find("405 Method Not Allowed") != std::string::npos) return 405;
    if (response.find("500 Internal Server Error") != std::string::npos) return 500;
    return 200; // Default
}

bool WebServer::read_request_with_timeout(int socket, std::string& headers_data, std::chrono::seconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    char buffer[4096];
    
    while (headers_data.find("\r\n\r\n") == std::string::npos) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false; // Timeout
        }
        
        if (ShutdownCoordinator::instance().is_shutdown_requested()) {
            return false;
        }
        
        // Use select for timeout
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int select_result = select(socket + 1, &read_fds, nullptr, nullptr, &tv);
        
        if (select_result < 0) {
            return false; // Error
        }
        
        if (select_result == 0) {
            continue; // Timeout, check deadline and continue
        }
        
        ssize_t bytes_received = recv(socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            headers_data += std::string(buffer);
            
            if (headers_data.size() > 8192) {
                return false; // Prevent abuse
            }
        } else if (bytes_received == 0) {
            return false; // Connection closed
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return false; // Error
            }
        }
    }
    
    return true;
}

bool WebServer::send_response_safe(int socket, const std::string& response) {
    if (ShutdownCoordinator::instance().is_shutdown_requested()) {
        return false;
    }
    
    size_t total_sent = 0;
    size_t response_len = response.length();
    
    while (total_sent < response_len) {
        if (ShutdownCoordinator::instance().is_shutdown_requested()) {
            return false;
        }
        
        ssize_t bytes_sent = send(socket, response.c_str() + total_sent, 
                                response_len - total_sent, MSG_NOSIGNAL);
        
        if (bytes_sent < 0) {
            if (errno == EPIPE || errno == ECONNRESET) {
                return false; // Client disconnected
            }
            return false;
        }
        
        total_sent += bytes_sent;
    }
    
    return true;
}

void WebServer::add_connection_safe(int socket) {
    // Critical operation: must succeed to prevent resource leaks
    std::lock_guard<std::timed_mutex> lock(connection_mutex);
    connection_timestamps[socket] = std::chrono::steady_clock::now();
}

void WebServer::update_connection_timestamp_safe(int socket) {
    // Critical operation: must succeed to maintain accurate timeouts
    std::lock_guard<std::timed_mutex> lock(connection_mutex);
    auto it = connection_timestamps.find(socket);
    if (it != connection_timestamps.end()) {
        it->second = std::chrono::steady_clock::now();
    }
}

void WebServer::remove_connection_safe(int socket) {
    // Critical operation: must succeed to prevent memory leaks
    std::lock_guard<std::timed_mutex> lock(connection_mutex);
    connection_timestamps.erase(socket);
}


std::string WebServer::handle_request(const HttpRequest& request, bool& keep_alive) {
    keep_alive = should_keep_alive(request);
    
    // Check for HTTP/2 upgrade request
    if (http2_enabled && request.method == "GET" && 
        request.get_header("upgrade") == "h2c" && 
        request.get_header("connection").find("Upgrade") != std::string::npos) {
        
        keep_alive = false; // Connection will be upgraded, not kept alive
        return "HTTP/1.1 101 Switching Protocols\r\n"
               "Connection: Upgrade\r\n"
               "Upgrade: h2c\r\n"
               "\r\n";
    }
    
    if (request.method == "GET") {
        return handle_get_request(request, keep_alive);
    } else if (request.method == "POST") {
        return handle_post_request(request, keep_alive);
    } else if (request.method == "OPTIONS") {
        return handle_options_request(request);
    } else if (request.method == "HEAD") {
        // HEAD is like GET but without body
        std::string response = handle_get_request(request, keep_alive);
        // Remove body (everything after \r\n\r\n)
        size_t body_start = response.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            response = response.substr(0, body_start + 4);
        }
        return response;
    } else {
        keep_alive = false; // Don't keep alive on error responses
        return get_405_response();
    }
}

std::string WebServer::handle_get_request(const HttpRequest& request, bool& keep_alive) {
    // Check for dashboard request
    if (request.path == "/dashboard" || request.path == "/dashboard.html") {
        return handle_dashboard_request(request);
    }
    
    // Check if it's an API request
    if (is_api_path(request.path)) {
        return handle_api_request(request, keep_alive);
    }
    
    // Regular static file serving (existing code)
    if (!file_handler->file_exists(request.path)) {
        keep_alive = false;
        return get_404_response();
    }
    
    std::string content = file_handler->read_file(request.path);
    if (content.empty()) {
        keep_alive = false;
        return get_404_response();
    }
    
    std::string mime_path = request.path;
    if (request.path == "/") {
        mime_path = "index.html";
    }
    
    std::string mime_type = file_handler->get_mime_type(mime_path);
    return build_http_response(200, "OK", mime_type, content, keep_alive, false);
}

std::string WebServer::handle_post_request(const HttpRequest& request, bool& keep_alive) {
    // Check if it's an API request
    if (is_api_path(request.path)) {
        return handle_api_request(request, keep_alive);
    }
    
    // For non-API POST requests, return 405 Method Not Allowed
    keep_alive = false;
    return get_405_response();
}

std::string WebServer::handle_api_request(const HttpRequest& request, bool& keep_alive) {
    auto path_parts = split_path(request.path);
    
    if (path_parts.size() < 2) {
        return build_http_response(400, "Bad Request", "application/json", 
                                 JsonHandler::build_error_response("Invalid API path", 400), 
                                 keep_alive, true);
    }
    
    // path_parts[0] = "api"
    std::string endpoint = path_parts[1];
    
    if (endpoint == "docs") {
        return handle_api_docs(request);
    } else if (endpoint == "users") {
        if (path_parts.size() == 2) {
            // /api/users
            return handle_users_api(request);
        } else if (path_parts.size() == 3) {
            // /api/users/{id}
            return handle_user_api(request, path_parts[2]);
        }
    } else if (endpoint == "stats") {
        return handle_server_stats_api(request);
    }
    
    return build_http_response(404, "Not Found", "application/json", 
                             JsonHandler::build_error_response("API endpoint not found", 404), 
                             keep_alive, true);
}

std::string WebServer::handle_users_api(const HttpRequest& request) {
    if (request.method == "GET") {
        // GET /api/users - List all users
        std::lock_guard<std::timed_mutex> lock(data_mutex);
        std::string json_response = JsonHandler::build_users_list_response(users_data);
        return build_http_response(200, "OK", "application/json", json_response, true, true);
        
    } else if (request.method == "POST") {
        // POST /api/users - Create new user
        if (!request.has_json_content_type()) {
            return build_http_response(400, "Bad Request", "application/json", 
                                     JsonHandler::build_error_response("Content-Type must be application/json", 400), 
                                     false, true);
        }
        
        auto json_data = JsonHandler::parse(request.body);
        if (!json_data || !json_data->is_object()) {
            return build_http_response(400, "Bad Request", "application/json", 
                                     JsonHandler::build_error_response("Invalid JSON data", 400), 
                                     false, true);
        }
        
        // Extract name and email
        std::string name = json_data->get_object_item("name")->as_string();
        std::string email = json_data->get_object_item("email")->as_string();
        
        if (name.empty() || email.empty()) {
            return build_http_response(400, "Bad Request", "application/json", 
                                     JsonHandler::build_error_response("Name and email are required", 400), 
                                     false, true);
        }
        
        // Create new user
        std::map<std::string, std::string> new_user = create_user(name, email);
        
        std::string json_response = JsonHandler::build_success_response("User created successfully", 
                                  JsonHandler::parse("{\"id\":" + new_user["id"] + 
                                                   ",\"name\":\"" + new_user["name"] + 
                                                   "\",\"email\":\"" + new_user["email"] + "\"}"));
        
        return build_http_response(201, "Created", "application/json", json_response, false, true);
    }
    
    return build_http_response(405, "Method Not Allowed", "application/json", 
                             JsonHandler::build_error_response("Method not allowed", 405), 
                             false, true);
}

std::string WebServer::handle_user_api(const HttpRequest& request, const std::string& user_id) {
    if (request.method == "GET") {
        // GET /api/users/{id} - Get specific user
        std::lock_guard<std::timed_mutex> lock(data_mutex);
        
        for (const auto& user : users_data) {
            if (user.at("id") == user_id) {
                std::string json_response = JsonHandler::build_user_response(
                    std::stoi(user.at("id")), user.at("name"), user.at("email"));
                return build_http_response(200, "OK", "application/json", json_response, true, true);
            }
        }
        
        return build_http_response(404, "Not Found", "application/json", 
                                 JsonHandler::build_error_response("User not found", 404), 
                                 false, true);
    }
    
    return build_http_response(405, "Method Not Allowed", "application/json", 
                             JsonHandler::build_error_response("Method not allowed", 405), 
                             false, true);
}

std::string WebServer::handle_server_stats_api(const HttpRequest& request) {
    if (request.method == "GET") {
        auto stats = std::make_shared<JsonValue>();
        stats->make_object();
        stats->set_object_item("total_requests", std::make_shared<JsonValue>(static_cast<int>(total_requests.load())));
        stats->set_object_item("active_connections", std::make_shared<JsonValue>(static_cast<int>(connection_timestamps.size())));
        stats->set_object_item("thread_count", std::make_shared<JsonValue>(static_cast<int>(thread_pool->get_thread_count())));
        stats->set_object_item("queue_size", std::make_shared<JsonValue>(static_cast<int>(thread_pool->get_queue_size())));
        
        std::string json_response = JsonHandler::build_success_response("Server statistics", stats);
        return build_http_response(200, "OK", "application/json", json_response, true, true);
    }
    
    return build_http_response(405, "Method Not Allowed", "application/json", 
                             JsonHandler::build_error_response("Method not allowed", 405), 
                             false, true);
}

std::string WebServer::handle_api_docs(const HttpRequest& request) {
    (void)request; // Suppress unused parameter warning
    
    std::string docs_html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>C++ Web Server API Documentation</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; line-height: 1.6; }
        .endpoint { background: #f5f5f5; padding: 15px; margin: 10px 0; border-radius: 5px; }
        .method { font-weight: bold; color: #2196F3; }
        .method.post { color: #4CAF50; }
        .method.get { color: #2196F3; }
        .url { font-family: monospace; background: #e8e8e8; padding: 5px; }
        pre { background: #f0f0f0; padding: 10px; overflow-x: auto; }
    </style>
</head>
<body>
    <h1>🚀 C++ Web Server API Documentation</h1>
    <p>Welcome to the REST API documentation for our high-performance C++ web server!</p>
    
    <h2>📊 Server Statistics</h2>
    <div class="endpoint">
        <span class="method get">GET</span> <span class="url">/api/stats</span>
        <p>Get real-time server performance statistics</p>
    </div>

    <h2>👥 User Management</h2>
    <div class="endpoint">
        <span class="method get">GET</span> <span class="url">/api/users</span>
        <p>Get all users</p>
    </div>

    <div class="endpoint">
        <span class="method post">POST</span> <span class="url">/api/users</span>
        <p>Create a new user</p>
    </div>

    <div class="endpoint">
        <span class="method get">GET</span> <span class="url">/api/users/{id}</span>
        <p>Get a specific user by ID</p>
    </div>

    <p><a href="/">← Back to Home</a> | <a href="/dashboard">📊 Dashboard</a></p>
</body>
</html>
)";
    
    return build_http_response(200, "OK", "text/html", docs_html, true, true);
}

std::string WebServer::handle_options_request(const HttpRequest& request) {
    (void)request; // Suppress unused parameter warning
    
    // CORS preflight request
    std::string response = build_http_response(200, "OK", "text/plain", "", false, true);
    
    // Add additional CORS headers for preflight
    size_t header_end = response.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        std::string additional_headers = "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
        additional_headers += "Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With\r\n";
        additional_headers += "Access-Control-Max-Age: 86400\r\n";
        
        response.insert(header_end, additional_headers);
    }
    
    return response;
}

void WebServer::send_response(int client_socket, const std::string& response) {
    size_t total_sent = 0;
    size_t response_len = response.length();
    
    while (total_sent < response_len) {
        ssize_t bytes_sent = send(client_socket, response.c_str() + total_sent, 
                                response_len - total_sent, MSG_NOSIGNAL);
        
        if (bytes_sent < 0) {
            if (errno != EPIPE) { // Broken pipe is common when client disconnects
                std::cerr << "Send failed: " << strerror(errno) << std::endl;
            }
            break;
        }
        
        total_sent += bytes_sent;
    }
}

std::string WebServer::build_http_response(int status_code, const std::string& status_text,
                                         const std::string& content_type, const std::string& body,
                                         bool keep_alive, bool add_cors) {
    std::ostringstream response;
    
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Server: wbeserver-http/1.0\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    
    if (keep_alive && keep_alive_enabled) {
        response << "Connection: keep-alive\r\n";
        response << "Keep-Alive: timeout=" << connection_timeout.count() << "\r\n";
    } else {
        response << "Connection: close\r\n";
    }
    
    // Add CORS headers if requested
    if (add_cors) {
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Credentials: true\r\n";
    }
    
    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    response << "Date: " << std::put_time(std::gmtime(&time_t), "%a, %d %b %Y %H:%M:%S GMT") << "\r\n";
    
    response << "\r\n";
    response << body;
    
    return response.str();
}

std::string WebServer::get_error_response(int status_code, const std::string& status_text, const std::string& message, bool add_cors) {
    std::ostringstream body;
    body << "<!DOCTYPE html>\n"
         << "<html><head><title>" << status_code << " " << status_text << "</title></head>\n"
         << "<body style='font-family: Arial, sans-serif; text-align: center; margin-top: 50px;'>\n"
         << "<h1>" << status_code << " " << status_text << "</h1>\n"
         << "<p>" << message << "</p>\n"
         << "<hr><small>wbeserver-http/1.0</small>\n"
         << "</body></html>";
    
    return build_http_response(status_code, status_text, "text/html", body.str(), false, add_cors);
}

std::string WebServer::get_404_response() {
    return get_error_response(404, "Not Found", "The requested file was not found on this server.");
}

std::string WebServer::get_400_response() {
    return get_error_response(400, "Bad Request", "The request could not be understood by the server.");
}

std::string WebServer::get_405_response() {
    return get_error_response(405, "Method Not Allowed", "The requested method is not allowed for this resource.");
}

bool WebServer::should_keep_alive(const HttpRequest& request) const {
    if (!keep_alive_enabled) {
        return false;
    }
    
    // Check HTTP version
    if (request.version != "HTTP/1.1") {
        return false;
    }
    
    // Check Connection header
    std::string connection = request.get_header("connection");
    if (connection == "close") {
        return false;
    }
    
    // For HTTP/1.1, keep-alive is default unless specified otherwise
    return true;
}

void WebServer::add_connection(int socket) {
    if (g_shutdown_requested) return;
    
    std::unique_lock<std::timed_mutex> lock(connection_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        connection_timestamps[socket] = std::chrono::steady_clock::now();
    }
}

void WebServer::update_connection_timestamp(int socket) {
    if (g_shutdown_requested) return;
    
    std::unique_lock<std::timed_mutex> lock(connection_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        auto it = connection_timestamps.find(socket);
        if (it != connection_timestamps.end()) {
            it->second = std::chrono::steady_clock::now();
        }
    }
}

void WebServer::remove_connection(int socket) {
    std::unique_lock<std::timed_mutex> lock(connection_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        connection_timestamps.erase(socket);
    }
}

void WebServer::manage_connections() {
    if (!keep_alive_enabled || ShutdownCoordinator::instance().is_shutdown_requested()) {
        return;
    }
    
    std::vector<int> expired_connections;
    auto now = std::chrono::steady_clock::now();
    
    // Try to get lock with timeout
    std::unique_lock<std::timed_mutex> lock(connection_mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(500))) {
        return; // Skip this cycle if we can't get the lock
    }
    
    // Find expired connections
    for (const auto& conn : connection_timestamps) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - conn.second) > connection_timeout) {
            expired_connections.push_back(conn.first);
        }
    }
    
    // Remove from map
    for (int socket : expired_connections) {
        connection_timestamps.erase(socket);
    }
    
    lock.unlock();
    
    // Close sockets outside of lock
    for (int socket : expired_connections) {
        shutdown(socket, SHUT_RDWR);
        close(socket);
        g_resource_manager.unregister_socket(socket);
        safe_cout("Closed idle connection: " + std::to_string(socket));
    }
}

void WebServer::enable_keep_alive(bool enable, int timeout_seconds) {
    keep_alive_enabled.store(enable);
    connection_timeout = std::chrono::seconds(timeout_seconds);
    
    std::ostringstream oss;
    oss << "Keep-Alive " << (enable ? "enabled" : "disabled") 
        << " with timeout: " << timeout_seconds << " seconds";
    safe_cout(oss.str());
}

void WebServer::log_request(const std::string& method, const std::string& path, int status_code,
                          const std::chrono::milliseconds& duration) const {
    std::ostringstream oss;
    oss << "[" << std::this_thread::get_id() << "] " 
        << method << " " << path << " - " << status_code 
        << " (" << duration.count() << "ms)";
    safe_cout(oss.str());
}

void WebServer::safe_cout(const std::string& message) const {
    std::unique_lock<std::timed_mutex> lock(log_mutex, std::defer_lock);
    
    if (lock.try_lock_for(std::chrono::milliseconds(50))) {
        std::cout << message << std::endl;
    }
    // If we can't get the log mutex, just skip - logging is not critical during shutdown
}

void WebServer::cleanup() {
    auto& coordinator = ShutdownCoordinator::instance();
    coordinator.request_shutdown();
    
    safe_cout("Initiating server cleanup...");
    
    // Stop accepting new connections
    if (server_fd != -1) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        server_fd = -1;
    }
    
    // Stop metrics collection
    stop_metrics_collection();
    
    // Stop WebSocket handler
    if (websocket_handler) {
        websocket_handler->stop();
    }
    
    // Stop thread pool
    if (thread_pool) {
        thread_pool->stop();
    }
    
    // Cleanup TLS/SSL context
    cleanup_ssl_context();
    
    // Force close all remaining sockets
    g_resource_manager.close_all_sockets();
    
    // Clear connection tracking with forced access
    {
        std::lock_guard<std::timed_mutex> lock(connection_mutex);
        connection_timestamps.clear();
    }
    
    // Wait for all threads to finish
    if (!coordinator.wait_for_all_threads(std::chrono::seconds(3))) {
        safe_cout("Forcing shutdown of remaining threads...");
        coordinator.force_shutdown_threads();
    }
    
    safe_cout("Server cleanup completed");
}

void WebServer::initialize_sample_data() {
    std::lock_guard<std::timed_mutex> lock(data_mutex);
    
    // Add some sample users
    users_data.push_back({
        {"id", "1"},
        {"name", "John Doe"},
        {"email", "john.doe@example.com"}
    });
    
    users_data.push_back({
        {"id", "2"},
        {"name", "Jane Smith"},
        {"email", "jane.smith@example.com"}
    });
    
    users_data.push_back({
        {"id", "3"},
        {"name", "Alice Johnson"},
        {"email", "alice.johnson@example.com"}
    });
    
    next_user_id = 4; // Next ID to assign
}

std::map<std::string, std::string> WebServer::create_user(const std::string& name, const std::string& email) {
    std::lock_guard<std::timed_mutex> lock(data_mutex);
    
    std::map<std::string, std::string> new_user;
    new_user["id"] = std::to_string(next_user_id.load());
    new_user["name"] = name;
    new_user["email"] = email;
    
    users_data.push_back(new_user);
    next_user_id++;
    
    return new_user;
}

std::vector<std::string> WebServer::split_path(const std::string& path) const {
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;
    
    while (std::getline(iss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    return parts;
}

bool WebServer::is_api_path(const std::string& path) const {
    return path.length() >= 4 && path.substr(0, 4) == "/api";
}

// WebSocket upgrade handler:
bool WebServer::handle_websocket_upgrade(int client_socket, const HttpRequest& request) {
    if (g_shutdown_requested) {
        return false;
    }
    
    std::string response = websocket_handler->generate_websocket_response(request.headers);
    
    if (response.empty()) {
        return false;
    }
    
    // Send WebSocket upgrade response
    if (send(client_socket, response.c_str(), response.length(), MSG_NOSIGNAL) < 0) {
        return false;
    }
    
    // Generate unique client ID
    std::string client_id = generate_client_id();
    
    // Handle WebSocket connection (this will block until connection closes)
    return websocket_handler->handle_websocket_connection(client_socket, client_id);
}

// Dashboard request handler:
std::string WebServer::handle_dashboard_request(const HttpRequest& request) {
    (void)request; // Suppress unused parameter warning
    
    // Serve the performance dashboard HTML file
    if (file_handler->file_exists("/dashboard.html")) {
        std::string content = file_handler->read_file("/dashboard.html");
        return build_http_response(200, "OK", "text/html", content, false, true);
    } else {
        // Return basic dashboard if file doesn't exist
        std::string basic_dashboard = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Performance Dashboard</title>
    <style>body { font-family: Arial, sans-serif; margin: 40px; }</style>
</head>
<body>
    <h1>🚀 Performance Dashboard</h1>
    <p>Dashboard HTML file not found. Please ensure dashboard.html is in your www directory.</p>
    <p><a href="/">← Back to Home</a> | <a href="/api/docs">📚 API Docs</a></p>
</body>
</html>
)";
        return build_http_response(200, "OK", "text/html", basic_dashboard, false, true);
    }
}

// Utility functions:
std::string WebServer::generate_client_id() const {
    static std::atomic<int> counter{0};
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << "client_" << timestamp << "_" << counter++;
    return oss.str();
}

bool WebServer::is_websocket_path(const std::string& path) const {
    return path == "/ws" || path == "/websocket";
}

size_t WebServer::get_active_connections() const {
    size_t count = 0;
    
    std::unique_lock<std::timed_mutex> lock(connection_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        count = connection_timestamps.size();
    }
    
    if (websocket_handler) {
        count += websocket_handler->get_connection_count();
    }
    
    return count;
}

// Performance metrics functions:
void WebServer::start_metrics_collection() {
    if (metrics_running.exchange(true)) {
        return; // Already running
    }
    
    metrics_thread = std::thread([this]() {
        while (metrics_running && !g_shutdown_requested) {
            try {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                if (g_shutdown_requested) break;
                
                // Collect system metrics
                size_t active_conn = get_active_connections();
                size_t queue_size = thread_pool->get_queue_size();
                size_t thread_count = thread_pool->get_thread_count();
                
                if (performance_metrics && !g_shutdown_requested) {
                    performance_metrics->record_system_metrics(
                        0,              // memory will be auto-detected
                        -1.0,           // cpu will be auto-calculated
                        active_conn,
                        queue_size,
                        thread_count
                    );
                }
            } catch (const std::exception& e) {
                if (!g_shutdown_requested) {
                    std::cerr << "Metrics collection error: " << e.what() << std::endl;
                }
                break;
            }
        }
    });
}

void WebServer::stop_metrics_collection() {
    metrics_running = false;
    
    if (metrics_thread.joinable()) {
        // Give the thread a moment to exit gracefully
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        metrics_thread.join();
    }
}

void WebServer::metrics_collection_loop() {
    while (metrics_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Collect system metrics
        size_t active_conn = get_active_connections();
        size_t queue_size = thread_pool->get_queue_size();
        size_t thread_count = thread_pool->get_thread_count();
        
        performance_metrics->record_system_metrics(
            0,              // memory will be auto-detected
            -1.0,           // cpu will be auto-calculated
            active_conn,
            queue_size,
            thread_count
        );
    }
}

void WebServer::record_request_metric(const std::string& method, const std::string& path, 
                                     int status_code, double response_time_ms) {
    if (performance_metrics && !g_shutdown_requested) {
        performance_metrics->record_request(method, path, status_code, response_time_ms);
    }
}

// HTTP/2 Support Implementation
void WebServer::enable_http2(bool enable) {
    http2_enabled = enable;
    if (enable) {
        safe_cout("HTTP/2 support enabled");
    } else {
        safe_cout("HTTP/2 support disabled");
    }
}

bool WebServer::detect_http2_preface(int client_socket) {
    char buffer[24]; // HTTP/2 connection preface is 24 bytes
    
    // Set a short timeout for preface detection
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), MSG_PEEK);
    
    // Reset timeout to default
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    if (bytes_received == 24) {
        return memcmp(buffer, HTTP2_CONNECTION_PREFACE, 24) == 0;
    }
    
    return false;
}

void WebServer::handle_http2_connection(int client_socket, const char* initial_data, size_t initial_len) {
    try {
        // Create HTTP/2 handler
        auto http2_handler = std::make_unique<HTTP2Handler>(
            client_socket, 
            std::shared_ptr<FileHandler>(file_handler.get(), [](FileHandler*) {}),
            performance_metrics,
            document_root
        );
        
        if (!http2_handler->initialize()) {
            safe_cout("Failed to initialize HTTP/2 handler");
            return;
        }
        
        safe_cout("HTTP/2 connection established");
        
        // Process initial data if provided (contains the preface and possibly more)
        if (initial_data && initial_len > 0) {
            if (http2_handler->process_data(reinterpret_cast<const uint8_t*>(initial_data), initial_len) < 0) {
                safe_cout("HTTP/2 initial data processing error");
                return;
            }
        }
        
        // Process HTTP/2 frames
        char buffer[8192];
        while (!g_shutdown_requested && 
               (http2_handler->session_want_read() || http2_handler->session_want_write())) {
            
            if (http2_handler->session_want_read()) {
                ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
                if (bytes_received <= 0) {
                    if (bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        safe_cout("HTTP/2 connection read error");
                    }
                    break;
                }
                
                if (http2_handler->process_data(reinterpret_cast<uint8_t*>(buffer), bytes_received) < 0) {
                    safe_cout("HTTP/2 data processing error");
                    break;
                }
            }
            
            if (http2_handler->session_want_write()) {
                if (!http2_handler->flush_output()) {
                    safe_cout("HTTP/2 output flush error");
                    break;
                }
            }
        }
        
        safe_cout("HTTP/2 connection closed");
        
    } catch (const std::exception& e) {
        safe_cout("HTTP/2 connection error: " + std::string(e.what()));
    }
}

bool WebServer::send_http2_upgrade_response(int client_socket) {
    std::string upgrade_response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: h2c\r\n"
        "\r\n";
    
    ssize_t sent = send(client_socket, upgrade_response.c_str(), upgrade_response.length(), 0);
    return sent == static_cast<ssize_t>(upgrade_response.length());
}

// TLS/ALPN Implementation
void WebServer::enable_tls(bool enable, const std::string& cert_file, const std::string& key_file) {
    tls_enabled.store(enable);
    if (enable) {
        this->cert_file = cert_file;
        this->key_file = key_file;
        if (!initialize_ssl_context()) {
            safe_cout("Failed to initialize SSL context");
            tls_enabled.store(false);
        }
    } else {
        cleanup_ssl_context();
    }
}

bool WebServer::initialize_ssl_context() {
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create SSL context
    ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx) {
        safe_cout("Failed to create SSL context");
        return false;
    }
    
    // Set certificate and key files (if provided)
    if (!cert_file.empty() && !key_file.empty()) {
        if (SSL_CTX_use_certificate_file(ssl_ctx, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            safe_cout("Failed to load certificate file: " + cert_file);
            cleanup_ssl_context();
            return false;
        }
        
        if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            safe_cout("Failed to load private key file: " + key_file);
            cleanup_ssl_context();
            return false;
        }
        
        if (!SSL_CTX_check_private_key(ssl_ctx)) {
            safe_cout("Private key does not match certificate");
            cleanup_ssl_context();
            return false;
        }
    }
    
    // Set ALPN callback for protocol negotiation
    SSL_CTX_set_alpn_select_cb(ssl_ctx, alpn_select_callback, this);
    
    // Set supported protocols (HTTP/2 and HTTP/1.1)
    const unsigned char protocols[] = {
        2, 'h', '2',           // HTTP/2
        8, 'h', 't', 't', 'p', '/', '1', '.', '1'  // HTTP/1.1
    };
    
    if (SSL_CTX_set_alpn_protos(ssl_ctx, protocols, sizeof(protocols)) != 0) {
        safe_cout("Failed to set ALPN protocols");
        cleanup_ssl_context();
        return false;
    }
    
    safe_cout("SSL context initialized with ALPN support");
    return true;
}

void WebServer::cleanup_ssl_context() {
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }
    EVP_cleanup();
}

SSL* WebServer::create_ssl_connection(int client_socket) {
    if (!ssl_ctx) {
        return nullptr;
    }
    
    SSL* ssl = SSL_new(ssl_ctx);
    if (!ssl) {
        safe_cout("Failed to create SSL connection");
        return nullptr;
    }
    
    if (SSL_set_fd(ssl, client_socket) != 1) {
        safe_cout("Failed to set SSL file descriptor");
        SSL_free(ssl);
        return nullptr;
    }
    
    return ssl;
}

bool WebServer::perform_alpn_negotiation(SSL* ssl, std::string& selected_protocol) {
    if (SSL_accept(ssl) <= 0) {
        safe_cout("SSL handshake failed");
        return false;
    }
    
    const unsigned char* alpn_selected;
    unsigned int alpn_len;
    SSL_get0_alpn_selected(ssl, &alpn_selected, &alpn_len);
    
    if (alpn_selected && alpn_len > 0) {
        selected_protocol = std::string(reinterpret_cast<const char*>(alpn_selected), alpn_len);
        safe_cout("ALPN negotiated protocol: " + selected_protocol);
        return true;
    }
    
    // Default to HTTP/1.1 if no ALPN negotiation
    selected_protocol = "http/1.1";
    safe_cout("No ALPN negotiation, defaulting to HTTP/1.1");
    return true;
}

void WebServer::handle_tls_connection(int client_socket) {
    SSL* ssl = create_ssl_connection(client_socket);
    if (!ssl) {
        safe_cout("Failed to create SSL connection");
        return;
    }
    
    std::string selected_protocol;
    if (!perform_alpn_negotiation(ssl, selected_protocol)) {
        safe_cout("TLS handshake or ALPN negotiation failed");
        SSL_free(ssl);
        return;
    }
    
    // Route to appropriate handler based on negotiated protocol
    if (selected_protocol == "h2") {
        safe_cout("Handling HTTP/2 over TLS connection");
        // TODO: Implement HTTP/2 over TLS with SSL wrapper
        // For now, inform client about the limitation
        safe_cout("HTTP/2 over TLS not fully implemented yet");
    } else {
        safe_cout("Handling HTTP/1.1 over TLS connection");
        // TODO: Implement HTTP/1.1 over TLS with SSL wrapper
        // For now, inform client about the limitation  
        safe_cout("HTTP/1.1 over TLS not fully implemented yet");
    }
    
    SSL_shutdown(ssl);
    SSL_free(ssl);
}

int WebServer::alpn_select_callback(SSL* ssl, const unsigned char** out, unsigned char* outlen,
                                   const unsigned char* in, unsigned int inlen, void* arg) {
    (void)ssl;
    WebServer* server = static_cast<WebServer*>(arg);
    
    // Preferred protocols in order of preference
    const unsigned char h2[] = {2, 'h', '2'};
    const unsigned char http11[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    
    // Check for HTTP/2 first (if enabled)
    if (server->is_http2_enabled()) {
        for (unsigned int i = 0; i < inlen; ) {
            unsigned char proto_len = in[i];
            if (i + 1 + proto_len > inlen) break;
            
            if (proto_len == h2[0] && memcmp(&in[i], h2, sizeof(h2)) == 0) {
                *out = &in[i + 1];
                *outlen = proto_len;
                return SSL_TLSEXT_ERR_OK;
            }
            
            i += 1 + proto_len;
        }
    }
    
    // Fall back to HTTP/1.1
    for (unsigned int i = 0; i < inlen; ) {
        unsigned char proto_len = in[i];
        if (i + 1 + proto_len > inlen) break;
        
        if (proto_len == http11[0] && memcmp(&in[i], http11, sizeof(http11)) == 0) {
            *out = &in[i + 1];
            *outlen = proto_len;
            return SSL_TLSEXT_ERR_OK;
        }
        
        i += 1 + proto_len;
    }
    
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}