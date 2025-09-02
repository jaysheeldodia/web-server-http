#include "../../include/handlers/websocket_handler.h"
#include "../../include/core/shutdown_coordinator.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <future>
#include <fcntl.h>

// SHA1 and Base64 implementation for WebSocket handshake
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

// PerformanceMetrics Implementation
void PerformanceMetrics::record_request(const std::string& method, const std::string& path, 
                                       int status_code, double response_time_ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    RequestMetric metric;
    metric.timestamp = std::chrono::steady_clock::now();
    metric.response_time_ms = response_time_ms;
    metric.status_code = status_code;
    metric.method = method;
    metric.path = path;
    
    request_history.push(metric);
    
    // Keep only last MAX_REQUEST_HISTORY requests
    while (request_history.size() > MAX_REQUEST_HISTORY) {
        request_history.pop();
    }
    
    total_requests++;
    requests_last_minute++;
    update_request_rate();
}

void PerformanceMetrics::record_system_metrics(size_t memory_mb, double cpu_percent, 
                                              size_t active_connections, size_t queue_size, size_t thread_count) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    SystemMetric metric;
    metric.timestamp = std::chrono::steady_clock::now();
    metric.memory_usage_mb = memory_mb > 0 ? memory_mb : get_memory_usage();
    metric.cpu_usage_percent = cpu_percent >= 0 ? cpu_percent : get_cpu_usage();
    metric.active_connections = active_connections;
    metric.total_requests = total_requests.load();
    metric.requests_per_second = static_cast<double>(requests_last_minute.load()) / 60.0;
    metric.queue_size = queue_size;
    metric.thread_count = thread_count;
    
    system_history.push(metric);
    
    // Keep only last MAX_SYSTEM_HISTORY metrics
    while (system_history.size() > MAX_SYSTEM_HISTORY) {
        system_history.pop();
    }
}

void PerformanceMetrics::update_request_rate() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_minute_reset);
    
    if (elapsed.count() >= 1) {
        requests_last_minute = 0;
        last_minute_reset = now;
    }
}

size_t PerformanceMetrics::get_memory_usage() const {
    // Read memory usage from /proc/self/status on Linux
    std::ifstream status_file("/proc/self/status");
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream iss(line);
            std::string key, value, unit;
            iss >> key >> value >> unit;
            return std::stoul(value) / 1024; // Convert KB to MB
        }
    }
    return 0;
}

double PerformanceMetrics::get_cpu_usage() const {
    // Simplified CPU usage - in production, you'd want more accurate measurement
    static auto last_time = std::chrono::steady_clock::now();
    static size_t last_requests = 0;
    
    auto now = std::chrono::steady_clock::now();
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_time).count();
    
    if (time_diff > 0) {
        size_t current_requests = total_requests.load();
        double request_rate = static_cast<double>(current_requests - last_requests) / time_diff;
        last_requests = current_requests;
        last_time = now;
        
        // Estimate CPU usage based on request rate (this is a rough approximation)
        return std::min(100.0, request_rate * 0.5);
    }
    
    return 0.0;
}

std::string PerformanceMetrics::get_metrics_json() const {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    std::ostringstream json;
    json << "{";
    json << "\"type\":\"metrics\",";
    json << "\"data\":{";
    json << "\"total_requests\":" << total_requests.load() << ",";
    json << "\"requests_per_minute\":" << requests_last_minute.load() << ",";
    json << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    json << "}";
    json << "}";
    
    return json.str();
}

std::string PerformanceMetrics::get_request_rate_json() const {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    std::ostringstream json;
    json << "{";
    json << "\"type\":\"request_rate\",";
    json << "\"data\":[";
    
    // Create request rate data for last 60 seconds
    auto now = std::chrono::steady_clock::now();
    std::map<int, int> request_counts;
    
    // Initialize all seconds to 0
    for (int i = 0; i < 60; i++) {
        request_counts[i] = 0;
    }
    
    // Count requests per second for last minute
    std::queue<RequestMetric> temp_history = request_history;
    while (!temp_history.empty()) {
        const auto& metric = temp_history.front();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - metric.timestamp).count();
        
        if (age < 60) {
            request_counts[static_cast<int>(age)]++;
        }
        
        temp_history.pop();
    }
    
    bool first = true;
    for (int i = 59; i >= 0; i--) {
        if (!first) json << ",";
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            (now - std::chrono::seconds(i)).time_since_epoch()).count();
        json << "{\"timestamp\":" << timestamp << ",\"count\":" << request_counts[i] << "}";
        first = false;
    }
    
    json << "]}";
    return json.str();
}

