#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include <string>
#include <map>

class FileHandler {
private:
    std::string document_root;
    std::map<std::string, std::string> mime_types;

public:
    FileHandler(const std::string& doc_root);
    
    // Check if a file exists and is readable
    bool file_exists(const std::string& path) const;
    
    // Read file contents
    std::string read_file(const std::string& path) const;
    
    // Get MIME type based on file extension
    std::string get_mime_type(const std::string& path) const;
    
    // Get file size
    size_t get_file_size(const std::string& path) const;
    
    // Resolve path (handle directory index, relative paths, etc.)
    std::string resolve_path(const std::string& requested_path) const;

private:
    void initialize_mime_types();
    std::string get_file_extension(const std::string& path) const;
    std::string to_lower(const std::string& str) const;
};

#endif // FILE_HANDLER_H