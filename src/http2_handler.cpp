#include "../include/http2_handler.h"
#include "../include/file_handler.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <algorithm>

const char HTTP2_CONNECTION_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

HTTP2Handler::HTTP2Handler(int socket_fd, std::shared_ptr<FileHandler> file_handler,
                           std::shared_ptr<PerformanceMetrics> metrics,
                           const std::string& doc_root, SSL* ssl)
    : session(nullptr), socket_fd(socket_fd), file_handler(file_handler),
      performance_metrics(metrics), document_root(doc_root), 
      ssl_connection(ssl), is_tls_connection(ssl != nullptr), preface_processed(false) {
}

HTTP2Handler::~HTTP2Handler() {
    if (session) {
        nghttp2_session_del(session);
    }
}

bool HTTP2Handler::initialize() {
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    
    // Set up all callbacks
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, on_frame_send_callback);
    nghttp2_session_callbacks_set_error_callback(callbacks, on_error_callback);
    
    int rv = nghttp2_session_server_new(&session, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    
    if (rv != 0) {
        std::cerr << "Failed to create HTTP/2 session: " << nghttp2_strerror(rv) << std::endl;
        return false;
    }
    
    return send_settings();
}

void HTTP2Handler::setup_callbacks() {
    // Callbacks are set up in initialize()
}

bool HTTP2Handler::send_settings() {
    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65536},
        {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384},
        {NGHTTP2_SETTINGS_ENABLE_PUSH, 1},
        {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, 8192}
    };
    
    int rv = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings, 
                                    sizeof(settings) / sizeof(settings[0]));
    if (rv != 0) {
        std::cerr << "Failed to submit settings: " << nghttp2_strerror(rv) << std::endl;
        return false;
    }
    
    return flush_output();
}

int HTTP2Handler::process_data(const uint8_t* data, size_t len) {
    // For TLS connections, the preface is handled differently by nghttp2
    if (!is_tls_connection) {
        // Check if this is the HTTP/2 connection preface for non-TLS connections
        if (!preface_processed && len >= 24 && memcmp(data, HTTP2_CONNECTION_PREFACE, 24) == 0) {
            // Skip the preface and process any remaining data
            data += 24;
            len -= 24;
            preface_processed = true;
            
            if (len == 0) {
                return 24; // Just the preface, nothing more to process
            }
        }
    } else {
        // For TLS connections, mark preface as processed (handled by nghttp2 internally)
        preface_processed = true;
    }
    
    if (len == 0) {
        return 0;
    }
    
    ssize_t readlen = nghttp2_session_mem_recv(session, data, len);
    if (readlen < 0) {
        std::cerr << "Failed to process HTTP/2 data: " << nghttp2_strerror(readlen) << std::endl;
        return -1;
    }
    
    if (!flush_output()) {
        return -1;
    }
    
    return readlen + (!is_tls_connection && preface_processed ? 24 : 0);
}

bool HTTP2Handler::flush_output() {
    int rv = nghttp2_session_send(session);
    if (rv != 0) {
        std::cerr << "Failed to send HTTP/2 data: " << nghttp2_strerror(rv) << std::endl;
        return false;
    }
    
    // Send buffered data to socket (SSL or regular)
    if (!output_buffer.empty()) {
        ssize_t sent;
        if (is_tls_connection && ssl_connection) {
            // Send over SSL connection
            sent = SSL_write(ssl_connection, output_buffer.data(), output_buffer.size());
            if (sent <= 0) {
                int ssl_error = SSL_get_error(ssl_connection, sent);
                if (ssl_error != SSL_ERROR_WANT_WRITE && ssl_error != SSL_ERROR_WANT_READ) {
                    std::cerr << "Failed to send data over SSL: " << ssl_error << std::endl;
                    return false;
                }
                return true; // Will retry later
            }
        } else {
            // Send over regular socket
            sent = send(socket_fd, output_buffer.data(), output_buffer.size(), 0);
            if (sent < 0) {
                std::cerr << "Failed to send data to socket" << std::endl;
                return false;
            }
        }
        output_buffer.clear();
    }
    
    return true;
}

bool HTTP2Handler::session_want_read() const {
    return nghttp2_session_want_read(session);
}

bool HTTP2Handler::session_want_write() const {
    return nghttp2_session_want_write(session);
}