std::string PerformanceMetrics::get_system_metrics_json() const {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    if (system_history.empty()) {
        return "{\"type\":\"system_metrics\",\"data\":[]}";
    }
    
    std::ostringstream json;
    json << "{";
    json << "\"type\":\"system_metrics\",";
    json << "\"data\":[";
    
    std::queue<SystemMetric> temp_history = system_history;
    bool first = true;
    
    while (!temp_history.empty()) {
        if (!first) json << ",";
        
        const auto& metric = temp_history.front();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            metric.timestamp.time_since_epoch()).count();
        
        json << "{";
        json << "\"timestamp\":" << timestamp << ",";
        json << "\"memory_mb\":" << metric.memory_usage_mb << ",";
        json << "\"cpu_percent\":" << std::fixed << std::setprecision(2) << metric.cpu_usage_percent << ",";
        json << "\"active_connections\":" << metric.active_connections << ",";
        json << "\"total_requests\":" << metric.total_requests << ",";
        json << "\"requests_per_second\":" << std::fixed << std::setprecision(2) << metric.requests_per_second << ",";
        json << "\"queue_size\":" << metric.queue_size << ",";
        json << "\"thread_count\":" << metric.thread_count;
        json << "}";
        
        temp_history.pop();
        first = false;
    }
    
    json << "]}";
    return json.str();
}

// WebSocketHandler Implementation
WebSocketHandler::WebSocketHandler() : metrics(std::make_shared<PerformanceMetrics>()) {}

WebSocketHandler::~WebSocketHandler() {
    stop();
}

bool WebSocketHandler::is_websocket_request(const std::map<std::string, std::string>& headers) const {
    auto connection_it = headers.find("connection");
    auto upgrade_it = headers.find("upgrade");
    auto ws_key_it = headers.find("sec-websocket-key");
    
    return connection_it != headers.end() && 
           upgrade_it != headers.end() &&
           ws_key_it != headers.end() &&
           connection_it->second.find("Upgrade") != std::string::npos &&
           upgrade_it->second == "websocket";
}

std::string WebSocketHandler::generate_websocket_response(const std::map<std::string, std::string>& headers) const {
    auto ws_key_it = headers.find("sec-websocket-key");
    if (ws_key_it == headers.end()) {
        return "";
    }
    
    // WebSocket magic string as per RFC 6455
    std::string magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = ws_key_it->second + magic_string;
    
    // SHA1 hash
    std::string accept_key = base64_encode(sha1_hash(combined));
    
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n";
    response << "Upgrade: websocket\r\n";
    response << "Connection: Upgrade\r\n";
    response << "Sec-WebSocket-Accept: " << accept_key << "\r\n";
    response << "\r\n";
    
    return response.str();
}

std::string WebSocketHandler::base64_encode(const std::string& input) const {
    BIO *bio, *b64;
    BUF_MEM *buffer_ptr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input.c_str(), input.length());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);
    
    return result;
}

std::string WebSocketHandler::sha1_hash(const std::string& input) const {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);
    return std::string(reinterpret_cast<char*>(hash), SHA_DIGEST_LENGTH);
}

