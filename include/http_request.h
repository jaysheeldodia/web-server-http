#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <map>

class HttpRequest {
public:
    std::string method;        // GET, POST, PUT, DELETE, etc.
    std::string path;          // /index.html, /, /api/users, etc.
    std::string version;       // HTTP/1.1
    std::map<std::string, std::string> headers;  // Key-value pairs
    std::string body;          // Request body (for POST/PUT requests)
    std::map<std::string, std::string> query_params;  // URL query parameters

    // Constructor
    HttpRequest();
    
    // Parse a raw HTTP request string
    bool parse(const std::string& raw_request);
    
    // Helper methods
    bool is_valid() const;
    std::string get_header(const std::string& header_name) const;
    void print_debug() const;  // For debugging
    
    // New methods for API support
    bool has_json_content_type() const;
    size_t get_content_length() const;
    std::string get_query_param(const std::string& param_name) const;

private:
    bool valid;
    
    // Helper parsing methods
    bool parse_request_line(const std::string& line);
    bool parse_header_line(const std::string& line);
    void parse_query_parameters(const std::string& path_with_query);
    std::string trim(const std::string& str) const;
    std::string to_lower(const std::string& str) const;
    std::string url_decode(const std::string& str) const;
};

#endif // HTTP_REQUEST_H