// Static callback implementations
ssize_t HTTP2Handler::send_callback(nghttp2_session *session, const uint8_t *data,
                                   size_t length, int flags, void *user_data) {
    (void)session;
    (void)flags;
    
    HTTP2Handler* handler = static_cast<HTTP2Handler*>(user_data);
    handler->output_buffer.insert(handler->output_buffer.end(), data, data + length);
    return length;
}

int HTTP2Handler::on_frame_recv_callback(nghttp2_session *session,
                                        const nghttp2_frame *frame, void *user_data) {
    HTTP2Handler* handler = static_cast<HTTP2Handler*>(user_data);
    
    switch (frame->hd.type) {
        case NGHTTP2_HEADERS:
            if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
                auto stream = std::make_unique<HTTP2Stream>(frame->hd.stream_id);
                if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
                    stream->headers_complete = true;
                }
                if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                    stream->request_complete = true;
                }
                handler->streams[frame->hd.stream_id] = std::move(stream);
                
                // Process request if complete
                if (handler->streams[frame->hd.stream_id]->request_complete) {
                    handler->process_request(handler->streams[frame->hd.stream_id].get());
                }
            }
            break;
        case NGHTTP2_DATA:
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                auto it = handler->streams.find(frame->hd.stream_id);
                if (it != handler->streams.end()) {
                    it->second->request_complete = true;
                    handler->process_request(it->second.get());
                }
            }
            // Send window update to maintain flow control
            if (frame->data.hd.length > 0) {
                handler->send_window_update(frame->hd.stream_id, frame->data.hd.length);
                handler->send_window_update(0, frame->data.hd.length); // Connection window
            }
            break;
        case NGHTTP2_SETTINGS:
            if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
                std::cout << "Received SETTINGS ACK" << std::endl;
            }
            break;
        case NGHTTP2_WINDOW_UPDATE:
            std::cout << "Received WINDOW_UPDATE for stream " << frame->hd.stream_id 
                     << " increment: " << frame->window_update.window_size_increment << std::endl;
            break;
        case NGHTTP2_GOAWAY:
            std::cout << "Received GOAWAY frame" << std::endl;
            return NGHTTP2_ERR_CALLBACK_FAILURE; // Signal to close connection
        case NGHTTP2_PRIORITY:
            handler->handle_priority_frame(frame);
            break;
        default:
            // Handle other frame types gracefully
            break;
    }
    
    return 0;
}

int HTTP2Handler::on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                          uint32_t error_code, void *user_data) {
    (void)session;
    (void)error_code;
    
    HTTP2Handler* handler = static_cast<HTTP2Handler*>(user_data);
    handler->streams.erase(stream_id);
    return 0;
}

int HTTP2Handler::on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                                     const uint8_t *name, size_t namelen,
                                     const uint8_t *value, size_t valuelen,
                                     uint8_t flags, void *user_data) {
    (void)session;
    (void)flags;
    
    HTTP2Handler* handler = static_cast<HTTP2Handler*>(user_data);
    
    if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
        auto it = handler->streams.find(frame->hd.stream_id);
        if (it != handler->streams.end()) {
            std::string header_name(reinterpret_cast<const char*>(name), namelen);
            std::string header_value(reinterpret_cast<const char*>(value), valuelen);
            
            if (header_name == ":method") {
                it->second->method = header_value;
            } else if (header_name == ":path") {
                it->second->path = header_value;
            } else {
                it->second->headers[header_name] = header_value;
            }
        }
    }
    
    return 0;
}

int HTTP2Handler::on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                             int32_t stream_id, const uint8_t *data,
                                             size_t len, void *user_data) {
    (void)session;
    (void)flags;
    
    HTTP2Handler* handler = static_cast<HTTP2Handler*>(user_data);
    
    auto it = handler->streams.find(stream_id);
    if (it != handler->streams.end()) {
        it->second->body.append(reinterpret_cast<const char*>(data), len);
    }
    
    return 0;
}