bool WebSocketHandler::handle_websocket_connection(int client_socket, const std::string& client_id) {
    add_connection(client_socket, client_id);
    
    std::vector<uint8_t> buffer(4096);
    
    while (running.load()) {
        // Set socket to non-blocking for graceful shutdown
        int flags = fcntl(client_socket, F_GETFL, 0);
        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
        
        ssize_t bytes_received = recv(client_socket, buffer.data(), buffer.size(), 0);
        
        if (bytes_received > 0) {
            buffer.resize(bytes_received);
            WebSocketFrame frame = parse_frame(buffer);
            
            if (frame.opcode == WS_OPCODE_CLOSE || !running.load()) {
                break;
            } else if (frame.opcode == WS_OPCODE_PING) {
                if (!send_pong(client_socket) || !running.load()) {
                    break;
                }
            } else if (frame.opcode == WS_OPCODE_TEXT && running.load()) {
                std::string message(frame.payload.begin(), frame.payload.end());
                
                if (message == "request_metrics" && metrics) {
                    send_message_to_client(client_id, metrics->get_metrics_json());
                } else if (message == "request_rate" && metrics) {
                    send_message_to_client(client_id, metrics->get_request_rate_json());
                } else if (message == "system_metrics" && metrics) {
                    send_message_to_client(client_id, metrics->get_system_metrics_json());
                }
            }
            
            buffer.resize(4096);
            
        } else if (bytes_received == 0) {
            // Connection closed by client
            break;
        } else {
            // Error or would block
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // No data available, sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                // Real error
                if (running.load()) {
                    std::cerr << "WebSocket recv error: " << strerror(errno) << std::endl;
                }
                break;
            }
        }
    }
    
    remove_connection(client_id);
    close(client_socket);
    return true;
}

WebSocketHandler::WebSocketFrame WebSocketHandler::parse_frame(const std::vector<uint8_t>& data) const {
    WebSocketFrame frame;
    
    if (data.size() < 2) {
        frame.fin = false;
        frame.opcode = 0;
        return frame;
    }
    
    frame.fin = (data[0] & 0x80) != 0;
    frame.opcode = data[0] & 0x0F;
    frame.masked = (data[1] & 0x80) != 0;
    
    size_t payload_start = 2;
    uint64_t payload_len = data[1] & 0x7F;
    
    if (payload_len == 126) {
        if (data.size() < 4) return frame;
        payload_len = (data[2] << 8) | data[3];
        payload_start = 4;
    } else if (payload_len == 127) {
        if (data.size() < 10) return frame;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len |= static_cast<uint64_t>(data[2 + i]) << (8 * (7 - i));
        }
        payload_start = 10;
    }
    
    frame.payload_length = payload_len;
    
    if (frame.masked) {
        if (data.size() < payload_start + 4) return frame;
        std::memcpy(frame.mask, &data[payload_start], 4);
        payload_start += 4;
    }
    
    if (data.size() < payload_start + payload_len) return frame;
    
    frame.payload.resize(payload_len);
    for (uint64_t i = 0; i < payload_len; i++) {
        if (frame.masked) {
            frame.payload[i] = data[payload_start + i] ^ frame.mask[i % 4];
        } else {
            frame.payload[i] = data[payload_start + i];
        }
    }
    
    return frame;
}

std::vector<uint8_t> WebSocketHandler::create_frame(uint8_t opcode, const std::string& payload) const {
    std::vector<uint8_t> frame;
    
    // First byte: FIN=1, RSV=0, Opcode
    frame.push_back(0x80 | opcode);
    
    // Payload length
    uint64_t payload_len = payload.length();
    if (payload_len < 126) {
        frame.push_back(static_cast<uint8_t>(payload_len));
    } else if (payload_len < 65536) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>(payload_len >> 8));
        frame.push_back(static_cast<uint8_t>(payload_len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<uint8_t>((payload_len >> (8 * i)) & 0xFF));
        }
    }
    
    // Payload
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    return frame;
}

