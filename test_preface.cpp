#include <iostream>
#include <cstring>

const char HTTP2_CONNECTION_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

int main() {
    std::cout << "HTTP/2 Connection Preface:" << std::endl;
    std::cout << "Length: " << strlen(HTTP2_CONNECTION_PREFACE) << " bytes" << std::endl;
    std::cout << "Content:" << std::endl;
    
    for (size_t i = 0; i < strlen(HTTP2_CONNECTION_PREFACE); i++) {
        char c = HTTP2_CONNECTION_PREFACE[i];
        if (c == '\r') {
            std::cout << "\\r";
        } else if (c == '\n') {
            std::cout << "\\n";
        } else {
            std::cout << c;
        }
    }
    std::cout << std::endl;
    
    // Print as hex
    std::cout << "Hex representation:" << std::endl;
    for (size_t i = 0; i < strlen(HTTP2_CONNECTION_PREFACE); i++) {
        printf("%02x ", (unsigned char)HTTP2_CONNECTION_PREFACE[i]);
    }
    std::cout << std::endl;
    
    return 0;
}
