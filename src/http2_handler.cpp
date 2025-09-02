#include "../include/http2_handler.h"
#include "../include/file_handler.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <algorithm>

const char HTTP2_CONNECTION_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

HTTP2Handler::HTTP2Handler(int socket_fd, std::shared_ptr<FileHandler> file_handler,
                           std::shared_ptr<PerformanceMetrics> metrics,
                           const std::string& doc_root)
    : session(nullptr), socket_fd(socket_fd), file_handler(file_handler),
      performance_metrics(metrics), document_root(doc_root) {
}

HTTP2Handler::~HTTP2Handler() {
    if (session) {
        nghttp2_session_del(session);
    }
}

bool HTTP2Handler::initialize() {
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    
    setup_callbacks();
    
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    
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
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535},
        {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384}
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
    // Check if this is the HTTP/2 connection preface
    if (len >= 24 && memcmp(data, HTTP2_CONNECTION_PREFACE, 24) == 0) {
        // Skip the preface and process any remaining data
        data += 24;
        len -= 24;
        
        if (len == 0) {
            return 24; // Just the preface, nothing more to process
        }
    }
    
    ssize_t readlen = nghttp2_session_mem_recv(session, data, len);
    if (readlen < 0) {
        std::cerr << "Failed to process HTTP/2 data: " << nghttp2_strerror(readlen) << std::endl;
        return -1;
    }
    
    if (!flush_output()) {
        return -1;
    }
    
    return readlen;
}

bool HTTP2Handler::flush_output() {
    int rv = nghttp2_session_send(session);
    if (rv != 0) {
        std::cerr << "Failed to send HTTP/2 data: " << nghttp2_strerror(rv) << std::endl;
        return false;
    }
    
    // Send buffered data to socket
    if (!output_buffer.empty()) {
        ssize_t sent = send(socket_fd, output_buffer.data(), output_buffer.size(), 0);
        if (sent < 0) {
            std::cerr << "Failed to send data to socket" << std::endl;
            return false;
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
                    handler->process_request(stream.get());
                }
                handler->streams[frame->hd.stream_id] = std::move(stream);
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
    // Prepare response headers
    std::vector<nghttp2_nv> response_headers;
    
    // Status header
    std::string status_str = std::to_string(stream->status_code);
    nghttp2_nv status_header = {
        reinterpret_cast<uint8_t*>(const_cast<char*>(":status")),
        reinterpret_cast<uint8_t*>(const_cast<char*>(status_str.c_str())),
        7, status_str.length(),
        NGHTTP2_NV_FLAG_NONE
    };
    response_headers.push_back(status_header);
    
    // Content-Length header
    std::string content_length = std::to_string(stream->response_body.length());
    nghttp2_nv length_header = {
        reinterpret_cast<uint8_t*>(const_cast<char*>("content-length")),
        reinterpret_cast<uint8_t*>(const_cast<char*>(content_length.c_str())),
        14, content_length.length(),
        NGHTTP2_NV_FLAG_NONE
    };
    response_headers.push_back(length_header);
    
    // Add other response headers
    for (const auto& header : stream->response_headers) {
        nghttp2_nv nv = {
            reinterpret_cast<uint8_t*>(const_cast<char*>(header.first.c_str())),
            reinterpret_cast<uint8_t*>(const_cast<char*>(header.second.c_str())),
            header.first.length(), header.second.length(),
            NGHTTP2_NV_FLAG_NONE
        };
        response_headers.push_back(nv);
    }
    
    // Submit response headers
    nghttp2_data_provider data_prd;
    data_prd.source.ptr = stream;
    data_prd.read_callback = [](nghttp2_session *session, int32_t stream_id,
                               uint8_t *buf, size_t length, uint32_t *data_flags,
                               nghttp2_data_source *source, void *user_data) -> ssize_t {
        (void)session;
        (void)stream_id;
        (void)user_data;
        
        HTTP2Stream* stream = static_cast<HTTP2Stream*>(source->ptr);
        size_t copy_len = std::min(length, stream->response_body.length());
        memcpy(buf, stream->response_body.c_str(), copy_len);
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
        return copy_len;
    };
    
    int rv = nghttp2_submit_response(session, stream->stream_id,
                                    response_headers.data(), response_headers.size(),
                                    &data_prd);
    if (rv != 0) {
        std::cerr << "Failed to submit response: " << nghttp2_strerror(rv) << std::endl;
    }
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