bool WebSocketHandler::send_frame(int socket, uint8_t opcode, const std::string& payload) const {
    auto frame = create_frame(opcode, payload);
    ssize_t sent = send(socket, frame.data(), frame.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(frame.size());
}

bool WebSocketHandler::send_ping(int socket) const {
    return send_frame(socket, WS_OPCODE_PING, "");
}

bool WebSocketHandler::send_pong(int socket) const {
    return send_frame(socket, WS_OPCODE_PONG, "");
}

void WebSocketHandler::add_connection(int socket, const std::string& client_id) {
    std::lock_guard<std::timed_mutex> lock(connections_mutex);
    connections[client_id] = std::make_shared<WebSocketConnection>(socket, client_id);
}

void WebSocketHandler::remove_connection(const std::string& client_id) {
    std::lock_guard<std::timed_mutex> lock(connections_mutex);
    connections.erase(client_id);
}

void WebSocketHandler::broadcast_message(const std::string& message) {
    std::unique_lock<std::timed_mutex> lock(connections_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !running) {
        return; // Avoid deadlock during shutdown
    }
    
    std::vector<std::string> dead_connections;
    
    // C++14 compatible iteration
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        const std::string& client_id = it->first;
        const std::shared_ptr<WebSocketConnection>& conn = it->second;
        
        if (!running) break; // Exit early if shutting down
        
        if (!send_frame(conn->socket, WS_OPCODE_TEXT, message)) {
            dead_connections.push_back(client_id);
        }
    }
    
    // Remove dead connections
    for (const auto& client_id : dead_connections) {
        connections.erase(client_id);
    }
}


void WebSocketHandler::send_message_to_client(const std::string& client_id, const std::string& message) {
    std::unique_lock<std::timed_mutex> lock(connections_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !running) {
        return;
    }
    
    auto it = connections.find(client_id);
    if (it != connections.end()) {
        if (!send_frame(it->second->socket, WS_OPCODE_TEXT, message)) {
            connections.erase(it);
        }
    }
}

void WebSocketHandler::set_metrics(std::shared_ptr<PerformanceMetrics> perf_metrics) {
    metrics = perf_metrics;
}

void WebSocketHandler::record_request(const std::string& method, const std::string& path, 
                                    int status_code, double response_time_ms) {
    if (metrics) {
        metrics->record_request(method, path, status_code, response_time_ms);
    }
}

void WebSocketHandler::start() {
    running = true;
    
    // Create threads directly without shared_ptr complications
    broadcast_thread = std::thread([this]() {
        this->broadcast_loop_safe();
    });
    
    ping_thread = std::thread([this]() {
        this->ping_loop_safe();
    });
}


void WebSocketHandler::stop() {
    running = false;
    auto& coordinator = ShutdownCoordinator::instance();
    coordinator.request_shutdown();
    
    // Use timeout-based joining with coordinated shutdown
    const auto timeout = std::chrono::seconds(2);
    
    if (broadcast_thread.joinable()) {
        auto future = std::async(std::launch::async, [this]() {
            broadcast_thread.join();
        });
        
        if (future.wait_for(timeout) == std::future_status::timeout) {
            std::cout << "Broadcast thread timeout - detaching" << std::endl;
            broadcast_thread.detach();
        }
    }
    
    if (ping_thread.joinable()) {
        auto future = std::async(std::launch::async, [this]() {
            ping_thread.join();
        });
        
        if (future.wait_for(timeout) == std::future_status::timeout) {
            std::cout << "Ping thread timeout - detaching" << std::endl;
            ping_thread.detach();
        }
    }
    
    // Force close all connections - critical operation during shutdown
    std::lock_guard<std::timed_mutex> lock(connections_mutex);
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        shutdown(it->second->socket, SHUT_RDWR);
        close(it->second->socket);
    }
    connections.clear();
}

size_t WebSocketHandler::get_connection_count() const {
    std::unique_lock<std::timed_mutex> lock(connections_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        return connections.size();
    }
    return 0; // Return 0 if can't get lock to avoid deadlock
}

void WebSocketHandler::broadcast_loop() {
    while (running.load()) {
        try {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            if (!running.load()) break;
            
            // Check if we have connections before proceeding
            size_t conn_count = 0;
            {
                std::unique_lock<std::timed_mutex> lock(connections_mutex, std::try_to_lock);
                if (lock.owns_lock()) {
                    conn_count = connections.size();
                } else {
                    continue; // Skip this iteration if can't get lock
                }
            }
            
            if (conn_count == 0) continue;
            
            // Broadcast system metrics every second
            if (metrics && running.load()) {
                auto system_metrics = metrics->get_system_metrics_json();
                broadcast_message(system_metrics);
            }
            
            // Broadcast request rate every 5 seconds
            static int counter = 0;
            if (++counter % 5 == 0 && metrics && running.load()) {
                auto request_rate = metrics->get_request_rate_json();
                broadcast_message(request_rate);
            }
            
        } catch (const std::exception& e) {
            if (running.load()) {
                std::cerr << "Broadcast loop error: " << e.what() << std::endl;
            }
            break;
        }
    }
}

