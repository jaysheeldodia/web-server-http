#include "../include/server.h"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <thread>
#include <sstream>
#include <iomanip>
#include <algorithm>

WebServer::WebServer(int port, const std::string& doc_root, size_t thread_count) 
    : server_fd(-1), port(port), document_root(doc_root), 
      keep_alive_enabled(false), connection_timeout(5), total_requests(0), next_user_id(1) {
    memset(&address, 0, sizeof(address));
    file_handler = std::make_unique<FileHandler>(document_root);
    thread_pool = std::make_unique<ThreadPool>(thread_count);
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
    if (listen(server_fd, 128) < 0) { // Increased backlog
        std::cerr << "Listen failed: " << strerror(errno) << std::endl;
        close(server_fd);
        server_fd = -1;
        return false;
    }

    safe_cout("Server initialized on port " + std::to_string(port));
    safe_cout("API endpoints available at /api/");
    return true;
}

void WebServer::start() {
    safe_cout("Server starting on http://localhost:" + std::to_string(port));
    safe_cout("Document root: " + document_root);
    safe_cout("Thread pool size: " + std::to_string(thread_pool->get_thread_count()));
    safe_cout("Keep-Alive: " + std::string(keep_alive_enabled ? "enabled" : "disabled"));
    safe_cout("API Documentation: http://localhost:" + std::to_string(port) + "/api/docs");
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
            
            // First, read headers to parse the request
            std::string headers_data;
            char buffer[4096];
            
            // Read until we have complete headers (look for \r\n\r\n)
            while (headers_data.find("\r\n\r\n") == std::string::npos) {
                ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    headers_data += std::string(buffer);
                    
                    // Prevent excessive memory usage
                    if (headers_data.size() > 8192) { // 8KB limit for headers
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
            
            if (headers_data.empty()) {
                std::ostringstream oss;
                oss << "[Thread " << thread_id << "] Empty request received";
                safe_cout(oss.str());
                break;
            }

            // Parse HTTP request headers first
            HttpRequest request;
            if (!request.parse(headers_data)) {
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

            // For POST requests, we might need to read the body if it wasn't fully received
            if ((request.method == "POST" || request.method == "PUT") && request.get_content_length() > 0) {
                size_t expected_length = request.get_content_length();
                size_t current_body_length = request.body.length();
                
                if (current_body_length < expected_length) {
                    // Read remaining body
                    size_t remaining = expected_length - current_body_length;
                    std::string additional_body;
                    
                    while (additional_body.length() < remaining) {
                        ssize_t bytes_received = recv(client_socket, buffer, 
                                                    std::min(sizeof(buffer) - 1, remaining - additional_body.length()), 0);
                        if (bytes_received > 0) {
                            buffer[bytes_received] = '\0';
                            additional_body += std::string(buffer);
                        } else {
                            break; // Timeout or error
                        }
                    }
                    
                    request.body += additional_body;
                }
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
    // Check if it's an API request
    if (is_api_path(request.path)) {
        return handle_api_request(request, keep_alive);
    }
    
    // Regular static file serving
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
        std::lock_guard<std::mutex> lock(data_mutex);
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
        std::lock_guard<std::mutex> lock(data_mutex);
        
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
        <pre>Response: {
  "success": true,
  "message": "Server statistics",
  "data": {
    "total_requests": 1234,
    "active_connections": 5,
    "thread_count": 4,
    "queue_size": 0
  }
}</pre>
    </div>

    <h2>👥 User Management</h2>
    <div class="endpoint">
        <span class="method get">GET</span> <span class="url">/api/users</span>
        <p>Get all users</p>
        <pre>Response: {
  "success": true,
  "message": "Users list retrieved",
  "data": [
    {"id": "1", "name": "John Doe", "email": "john@example.com"},
    {"id": "2", "name": "Jane Smith", "email": "jane@example.com"}
  ]
}</pre>
    </div>

    <div class="endpoint">
        <span class="method post">POST</span> <span class="url">/api/users</span>
        <p>Create a new user</p>
        <pre>Request: {
  "name": "New User",
  "email": "user@example.com"
}

Response: {
  "success": true,
  "message": "User created successfully",
  "data": {
    "id": 3,
    "name": "New User",
    "email": "user@example.com"
  }
}</pre>
    </div>

    <div class="endpoint">
        <span class="method get">GET</span> <span class="url">/api/users/{id}</span>
        <p>Get a specific user by ID</p>
        <pre>Response: {
  "success": true,
  "message": "User data retrieved",
  "data": {
    "id": 1,
    "name": "John Doe",
    "email": "john@example.com"
  }
}</pre>
    </div>

    <h2>🧪 Test the API</h2>
    <p>You can test these endpoints using:</p>
    <ul>
        <li><strong>Browser:</strong> Visit the GET endpoints directly</li>
        <li><strong>curl:</strong> <code>curl -X POST http://localhost:)" + std::to_string(port) + R"(/api/users -H "Content-Type: application/json" -d '{"name":"Test User","email":"test@example.com"}'</code></li>
        <li><strong>Postman:</strong> Import the endpoints and test with a UI</li>
    </ul>

    <p><a href="/">← Back to Home</a> | <a href="/api/stats">View Live Stats</a></p>
</body>
</html>
)";
    
    return build_http_response(200, "OK", "text/html", docs_html, true, true);
}

std::string WebServer::handle_options_request(const HttpRequest& request) {
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

void WebServer::initialize_sample_data() {
    std::lock_guard<std::mutex> lock(data_mutex);
    
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
    std::lock_guard<std::mutex> lock(data_mutex);
    
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