int HTTP2Handler::on_frame_send_callback(nghttp2_session *session,
                                        const nghttp2_frame *frame, void *user_data) {
    (void)session;
    (void)user_data;
    
    switch (frame->hd.type) {
        case NGHTTP2_HEADERS:
            std::cout << "Sent HEADERS frame for stream " << frame->hd.stream_id << std::endl;
            break;
        case NGHTTP2_DATA:
            std::cout << "Sent DATA frame for stream " << frame->hd.stream_id 
                     << " length: " << frame->data.hd.length << std::endl;
            break;
        case NGHTTP2_SETTINGS:
            if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
                std::cout << "Sent SETTINGS ACK" << std::endl;
            } else {
                std::cout << "Sent SETTINGS frame" << std::endl;
            }
            break;
        default:
            break;
    }
    
    return 0;
}

int HTTP2Handler::on_error_callback(nghttp2_session *session, const char *msg,
                                   size_t len, void *user_data) {
    (void)session;
    (void)user_data;
    
    std::string error_msg(msg, len);
    std::cerr << "HTTP/2 error: " << error_msg << std::endl;
    
    return 0;
}

ssize_t HTTP2Handler::data_source_read_callback(nghttp2_session *session, int32_t stream_id,
                                               uint8_t *buf, size_t length, uint32_t *data_flags,
                                               nghttp2_data_source *source, void *user_data) {
    (void)session;
    (void)user_data;
    
    HTTP2Stream* stream = static_cast<HTTP2Stream*>(source->ptr);
    
    size_t remaining = stream->response_body.length() - stream->response_data_sent;
    size_t copy_len = std::min(length, remaining);
    
    if (copy_len > 0) {
        memcpy(buf, stream->response_body.c_str() + stream->response_data_sent, copy_len);
        stream->response_data_sent += copy_len;
    }
    
    if (stream->response_data_sent >= stream->response_body.length()) {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
    }
    
    return copy_len;
}

void HTTP2Handler::send_window_update(int32_t stream_id, uint32_t window_size_increment) {
    int rv = nghttp2_submit_window_update(session, NGHTTP2_FLAG_NONE, stream_id, window_size_increment);
    if (rv != 0) {
        std::cerr << "Failed to submit window update: " << nghttp2_strerror(rv) << std::endl;
    }
}

void HTTP2Handler::process_request(HTTP2Stream* stream) {
    if (!stream->request_complete) {
        return;
    }
    
    std::cout << "Processing HTTP/2 " << stream->method << " request for " << stream->path << std::endl;
    
    // Handle different HTTP methods
    if (stream->method == "GET") {
        std::string file_path = document_root + stream->path;
        if (stream->path == "/") {
            file_path = document_root + "/index.html";
        }
        
        try {
            if (file_handler->file_exists(file_path)) {
                stream->response_body = file_handler->read_file(file_path);
                stream->status_code = 200;
                stream->response_headers["content-type"] = file_handler->get_mime_type(file_path);
                
                // Server push logic for HTML pages
                if (stream->push_enabled && server_push_enabled() && 
                    (stream->response_headers["content-type"] == "text/html")) {
                    std::vector<std::string> push_resources;
                    identify_push_resources(stream->path, push_resources);
                    
                    for (const auto& resource : push_resources) {
                        push_resource(stream->stream_id, resource, "GET");
                    }
                }
            } else {
                stream->status_code = 404;
                stream->response_body = "<!DOCTYPE html><html><body><h1>404 Not Found</h1></body></html>";
                stream->response_headers["content-type"] = "text/html";
            }
        } catch (const std::exception& e) {
            stream->status_code = 500;
            stream->response_body = "<!DOCTYPE html><html><body><h1>500 Internal Server Error</h1></body></html>";
            stream->response_headers["content-type"] = "text/html";
        }
    } else if (stream->method == "POST") {
        // Handle POST requests - for now, just echo back
        stream->status_code = 200;
        stream->response_body = "POST request received. Body: " + stream->body;
        stream->response_headers["content-type"] = "text/plain";
    } else {
        stream->status_code = 405;
        stream->response_body = "Method Not Allowed";
        stream->response_headers["content-type"] = "text/plain";
    }
    
    send_response(stream);
}

void HTTP2Handler::send_response(HTTP2Stream* stream) {
    // Prepare response headers with proper memory management
    std::vector<nghttp2_nv> response_headers;
    std::vector<std::string> header_storage; // Keep strings alive
    
    if (!create_response_headers(stream, response_headers, header_storage)) {
        std::cerr << "Failed to create response headers" << std::endl;
        return;
    }
    
    // Submit response headers with data
    nghttp2_data_provider data_prd;
    data_prd.source.ptr = stream;
    data_prd.read_callback = data_source_read_callback;
    
    int rv = nghttp2_submit_response(session, stream->stream_id,
                                    response_headers.data(), response_headers.size(),
                                    &data_prd);
    if (rv != 0) {
        std::cerr << "Failed to submit response: " << nghttp2_strerror(rv) << std::endl;
    }
}

