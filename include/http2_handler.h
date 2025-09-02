#ifndef HTTP2_HANDLER_H
#define HTTP2_HANDLER_H

#include <nghttp2/nghttp2.h>
#include <memory>
#include <map>
#include <string>
#include <functional>
#include <vector>

class FileHandler;
class PerformanceMetrics;

struct HTTP2Stream {
    int32_t stream_id;
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
    bool headers_complete;
    bool request_complete;
    std::string response_body;
    std::map<std::string, std::string> response_headers;
    int status_code;
    
    HTTP2Stream(int32_t id) : stream_id(id), headers_complete(false), 
                             request_complete(false), status_code(200) {}
};

class HTTP2Handler {
private:
    nghttp2_session* session;
    int socket_fd;
    std::map<int32_t, std::unique_ptr<HTTP2Stream>> streams;
    std::shared_ptr<FileHandler> file_handler;
    std::shared_ptr<PerformanceMetrics> performance_metrics;
    std::string document_root;
    
    // Buffer for outgoing data
    std::vector<uint8_t> output_buffer;
    
    // Callbacks for nghttp2
    static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                                size_t length, int flags, void *user_data);
    static int on_frame_recv_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame, void *user_data);
    static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                       uint32_t error_code, void *user_data);
    static int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                                 const uint8_t *name, size_t namelen,
                                 const uint8_t *value, size_t valuelen,
                                 uint8_t flags, void *user_data);
    static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                          int32_t stream_id, const uint8_t *data,
                                          size_t len, void *user_data);
    
    // Helper methods
    void setup_callbacks();
    void process_request(HTTP2Stream* stream);
    std::string build_response(HTTP2Stream* stream);
    std::string get_content_type(const std::string& path);
    void send_response(HTTP2Stream* stream);
    
public:
    HTTP2Handler(int socket_fd, std::shared_ptr<FileHandler> file_handler,
                 std::shared_ptr<PerformanceMetrics> metrics,
                 const std::string& doc_root);
    ~HTTP2Handler();
    
    // Non-copyable
    HTTP2Handler(const HTTP2Handler&) = delete;
    HTTP2Handler& operator=(const HTTP2Handler&) = delete;
    
    bool initialize();
    int process_data(const uint8_t* data, size_t len);
    bool send_settings();
    bool flush_output();
    bool session_want_read() const;
    bool session_want_write() const;
    
    // Get buffered output data
    const std::vector<uint8_t>& get_output_buffer() const { return output_buffer; }
    void clear_output_buffer() { output_buffer.clear(); }
};

// HTTP/2 Connection Preface
extern const char HTTP2_CONNECTION_PREFACE[];

#endif // HTTP2_HANDLER_H
