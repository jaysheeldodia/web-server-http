#include "../include/server.h"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <thread>
#include <sstream>
#include <iomanip>

WebServer::WebServer(int port, const std::string& doc_root, size_t thread_count) 
    : server_fd(-1), port(port), document_root(doc_root), 
      keep_alive_enabled(false), connection_timeout(5), total_requests(0) {
    memset(&address, 0, sizeof(address));
    file_handler = std::make_unique<FileHandler>(document_root);
    thread_pool = std::make_unique<ThreadPool>(thread_count);
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
    if (listen(server_fd, 128) < 0) { // Increased backlog
        std::cerr << "Listen failed: " << strerror(errno) << std::endl;
        close(server_fd);
        server_fd = -1;
        return false;
    }

    safe_cout("Server initialized on port " + std::to_string(port));
    return true;
}

void WebServer::start() {
    safe_cout("Server starting on http://localhost:" + std::to_string(port));
    safe_cout("Document root: " + document_root);
    safe_cout("Thread pool size: " + std::to_string(thread_pool->get_thread_count()));
    safe_cout("Keep-Alive: " + std::string(keep_alive_enabled ? "enabled" : "disabled"));
    safe_cout("Press Ctrl+C to stop the server");

    // Start connection cleanup thread if keep-alive is enabled
    std::thread cleanup_thread;
    if (keep_alive_enabled) {
        cleanup_thread = std::thread([this]() {
            while (keep_alive_enabled) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                manage_connections();
            }
        });
    }

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (errno == EINTR) continue; // Interrupted by signal
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            continue;
        }

        // Set socket timeout for safety
        struct timeval timeout;
        timeout.tv_sec = 30; // 30 second timeout
        timeout.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        std::ostringstream oss;
        oss << "New connection from " << inet_ntoa(client_addr.sin_addr) 
            << ":" << ntohs(client_addr.sin_port) 
            << " (queue size: " << thread_pool->get_queue_size() << ")";
        safe_cout(oss.str());

        // Add to connection tracking if keep-alive is enabled
        if (keep_alive_enabled) {
            add_connection(client_socket);
        }

        // Add client handling to thread pool
        thread_pool->enqueue([this, client_socket]() {
            this->handle_client_task(client_socket);
        });
    }

    // Clean up connection management thread
    if (cleanup_thread.joinable()) {
        cleanup_thread.join();
    }
}

void WebServer::handle_client_task(int client_socket) {
    std::thread::id thread_id = std::this_thread::get_id();
    bool keep_connection = false;
    
    try {
        do {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Read the request
            std::string request_data = read_request(client_socket);
            
            if (request_data.empty()) {
                std::ostringstream oss;
                oss << "[Thread " << thread_id << "] Empty request received";
                safe_cout(oss.str());
                break;
            }

            // Parse HTTP request
            HttpRequest request;
            if (!request.parse(request_data)) {
                std::ostringstream oss;
                oss << "[Thread " << thread_id << "] Failed to parse HTTP request";
                safe_cout(oss.str());
                
                std::string response = get_400_response();
                send_response(client_socket, response);
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                log_request("INVALID", "INVALID", 400, duration);
                break;
            }

            // Handle the request
            std::string response = handle_request(request, keep_connection);
            send_response(client_socket, response);
            
            // Update connection timestamp if keeping alive
            if (keep_connection && keep_alive_enabled) {
                update_connection_timestamp(client_socket);
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            // Extract status code from response (basic parsing)
            int status_code = 200;
            if (response.find("404") != std::string::npos) status_code = 404;
            else if (response.find("400") != std::string::npos) status_code = 400;
            else if (response.find("405") != std::string::npos) status_code = 405;
            
            log_request(request.method, request.path, status_code, duration);
            total_requests++;

        } while (keep_connection && keep_alive_enabled);
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "[Thread " << thread_id << "] Exception: " << e.what();
        safe_cout(oss.str());
    }
    
    // Clean up connection
    remove_connection(client_socket);
    close(client_socket);
    
    std::ostringstream oss;
    oss << "[Thread " << thread_id << "] Connection closed";
    safe_cout(oss.str());
}

std::string WebServer::handle_request(const HttpRequest& request, bool& keep_alive) {
    keep_alive = should_keep_alive(request);
    
    if (request.method == "GET") {
        return handle_get_request(request, keep_alive);
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

std::string WebServer::read_request(int client_socket) {
    std::string request;
    char buffer[4096];
    
    // Read until we have complete headers (look for \r\n\r\n)
    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            request += std::string(buffer);
            
            // Prevent excessive memory usage
            if (request.size() > 8192) { // 8KB limit for headers
                break;
            }
        } else if (bytes_received == 0) {
            // Client closed connection
            break;
        } else {
            // Error occurred
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "Recv failed: " << strerror(errno) << std::endl;
            }
            break;
        }
    }
    
    return request;
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
                                         bool keep_alive) {
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
    
    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    response << "Date: " << std::put_time(std::gmtime(&time_t), "%a, %d %b %Y %H:%M:%S GMT") << "\r\n";
    
    response << "\r\n";
    response << body;
    
    return response.str();
}