bool HTTP2Handler::create_response_headers(HTTP2Stream* stream, std::vector<nghttp2_nv>& headers,
                                          std::vector<std::string>& header_storage) {
    // Prepare header strings that will stay alive
    header_storage.clear();
    headers.clear();
    
    // Status header
    header_storage.push_back(":status");
    header_storage.push_back(std::to_string(stream->status_code));
    
    nghttp2_nv status_header;
    status_header.name = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-2].c_str()));
    status_header.value = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-1].c_str()));
    status_header.namelen = header_storage[header_storage.size()-2].length();
    status_header.valuelen = header_storage[header_storage.size()-1].length();
    status_header.flags = NGHTTP2_NV_FLAG_NONE;
    headers.push_back(status_header);
    
    // Content-Length header
    header_storage.push_back("content-length");
    header_storage.push_back(std::to_string(stream->response_body.length()));
    
    nghttp2_nv length_header;
    length_header.name = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-2].c_str()));
    length_header.value = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-1].c_str()));
    length_header.namelen = header_storage[header_storage.size()-2].length();
    length_header.valuelen = header_storage[header_storage.size()-1].length();
    length_header.flags = NGHTTP2_NV_FLAG_NONE;
    headers.push_back(length_header);
    
    // Add other response headers
    for (const auto& header : stream->response_headers) {
        header_storage.push_back(header.first);
        header_storage.push_back(header.second);
        
        nghttp2_nv nv;
        nv.name = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-2].c_str()));
        nv.value = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-1].c_str()));
        nv.namelen = header_storage[header_storage.size()-2].length();
        nv.valuelen = header_storage[header_storage.size()-1].length();
        nv.flags = NGHTTP2_NV_FLAG_NONE;
        headers.push_back(nv);
    }
    
    return true;
}

std::string HTTP2Handler::get_content_type(const std::string& path) {
    size_t dot_pos = path.rfind('.');
    if (dot_pos != std::string::npos) {
        std::string ext = path.substr(dot_pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == "html" || ext == "htm") return "text/html";
        if (ext == "css") return "text/css";
        if (ext == "js") return "application/javascript";
        if (ext == "json") return "application/json";
        if (ext == "png") return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "gif") return "image/gif";
        if (ext == "svg") return "image/svg+xml";
        if (ext == "txt") return "text/plain";
    }
    return "application/octet-stream";
}

// Server Push Implementation
bool HTTP2Handler::server_push_enabled() const {
    return true; // Default enabled, can be controlled by settings
}

void HTTP2Handler::enable_server_push(bool enable) {
    nghttp2_settings_entry setting = {NGHTTP2_SETTINGS_ENABLE_PUSH, enable ? 1u : 0u};
    int rv = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, &setting, 1);
    if (rv != 0) {
        std::cerr << "Failed to submit push setting: " << nghttp2_strerror(rv) << std::endl;
    }
}