void WebSocketHandler::broadcast_loop_safe() {
    auto& coordinator = ShutdownCoordinator::instance();
    
    while (running.load() && !coordinator.is_shutdown_requested()) {
        try {
            // Use coordinated wait instead of plain sleep
            if (coordinator.wait_for_shutdown(std::chrono::seconds(1))) {
                break; // Shutdown requested
            }
            
            if (!running.load()) break;
            
            // Check connection count with timeout-based lock
            size_t conn_count = get_connection_count_safe();
            if (conn_count == 0) continue;
            
            // Broadcast system metrics
            if (metrics && running.load() && !coordinator.is_shutdown_requested()) {
                auto system_metrics = metrics->get_system_metrics_json();
                broadcast_message_safe(system_metrics);
            }
            
            // Broadcast request rate every 5 seconds
            static int counter = 0;
            if (++counter % 5 == 0 && metrics && running.load() && !coordinator.is_shutdown_requested()) {
                auto request_rate = metrics->get_request_rate_json();
                broadcast_message_safe(request_rate);
            }
            
        } catch (const std::exception& e) {
            if (running.load() && !coordinator.is_shutdown_requested()) {
                std::cerr << "Broadcast loop error: " << e.what() << std::endl;
            }
            break;
        }
    }
    
    coordinator.thread_exiting();
}

// In WebSocketHandler::ping_loop():
void WebSocketHandler::ping_loop() {
    while (running.load()) {
        try {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            if (!running.load()) break;
            
            std::vector<std::string> dead_connections;
            
            {
                std::unique_lock<std::timed_mutex> lock(connections_mutex, std::try_to_lock);
                if (!lock.owns_lock() || !running.load()) {
                    continue;
                }
                
                // C++14 compatible iteration
                for (auto it = connections.begin(); it != connections.end(); ++it) {
                    const std::string& client_id = it->first;
                    const std::shared_ptr<WebSocketConnection>& conn = it->second;
                    
                    if (!running.load()) break;
                    
                    if (!send_ping(conn->socket)) {
                        dead_connections.push_back(client_id);
                    } else {
                        conn->last_ping = std::chrono::steady_clock::now();
                    }
                }
                
                // Remove dead connections
                for (const auto& client_id : dead_connections) {
                    connections.erase(client_id);
                }
            }
            
        } catch (const std::exception& e) {
            if (running.load()) {
                std::cerr << "Ping loop error: " << e.what() << std::endl;
            }
            break;
        }
    }
}


void WebSocketHandler::ping_loop_safe() {
    auto& coordinator = ShutdownCoordinator::instance();
    
    while (running.load() && !coordinator.is_shutdown_requested()) {
        try {
            // Use coordinated wait with longer timeout for pings
            if (coordinator.wait_for_shutdown(std::chrono::seconds(30))) {
                break; // Shutdown requested
            }
            
            if (!running.load()) break;
            
            std::vector<std::string> dead_connections;
            
            // Try to get connections lock with timeout
            std::unique_lock<std::timed_mutex> lock(connections_mutex, std::defer_lock);
            if (!lock.try_lock_for(std::chrono::milliseconds(500))) {
                continue; // Skip this ping cycle if we can't get the lock
            }
            
            if (!running.load() || coordinator.is_shutdown_requested()) {
                break;
            }
            
            // Send pings to all connections
            for (auto it = connections.begin(); it != connections.end(); ++it) {
                if (!running.load() || coordinator.is_shutdown_requested()) break;
                
                const std::string& client_id = it->first;
                const std::shared_ptr<WebSocketConnection>& conn = it->second;
                
                if (!send_ping(conn->socket)) {
                    dead_connections.push_back(client_id);
                } else {
                    conn->last_ping = std::chrono::steady_clock::now();
                }
            }
            
            // Remove dead connections
            for (const auto& client_id : dead_connections) {
                connections.erase(client_id);
            }
            
            lock.unlock();
            
        } catch (const std::exception& e) {
            if (running.load() && !coordinator.is_shutdown_requested()) {
                std::cerr << "Ping loop error: " << e.what() << std::endl;
            }
            break;
        }
    }
    
    coordinator.thread_exiting();
}

