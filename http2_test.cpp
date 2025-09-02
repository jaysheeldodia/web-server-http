#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const char HTTP2_CONNECTION_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

// Simple HTTP/2 SETTINGS frame
const unsigned char HTTP2_SETTINGS_FRAME[] = {
    0x00, 0x00, 0x0C,  // Length: 12 bytes
    0x04,              // Type: SETTINGS
    0x00,              // Flags: none
    0x00, 0x00, 0x00, 0x00,  // Stream ID: 0 (connection)
    // Settings payload:
    0x00, 0x03, 0x00, 0x00, 0x00, 0x64,  // MAX_CONCURRENT_STREAMS = 100
    0x00, 0x04, 0x00, 0x01, 0x00, 0x00   // INITIAL_WINDOW_SIZE = 65536
};

// Simple HTTP/2 HEADERS frame for GET /
const unsigned char HTTP2_HEADERS_FRAME[] = {
    0x00, 0x00, 0x29,  // Length: 41 bytes
    0x01,              // Type: HEADERS
    0x05,              // Flags: END_HEADERS | END_STREAM
    0x00, 0x00, 0x00, 0x01,  // Stream ID: 1
    // HPACK encoded headers for GET / HTTP/2
    0x82,              // :method: GET (indexed)
    0x84,              // :path: / (indexed)
    0x86,              // :scheme: http (indexed)
    0x41, 0x0a, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68, 0x6f, 0x73, 0x74, // :authority: localhost
    0x40, 0x0a, 0x75, 0x73, 0x65, 0x72, 0x2d, 0x61, 0x67, 0x65, 0x6e, 0x74, // user-agent
    0x0d, 0x48, 0x54, 0x54, 0x50, 0x32, 0x2d, 0x54, 0x65, 0x73, 0x74, 0x65, 0x72 // HTTP2-Tester
};

int main() {
    std::cout << "HTTP/2 Connection Test\n";
    std::cout << "======================\n";
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    
    // Connect to server
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to server\n";
        close(sock);
        return 1;
    }
    
    std::cout << "Connected to server on port 8080\n";
    
    // Send HTTP/2 connection preface
    ssize_t sent = send(sock, HTTP2_CONNECTION_PREFACE, 24, 0);
    if (sent != 24) {
        std::cerr << "Failed to send HTTP/2 preface\n";
        close(sock);
        return 1;
    }
    
    std::cout << "Sent HTTP/2 connection preface\n";
    
    // Send SETTINGS frame
    sent = send(sock, HTTP2_SETTINGS_FRAME, sizeof(HTTP2_SETTINGS_FRAME), 0);
    if (sent != sizeof(HTTP2_SETTINGS_FRAME)) {
        std::cerr << "Failed to send SETTINGS frame\n";
        close(sock);
        return 1;
    }
    
    std::cout << "Sent SETTINGS frame\n";
    
    // Wait for server response (SETTINGS frame)
    char buffer[1024];
    ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
    if (received > 0) {
        std::cout << "Received " << received << " bytes from server\n";
        
        // Print hex dump of response
        std::cout << "Response hex dump:\n";
        for (int i = 0; i < received; i++) {
            printf("%02x ", (unsigned char)buffer[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        if (received % 16 != 0) printf("\n");
    }
    
    // Send HEADERS frame for GET /
    sent = send(sock, HTTP2_HEADERS_FRAME, sizeof(HTTP2_HEADERS_FRAME), 0);
    if (sent != sizeof(HTTP2_HEADERS_FRAME)) {
        std::cerr << "Failed to send HEADERS frame\n";
        close(sock);
        return 1;
    }
    
    std::cout << "Sent HEADERS frame for GET /\n";
    
    // Wait for response
    received = recv(sock, buffer, sizeof(buffer), 0);
    if (received > 0) {
        std::cout << "Received response: " << received << " bytes\n";
        
        // Print hex dump of response
        std::cout << "Response hex dump:\n";
        for (int i = 0; i < received; i++) {
            printf("%02x ", (unsigned char)buffer[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        if (received % 16 != 0) printf("\n");
    }
    
    close(sock);
    std::cout << "Test completed\n";
    
    return 0;
}
