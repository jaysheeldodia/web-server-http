#include "../include/http_request.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

HttpRequest::HttpRequest() : valid(false) {}

bool HttpRequest::parse(const std::string& raw_request) {
    if (raw_request.empty()) {
        return false;
    }

    // Split request into lines
    std::istringstream stream(raw_request);
    std::string line;
    bool first_line = true;
    bool headers_done = false;

    while (std::getline(stream, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (first_line) {
            // Parse the request line (GET /path HTTP/1.1)
            if (!parse_request_line(line)) {
                return false;
            }
            first_line = false;
        } else if (line.empty()) {
            // Empty line marks end of headers
            headers_done = true;
            break;
        } else if (!headers_done) {
            // Parse header line - be more strict about malformed headers
            if (!parse_header_line(line)) {
                return false; // Reject malformed headers - THIS IS THE KEY CHANGE
            }
        }
    }

    // Read body if there's content after headers
    if (headers_done) {
        std::string body_line;
        while (std::getline(stream, body_line)) {
            body += body_line + "\n";
        }
    }

    valid = !method.empty() && !path.empty() && !version.empty();
    return valid;
}

bool HttpRequest::parse_request_line(const std::string& line) {
    std::istringstream iss(line);
    
    // Parse: METHOD PATH VERSION
    if (!(iss >> method >> path >> version)) {
        return false;
    }

    // Convert method to uppercase
    std::transform(method.begin(), method.end(), method.begin(), ::toupper);

    // Basic validation
    if (method.empty() || path.empty() || version.empty()) {
        return false;
    }

    // Path should start with /
    if (path[0] != '/') {
        return false;
    }

    return true;
}

bool HttpRequest::parse_header_line(const std::string& line) {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        // Header line without colon is malformed - REJECT IT
        return false;
    }
    
    if (colon_pos == 0) {
        // Header name cannot be empty
        return false;
    }
    
    std::string key = trim(line.substr(0, colon_pos));
    std::string value = trim(line.substr(colon_pos + 1));
    
    // Header name cannot be empty after trimming
    if (key.empty()) {
        return false;
    }
    
    // Convert header name to lowercase for case-insensitive lookup
    key = to_lower(key);
    headers[key] = value;
    
    return true;
}

std::string HttpRequest::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

std::string HttpRequest::to_lower(const std::string& str) const {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool HttpRequest::is_valid() const {
    return valid;
}

std::string HttpRequest::get_header(const std::string& header_name) const {
    std::string lower_name = to_lower(header_name);
    auto it = headers.find(lower_name);
    if (it != headers.end()) {
        return it->second;
    }
    return "";
}

void HttpRequest::print_debug() const {
    std::cout << "=== HTTP Request Debug ===" << std::endl;
    std::cout << "Method: " << method << std::endl;
    std::cout << "Path: " << path << std::endl;
    std::cout << "Version: " << version << std::endl;
    std::cout << "Headers:" << std::endl;
    
    for (const auto& header : headers) {
        std::cout << "  " << header.first << ": " << header.second << std::endl;
    }
    
    if (!body.empty()) {
        std::cout << "Body: " << body << std::endl;
    }
    std::cout << "=========================" << std::endl;
}