std::string WebServer::handle_get_request(const HttpRequest& request, bool& keep_alive) {
    if (!file_handler->file_exists(request.path)) {
        keep_alive = false;
        return get_404_response();
    }
    
    std::string content = file_handler->read_file(request.path);
    if (content.empty()) {
        keep_alive = false;
        return get_404_response();
    }
    
    // Fix for MIME type detection: when path is "/", treat it as "index.html"
    std::string mime_path = request.path;
    if (request.path == "/") {
        mime_path = "index.html";  // This will give us text/html instead of octet-stream
    }
    
    std::string mime_type = file_handler->get_mime_type(mime_path);
    return build_http_response(200, "OK", mime_type, content, keep_alive);
}

std::string WebServer::get_error_response(int status_code, const std::string& status_text, const std::string& message) {
    std::ostringstream body;
    body << "<!DOCTYPE html>\n"
         << "<html><head><title>" << status_code << " " << status_text << "</title></head>\n"
         << "<body style='font-family: Arial, sans-serif; text-align: center; margin-top: 50px;'>\n"
         << "<h1>" << status_code << " " << status_text << "</h1>\n"
         << "<p>" << message << "</p>\n"
         << "<hr><small>wbeserver-http/1.0</small>\n"
         << "</body></html>";
    
    return build_http_response(status_code, status_text, "text/html", body.str(), false);
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
    std::lock_guard<std::mutex> lock(connection_mutex);
    connection_timestamps[socket] = std::chrono::steady_clock::now();
}

void WebServer::update_connection_timestamp(int socket) {
    std::lock_guard<std::mutex> lock(connection_mutex);
    auto it = connection_timestamps.find(socket);
    if (it != connection_timestamps.end()) {
        it->second = std::chrono::steady_clock::now();
    }
}

void WebServer::remove_connection(int socket) {
    std::lock_guard<std::mutex> lock(connection_mutex);
    connection_timestamps.erase(socket);
}

void WebServer::manage_connections() {
    if (!keep_alive_enabled) return;
    
    std::vector<int> expired_connections;
    auto now = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(connection_mutex);
        for (const auto& conn : connection_timestamps) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - conn.second) > connection_timeout) {
                expired_connections.push_back(conn.first);
            }
        }
        
        // Remove expired connections from tracking
        for (int socket : expired_connections) {
            connection_timestamps.erase(socket);
        }
    }
    
    // Close expired connections
    for (int socket : expired_connections) {
        close(socket);
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
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << message << std::endl;
}

void WebServer::cleanup() {
    keep_alive_enabled.store(false);
    
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
    
    // Close all active connections
    std::lock_guard<std::mutex> lock(connection_mutex);
    for (const auto& conn : connection_timestamps) {
        close(conn.first);
    }
    connection_timestamps.clear();
    
    safe_cout("Server cleanup completed");
}