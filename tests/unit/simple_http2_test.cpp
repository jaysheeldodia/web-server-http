#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const char HTTP2_CONNECTION_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

int main() {
    std::cout << "Simple HTTP/2 Preface Test\n";
    std::cout << "==========================\n";
    
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
    
    std::cout << "Sent HTTP/2 connection preface (" << sent << " bytes)\n";
    
    // Wait for response
    char buffer[1024];
    ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
    if (received > 0) {
        std::cout << "Received " << received << " bytes from server\n";
        
        // Print hex dump of first 64 bytes
        std::cout << "Response hex dump (first 64 bytes):\n";
        for (int i = 0; i < std::min(received, 64L); i++) {
            printf("%02x ", (unsigned char)buffer[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        if (received % 16 != 0) printf("\n");
        
        // Check if it's HTTP/1.1 or HTTP/2
        if (received >= 8 && memcmp(buffer, "HTTP/1.1", 8) == 0) {
            std::cout << "Server responded with HTTP/1.1 (preface not detected)\n";
        } else {
            std::cout << "Server response format unknown\n";
        }
    } else {
        std::cout << "No response from server\n";
    }
    
    close(sock);
    std::cout << "Test completed\n";
    
    return 0;
}