void WebSocketHandler::broadcast_message_safe(const std::string& message) {
    if (!running.load() || ShutdownCoordinator::instance().is_shutdown_requested()) {
        return;
    }
    
    std::unique_lock<std::timed_mutex> lock(connections_mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(100))) {
        return; // Skip broadcast if we can't get lock quickly
    }
    
    std::vector<std::string> dead_connections;
    
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (!running.load() || ShutdownCoordinator::instance().is_shutdown_requested()) {
            break;
        }
        
        const std::string& client_id = it->first;
        const std::shared_ptr<WebSocketConnection>& conn = it->second;
        
        if (!send_frame(conn->socket, WS_OPCODE_TEXT, message)) {
            dead_connections.push_back(client_id);
        }
    }
    
    // Remove dead connections
    for (const auto& client_id : dead_connections) {
        connections.erase(client_id);
    }
}

void WebSocketHandler::send_message_to_client_safe(const std::string& client_id, const std::string& message) {
    if (!running.load() || ShutdownCoordinator::instance().is_shutdown_requested()) {
        return;
    }
    
    std::unique_lock<std::timed_mutex> lock(connections_mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(100))) {
        return;
    }
    
    auto it = connections.find(client_id);
    if (it != connections.end()) {
        if (!send_frame(it->second->socket, WS_OPCODE_TEXT, message)) {
            connections.erase(it);
        }
    }
}

size_t WebSocketHandler::get_connection_count_safe() const {
    std::unique_lock<std::timed_mutex> lock(connections_mutex, std::defer_lock);
    if (lock.try_lock_for(std::chrono::milliseconds(10))) {
        return connections.size();
    }
    return 0; // Return 0 if we can't get the lock quickly
}

bool WebSocketHandler::handle_websocket_connection_safe(int client_socket, const std::string& client_id) {
    add_connection(client_socket, client_id);
    
    std::vector<uint8_t> buffer(4096);
    auto& coordinator = ShutdownCoordinator::instance();
    
    while (running.load() && !coordinator.is_shutdown_requested()) {
        // Use select with timeout for non-blocking receive
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;
        
        int select_result = select(client_socket + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (select_result < 0) {
            break; // Error
        }
        
        if (select_result == 0) {
            continue; // Timeout, check shutdown and continue
        }
        
        if (!FD_ISSET(client_socket, &read_fds)) {
            continue;
        }
        
        ssize_t bytes_received = recv(client_socket, buffer.data(), buffer.size(), 0);
        
        if (bytes_received > 0) {
            buffer.resize(bytes_received);
            WebSocketFrame frame = parse_frame(buffer);
            
            if (frame.opcode == WS_OPCODE_CLOSE || !running.load() || coordinator.is_shutdown_requested()) {
                break;
            } else if (frame.opcode == WS_OPCODE_PING) {
                if (!send_pong(client_socket) || !running.load()) {
                    break;
                }
            } else if (frame.opcode == WS_OPCODE_TEXT && running.load() && !coordinator.is_shutdown_requested()) {
                std::string message(frame.payload.begin(), frame.payload.end());
                
                if (message == "request_metrics" && metrics) {
                    send_message_to_client_safe(client_id, metrics->get_metrics_json());
                } else if (message == "request_rate" && metrics) {
                    send_message_to_client_safe(client_id, metrics->get_request_rate_json());
                } else if (message == "system_metrics" && metrics) {
                    send_message_to_client_safe(client_id, metrics->get_system_metrics_json());
                }
            }
            
            buffer.resize(4096);
        } else if (bytes_received == 0) {
            break; // Connection closed
        } else {
            break; // Error
        }
    }
    
    remove_connection(client_id);
    close(client_socket);
    return true;
}