#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

class DebugTester {
private:
    std::string host;
    int port;
    
public:
    DebugTester(const std::string& h = "127.0.0.1", int p = 8080) : host(h), port(p) {}
    
    void debug_mime_types() {
        std::cout << "=== DEBUGGING MIME TYPES ===" << std::endl;
        
        // Test root path
        std::cout << "\n1. Testing root path '/':" << std::endl;
        std::string response = send_http_request("GET / HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
        print_response_headers(response);
        
        // Test index.html directly
        std::cout << "\n2. Testing '/index.html':" << std::endl;
        response = send_http_request("GET /index.html HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
        print_response_headers(response);
    }
    
    void debug_malformed_requests() {
        std::cout << "\n=== DEBUGGING MALFORMED REQUESTS ===" << std::endl;
        
        std::vector<std::string> malformed_requests = {
            "INVALID REQUEST\r\n\r\n",
            "GET\r\n\r\n", 
            "GET /\r\n\r\n",
            "GET / HTTP/1.1\r\nInvalid-Header\r\n\r\n"
        };
        
        for (size_t i = 0; i < malformed_requests.size(); ++i) {
            std::cout << "\n" << (i+1) << ". Testing malformed request:" << std::endl;
            std::cout << "Request: " << malformed_requests[i] << std::endl;
            
            std::string response = send_http_request(malformed_requests[i]);
            
            if (response.empty()) {
                std::cout << "Response: [EMPTY - Connection closed]" << std::endl;
            } else {
                std::cout << "Response: " << std::endl;
                print_response_headers(response);
            }
        }
    }
    
private:
    void print_response_headers(const std::string& response) {
        if (response.empty()) {
            std::cout << "[EMPTY RESPONSE]" << std::endl;
            return;
        }
        
        // Find end of headers
        size_t header_end = response.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            std::cout << "Headers: [MALFORMED - no header end found]" << std::endl;
            std::cout << "Raw response: " << response.substr(0, 200) << "..." << std::endl;
            return;
        }
        
        std::string headers = response.substr(0, header_end);
        std::cout << "Headers:" << std::endl;
        std::cout << headers << std::endl;
        
        // Extract Content-Type specifically
        size_t ct_pos = response.find("Content-Type: ");
        if (ct_pos != std::string::npos) {
            size_t ct_end = response.find("\r\n", ct_pos);
            if (ct_end != std::string::npos) {
                std::string content_type = response.substr(ct_pos, ct_end - ct_pos);
                std::cout << "*** " << content_type << " ***" << std::endl;
            }
        } else {
            std::cout << "*** NO CONTENT-TYPE HEADER FOUND ***" << std::endl;
        }
    }
    
    int create_socket() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return -1;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
        
        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if (connect(sock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
            close(sock);
            return -1;
        }
        
        return sock;
    }
    
    std::string read_response(int sock) {
        char buffer[4096];
        std::string response;
        
        while (true) {
            ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) break;
            
            buffer[bytes] = '\0';
            response += buffer;
            
            // Stop reading if we have complete headers and body
            if (response.find("\r\n\r\n") != std::string::npos) {
                // For simple responses, we can stop here
                break;
            }
        }
        
        return response;
    }
    
    std::string send_http_request(const std::string& request) {
        int sock = create_socket();
        if (sock < 0) return "";
        
        if (send(sock, request.c_str(), request.length(), 0) < 0) {
            close(sock);
            return "";
        }
        
        std::string response = read_response(sock);
        close(sock);
        return response;
    }
};

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 8080;
    
    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) host = argv[2];
    
    DebugTester tester(host, port);
    tester.debug_mime_types();
    tester.debug_malformed_requests();
    
    return 0;
}