bool HTTP2Handler::push_resource(int32_t parent_stream_id, const std::string& path, const std::string& method) {
    if (!server_push_enabled()) {
        return false;
    }
    
    // Create push promise headers
    std::vector<nghttp2_nv> push_headers;
    std::vector<std::string> header_storage;
    
    // :method
    header_storage.push_back(":method");
    header_storage.push_back(method);
    nghttp2_nv method_header;
    method_header.name = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-2].c_str()));
    method_header.value = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-1].c_str()));
    method_header.namelen = header_storage[header_storage.size()-2].length();
    method_header.valuelen = header_storage[header_storage.size()-1].length();
    method_header.flags = NGHTTP2_NV_FLAG_NONE;
    push_headers.push_back(method_header);
    
    // :path
    header_storage.push_back(":path");
    header_storage.push_back(path);
    nghttp2_nv path_header;
    path_header.name = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-2].c_str()));
    path_header.value = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-1].c_str()));
    path_header.namelen = header_storage[header_storage.size()-2].length();
    path_header.valuelen = header_storage[header_storage.size()-1].length();
    path_header.flags = NGHTTP2_NV_FLAG_NONE;
    push_headers.push_back(path_header);
    
    // :scheme
    header_storage.push_back(":scheme");
    header_storage.push_back("http");
    nghttp2_nv scheme_header;
    scheme_header.name = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-2].c_str()));
    scheme_header.value = reinterpret_cast<uint8_t*>(const_cast<char*>(header_storage[header_storage.size()-1].c_str()));
    scheme_header.namelen = header_storage[header_storage.size()-2].length();
    scheme_header.valuelen = header_storage[header_storage.size()-1].length();
    scheme_header.flags = NGHTTP2_NV_FLAG_NONE;
    push_headers.push_back(scheme_header);
    
    // Submit push promise
    int32_t promised_stream_id = nghttp2_submit_push_promise(session, NGHTTP2_FLAG_NONE, parent_stream_id,
                                                            push_headers.data(), push_headers.size(), nullptr);
    
    if (promised_stream_id < 0) {
        std::cerr << "Failed to submit push promise: " << nghttp2_strerror(promised_stream_id) << std::endl;
        return false;
    }
    
    std::cout << "Push promise submitted for " << path << " on stream " << promised_stream_id << std::endl;
    
    // Create stream for the pushed resource
    auto pushed_stream = std::make_unique<HTTP2Stream>(promised_stream_id);
    pushed_stream->method = method;
    pushed_stream->path = path;
    pushed_stream->headers_complete = true;
    pushed_stream->request_complete = true;
    streams[promised_stream_id] = std::move(pushed_stream);
    
    // Process the pushed resource
    process_request(streams[promised_stream_id].get());
    
    return true;
}

void HTTP2Handler::identify_push_resources(const std::string& path, std::vector<std::string>& resources) {
    // Basic push resource identification for HTML pages
    if (path == "/" || path == "/index.html") {
        resources.push_back("/style.css");
        resources.push_back("/demo.html");
    } else if (path == "/dashboard.html") {
        resources.push_back("/style.css");
        resources.push_back("/data.json");
    } else if (path == "/demo.html") {
        resources.push_back("/style.css");
    }
    // Add more sophisticated resource identification as needed
}

bool HTTP2Handler::send_push_promise(int32_t parent_stream_id, const std::string& path) {
    return push_resource(parent_stream_id, path, "GET");
}

// Priority Handling Implementation
void HTTP2Handler::set_stream_priority(int32_t stream_id, int32_t dependency, int weight, bool exclusive) {
    StreamPriority priority(stream_id, dependency, weight, exclusive);
    stream_priorities[stream_id] = priority;
    
    nghttp2_priority_spec priority_spec;
    nghttp2_priority_spec_init(&priority_spec, dependency, weight, exclusive ? 1 : 0);
    
    int rv = nghttp2_submit_priority(session, NGHTTP2_FLAG_NONE, stream_id, &priority_spec);
    if (rv != 0) {
        std::cerr << "Failed to submit priority: " << nghttp2_strerror(rv) << std::endl;
    }
}

void HTTP2Handler::update_stream_priority(int32_t stream_id, int32_t dependency, int weight, bool exclusive) {
    auto it = stream_priorities.find(stream_id);
    if (it != stream_priorities.end()) {
        it->second.dependency = dependency;
        it->second.weight = weight;
        it->second.exclusive = exclusive;
    } else {
        set_stream_priority(stream_id, dependency, weight, exclusive);
    }
}

HTTP2Handler::StreamPriority HTTP2Handler::get_stream_priority(int32_t stream_id) const {
    auto it = stream_priorities.find(stream_id);
    if (it != stream_priorities.end()) {
        return it->second;
    }
    return StreamPriority(); // Default priority
}

void HTTP2Handler::handle_priority_frame(const nghttp2_frame* frame) {
    const nghttp2_priority_spec& priority_spec = frame->priority.pri_spec;
    
    StreamPriority priority(frame->hd.stream_id, 
                          priority_spec.stream_id,
                          priority_spec.weight,
                          priority_spec.exclusive != 0);
    
    stream_priorities[frame->hd.stream_id] = priority;
    
    std::cout << "Updated priority for stream " << frame->hd.stream_id 
              << " dependency: " << priority_spec.stream_id
              << " weight: " << priority_spec.weight
              << " exclusive: " << (priority_spec.exclusive ? "true" : "false") << std::endl;
}
