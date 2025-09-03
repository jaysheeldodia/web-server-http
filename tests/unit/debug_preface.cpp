
#include <iostream>
#include <cstring>

const char HTTP2_CONNECTION_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

int main() {
    std::cout << "HTTP/2 Connection Preface Debug\n";
    std::cout << "===============================\n";
    
    std::cout << "Expected preface length: " << strlen(HTTP2_CONNECTION_PREFACE) << " bytes\n";
    std::cout << "Expected preface (hex): ";
    for (size_t i = 0; i < strlen(HTTP2_CONNECTION_PREFACE); i++) {
        printf("%02x ", (unsigned char)HTTP2_CONNECTION_PREFACE[i]);
    }
    std::cout << "\n";
    
    std::cout << "Expected preface (string): '";
    for (size_t i = 0; i < strlen(HTTP2_CONNECTION_PREFACE); i++) {
        char c = HTTP2_CONNECTION_PREFACE[i];
        if (c == '\r') std::cout << "\\r";
        else if (c == '\n') std::cout << "\\n";
        else std::cout << c;
    }
    std::cout << "'\n";
    
    return 0;
}
