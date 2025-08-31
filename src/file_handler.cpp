#include "../include/file_handler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>

FileHandler::FileHandler(const std::string& doc_root) : document_root(doc_root) {
    initialize_mime_types();
}

void FileHandler::initialize_mime_types() {
    // Common MIME types
    mime_types[".html"] = "text/html";
    mime_types[".htm"] = "text/html";
    mime_types[".css"] = "text/css";
    mime_types[".js"] = "application/javascript";
    mime_types[".json"] = "application/json";
    mime_types[".txt"] = "text/plain";
    mime_types[".xml"] = "application/xml";
    
    // Images
    mime_types[".png"] = "image/png";
    mime_types[".jpg"] = "image/jpeg";
    mime_types[".jpeg"] = "image/jpeg";
    mime_types[".gif"] = "image/gif";
    mime_types[".svg"] = "image/svg+xml";
    mime_types[".ico"] = "image/x-icon";
    
    // Other
    mime_types[".pdf"] = "application/pdf";
    mime_types[".zip"] = "application/zip";
}

std::string FileHandler::resolve_path(const std::string& requested_path) const {
    std::string path = requested_path;
    
    // If path ends with '/', append index.html
    if (path == "/" || path.back() == '/') {
        if (path == "/") {
            path = "/index.html";
        } else {
            path += "index.html";
        }
    }
    
    // Combine with document root
    std::string full_path = document_root + path;
    
    return full_path;
}

bool FileHandler::file_exists(const std::string& path) const {
    std::string full_path = resolve_path(path);
    
    struct stat file_stat;
    if (stat(full_path.c_str(), &file_stat) == 0) {
        // Check if it's a regular file
        return S_ISREG(file_stat.st_mode);
    }
    
    return false;
}

std::string FileHandler::read_file(const std::string& path) const {
    std::string full_path = resolve_path(path);
    
    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
    // Read entire file into string
    std::ostringstream content_stream;
    content_stream << file.rdbuf();
    file.close();
    
    return content_stream.str();
}

size_t FileHandler::get_file_size(const std::string& path) const {
    std::string full_path = resolve_path(path);
    
    struct stat file_stat;
    if (stat(full_path.c_str(), &file_stat) == 0) {
        return file_stat.st_size;
    }
    
    return 0;
}

std::string FileHandler::get_mime_type(const std::string& path) const {
    std::string extension = get_file_extension(path);
    extension = to_lower(extension);
    
    auto it = mime_types.find(extension);
    if (it != mime_types.end()) {
        return it->second;
    }
    
    // Default MIME type
    return "application/octet-stream";
}

std::string FileHandler::get_file_extension(const std::string& path) const {
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return path.substr(dot_pos);
    }
    return "";
}

std::string FileHandler::to_lower(const std::string& str) const {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}