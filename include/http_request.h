#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <map>

class HttpRequest {
public:
    std::string method;        // GET, POST, etc.
    std::string path;          // /index.html, /, etc.
    std::string version;       // HTTP/1.1
    std::map<std::string, std::string> headers;  // Key-value pairs
    std::string body;          // Request body (for POST requests)

    // Constructor
    HttpRequest();
    
    // Parse a raw HTTP request string
    bool parse(const std::string& raw_request);
    
    // Helper methods
    bool is_valid() const;
    std::string get_header(const std::string& header_name) const;
    void print_debug() const;  // For debugging

private:
    bool valid;
    
    // Helper parsing methods
    bool parse_request_line(const std::string& line);
    bool parse_header_line(const std::string& line);  // NOW RETURNS BOOL FOR VALIDATION
    std::string trim(const std::string& str) const;
    std::string to_lower(const std::string& str) const;
};

#endif // HTTP_REQUEST_H