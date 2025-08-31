#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <memory>

class ServerTester {
private:
    std::string host;
    int port;
    
public:
    ServerTester(const std::string& h = "127.0.0.1", int p = 8080) : host(h), port(p) {}
    
    // Test basic GET request
    bool test_basic_get() {
        std::cout << "Testing basic GET request..." << std::endl;
        
        std::string response = send_http_request("GET / HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
        
        if (response.empty()) {
            std::cout << "❌ No response received" << std::endl;
            return false;
        }
        
        if (response.find("200 OK") != std::string::npos) {
            std::cout << "✅ Basic GET request successful" << std::endl;
            return true;
        }
        
        std::cout << "❌ Expected 200 OK, got: " << response.substr(0, 100) << "..." << std::endl;
        return false;
    }
    
    // Test 404 error handling
    bool test_404_response() {
        std::cout << "Testing 404 error handling..." << std::endl;
        
        std::string response = send_http_request("GET /nonexistent.html HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
        
        if (response.find("404 Not Found") != std::string::npos) {
            std::cout << "✅ 404 error handling works" << std::endl;
            return true;
        }
        
        std::cout << "❌ Expected 404 Not Found, got: " << response.substr(0, 100) << "..." << std::endl;
        return false;
    }
    
    // Test 405 Method Not Allowed
    bool test_405_method_not_allowed() {
        std::cout << "Testing 405 Method Not Allowed..." << std::endl;
        
        std::string response = send_http_request("POST / HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
        
        if (response.find("405 Method Not Allowed") != std::string::npos) {
            std::cout << "✅ 405 Method Not Allowed works" << std::endl;
            return true;
        }
        
        std::cout << "❌ Expected 405 Method Not Allowed, got: " << response.substr(0, 100) << "..." << std::endl;
        return false;
    }
    
    // Test Keep-Alive functionality
    bool test_keep_alive() {
        std::cout << "Testing Keep-Alive functionality..." << std::endl;
        
        int sock = create_socket();
        if (sock < 0) return false;
        
        // First request
        std::string request1 = "GET / HTTP/1.1\r\nHost: " + host + "\r\nConnection: keep-alive\r\n\r\n";
        if (send(sock, request1.c_str(), request1.length(), 0) < 0) {
            close(sock);
            std::cout << "❌ Failed to send first request" << std::endl;
            return false;
        }
        
        std::string response1 = read_response(sock);
        if (response1.empty() || response1.find("200 OK") == std::string::npos) {
            close(sock);
            std::cout << "❌ First request failed" << std::endl;
            return false;
        }
        
        // Check if connection is kept alive
        if (response1.find("Connection: keep-alive") == std::string::npos) {
            close(sock);
            std::cout << "❌ Server doesn't support Keep-Alive" << std::endl;
            return false;
        }
        
        // Second request on the same connection
        std::string request2 = "GET /about.html HTTP/1.1\r\nHost: " + host + "\r\nConnection: keep-alive\r\n\r\n";
        if (send(sock, request2.c_str(), request2.length(), 0) < 0) {
            close(sock);
            std::cout << "❌ Failed to send second request" << std::endl;
            return false;
        }
        
        std::string response2 = read_response(sock);
        close(sock);
        
        if (response2.empty()) {
            std::cout << "❌ Second request on kept-alive connection failed" << std::endl;
            return false;
        }
        
        std::cout << "✅ Keep-Alive functionality works" << std::endl;
        return true;
    }
    
    // Test concurrent connections
    bool test_concurrent_connections(int num_threads = 10, int requests_per_thread = 5) {
        std::cout << "Testing concurrent connections (" << num_threads << " threads, " 
                  << requests_per_thread << " requests each)..." << std::endl;
        
        std::vector<std::thread> threads;
        // Use std::unique_ptr<bool[]> instead of std::vector<bool> to avoid the bit-packing issue
        std::unique_ptr<bool[]> results(new bool[static_cast<size_t>(num_threads)]);
        
        // Initialize results array
        for (int i = 0; i < num_threads; ++i) {
            results[i] = false;
        }
        
        auto worker = [this](int /* thread_id */, int num_requests, bool* result) {
            *result = true;
            for (int i = 0; i < num_requests; ++i) {
                std::string response = send_http_request("GET / HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
                if (response.empty() || response.find("200 OK") == std::string::npos) {
                    *result = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small delay
            }
        };
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Start all threads
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker, i, requests_per_thread, &results[static_cast<size_t>(i)]);
        }
        
        // Wait for all threads to complete
        for (auto& t : threads) {
            t.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Check results
        int successful_threads = 0;
        for (int i = 0; i < num_threads; ++i) {
            if (results[i]) successful_threads++;
        }
        
        int total_requests = num_threads * requests_per_thread;
        int successful_requests = successful_threads * requests_per_thread;
        
        std::cout << "Completed " << successful_requests << "/" << total_requests 
                  << " requests in " << duration.count() << "ms" << std::endl;
        
        if (successful_threads == num_threads) {
            std::cout << "✅ Concurrent connections test passed" << std::endl;
            return true;
        } else {
            std::cout << "❌ " << (num_threads - successful_threads) << " threads failed" << std::endl;
            return false;
        }
    }
    
    // Test different file types and MIME types
    bool test_mime_types() {
        std::cout << "Testing MIME types..." << std::endl;
        
        struct TestFile {
            std::string path;
            std::string expected_type;
        };
        
        std::vector<TestFile> test_files = {
            {"/", "text/html"},
            {"/index.html", "text/html"},
            {"/about.html", "text/html"},
            {"/style.css", "text/css"},
            {"/data.json", "application/json"}
        };
        
        bool all_passed = true;
        
        for (const auto& file : test_files) {
            std::string request = "GET " + file.path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
            std::string response = send_http_request(request);
            
            if (response.find("Content-Type: " + file.expected_type) != std::string::npos) {
                std::cout << "✅ " << file.path << " -> " << file.expected_type << std::endl;
            } else {
                std::cout << "❌ " << file.path << " expected " << file.expected_type << std::endl;
                all_passed = false;
            }
        }
        
        return all_passed;
    }
    
    // Test malformed requests
    bool test_malformed_requests() {
        std::cout << "Testing malformed request handling..." << std::endl;
        
        std::vector<std::string> malformed_requests = {
            "INVALID REQUEST\r\n\r\n",
            "GET\r\n\r\n",
            "GET /\r\n\r\n",
            "GET / HTTP/1.1\r\nInvalid-Header\r\n\r\n"
        };
        
        bool all_handled = true;
        
        for (const auto& request : malformed_requests) {
            std::string response = send_http_request(request);
            
            // Should get 400 Bad Request or connection closed
            if (response.empty() || response.find("400 Bad Request") != std::string::npos) {
                std::cout << "✅ Malformed request handled correctly" << std::endl;
            } else {
                std::cout << "❌ Malformed request not handled properly" << std::endl;
                all_handled = false;
            }
        }
        
        return all_handled;
    }
    
    // Run all tests
    void run_all_tests() {
        std::cout << "=== Starting Web Server Tests ===" << std::endl;
        std::cout << "Target: http://" << host << ":" << port << std::endl << std::endl;
        
        std::vector<std::pair<std::string, bool>> test_results;
        
        test_results.push_back({"Basic GET Request", test_basic_get()});
        test_results.push_back({"404 Error Handling", test_404_response()});
        test_results.push_back({"405 Method Not Allowed", test_405_method_not_allowed()});
        test_results.push_back({"Keep-Alive Functionality", test_keep_alive()});
        test_results.push_back({"MIME Types", test_mime_types()});
        test_results.push_back({"Malformed Requests", test_malformed_requests()});
        test_results.push_back({"Concurrent Connections", test_concurrent_connections()});
        
        // Print summary
        std::cout << std::endl << "=== Test Summary ===" << std::endl;
        int passed = 0;
        for (const auto& result : test_results) {
            std::cout << (result.second ? "✅ " : "❌ ") << result.first << std::endl;
            if (result.second) passed++;
        }
        
        std::cout << std::endl << "Passed: " << passed << "/" << test_results.size() << " tests" << std::endl;
        
        if (static_cast<size_t>(passed) == test_results.size()) {
            std::cout << "🎉 All tests passed!" << std::endl;
        } else {
            std::cout << "❌ Some tests failed. Please check the server implementation." << std::endl;
        }
    }

private:
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
                // In a more complete implementation, we'd check Content-Length
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
    
    ServerTester tester(host, port);
    tester.run_all_tests();
    
    return 0;
}