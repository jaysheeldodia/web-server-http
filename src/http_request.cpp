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
            // Parse the request line (GET /path?query HTTP/1.1)
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
                return false;
            }
        }
    }

    // Read body if there's content after headers
    if (headers_done) {
        std::string remaining_data;
        std::string body_line;
        while (std::getline(stream, body_line)) {
            remaining_data += body_line + "\n";
        }
        
        // Remove the last newline if it exists
        if (!remaining_data.empty() && remaining_data.back() == '\n') {
            remaining_data.pop_back();
        }
        
        body = remaining_data;
        
        // For POST/PUT requests, ensure we have the complete body
        size_t expected_length = get_content_length();
        if (expected_length > 0 && body.length() < expected_length) {
            // Body might be incomplete, but we'll work with what we have
            // In a production server, you'd read more data from the socket
        }
    }

    valid = !method.empty() && !path.empty() && !version.empty();
    return valid;
}

bool HttpRequest::parse_request_line(const std::string& line) {
    std::istringstream iss(line);
    std::string path_with_query;
    
    // Parse: METHOD PATH_WITH_QUERY VERSION
    if (!(iss >> method >> path_with_query >> version)) {
        return false;
    }

    // Convert method to uppercase
    std::transform(method.begin(), method.end(), method.begin(), ::toupper);

    // Parse query parameters from path
    parse_query_parameters(path_with_query);

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

void HttpRequest::parse_query_parameters(const std::string& path_with_query) {
    size_t query_pos = path_with_query.find('?');
    
    if (query_pos == std::string::npos) {
        // No query parameters
        path = path_with_query;
        return;
    }
    
    // Split path and query
    path = path_with_query.substr(0, query_pos);
    std::string query_string = path_with_query.substr(query_pos + 1);
    
    // Parse query parameters (key1=value1&key2=value2)
    std::istringstream query_stream(query_string);
    std::string pair;
    
    while (std::getline(query_stream, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq_pos));
            std::string value = url_decode(pair.substr(eq_pos + 1));
            query_params[key] = value;
        }
    }
}

std::string HttpRequest::url_decode(const std::string& str) const {
    std::string decoded;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            // URL decode %XX
            std::string hex = str.substr(i + 1, 2);
            char decoded_char = static_cast<char>(std::stoi(hex, nullptr, 16));
            decoded += decoded_char;
            i += 2;
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }
    return decoded;
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

std::string HttpRequest::get_query_param(const std::string& param_name) const {
    auto it = query_params.find(param_name);
    if (it != query_params.end()) {
        return it->second;
    }
    return "";
}

bool HttpRequest::has_json_content_type() const {
    std::string content_type = get_header("content-type");
    return content_type.find("application/json") != std::string::npos;
}

size_t HttpRequest::get_content_length() const {
    std::string length_header = get_header("content-length");
    if (length_header.empty()) {
        return 0;
    }
    try {
        return std::stoul(length_header);
    } catch (...) {
        return 0;
    }
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
    
    if (!query_params.empty()) {
        std::cout << "Query Parameters:" << std::endl;
        for (const auto& param : query_params) {
            std::cout << "  " << param.first << " = " << param.second << std::endl;
        }
    }
    
    if (!body.empty()) {
        std::cout << "Body (" << body.length() << " bytes): " << body << std::endl;
    }
    std::cout << "=========================" << std::endl;
}