#include "../include/server.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>
#include "../include/globals.h"


WebServer* server_instance = nullptr;
std::atomic<bool> g_shutdown_requested{false};

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    static bool shutdown_in_progress = false;
    
    if (shutdown_in_progress) {
        // Force exit if already shutting down
        std::cout << "\nForce exit requested!" << std::endl;
        _exit(1);
    }
    
    shutdown_in_progress = true;
    g_shutdown_requested = true;
    
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    
    if (server_instance) {
        // Start cleanup in a separate thread to avoid deadlock
        std::thread cleanup_thread([&]() {
            try {
                server_instance->cleanup();
            } catch (const std::exception& e) {
                std::cerr << "Cleanup error: " << e.what() << std::endl;
            }
        });
        
        // Give cleanup thread 5 seconds to complete
        if (cleanup_thread.joinable()) {
            auto future = std::async(std::launch::async, [&]() {
                cleanup_thread.join();
            });
            
            if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                std::cout << "Cleanup timeout - forcing exit" << std::endl;
                cleanup_thread.detach();
            }
        }
    }
    
    exit(0);
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port PORT        Server port (default: 8080)" << std::endl;
    std::cout << "  -d, --docroot PATH     Document root directory (default: ./www)" << std::endl;
    std::cout << "  -t, --threads COUNT    Thread pool size (default: 4)" << std::endl;
    std::cout << "  -k, --keep-alive       Enable Keep-Alive (default: enabled)" << std::endl;
    std::cout << "  -T, --timeout SECONDS  Keep-Alive timeout (default: 5)" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                    # Default settings" << std::endl;
    std::cout << "  " << program_name << " -p 8081           # Custom port" << std::endl;
    std::cout << "  " << program_name << " -p 8080 -t 8      # Port 8080, 8 threads" << std::endl;
    std::cout << "  " << program_name << " -k -T 10          # Keep-Alive with 10s timeout" << std::endl;
}

void print_server_info(int port, const std::string& doc_root, size_t thread_count, 
                      bool keep_alive, int timeout) {
    std::cout << "=== Server Configuration ===" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Document root: " << doc_root << std::endl;
    std::cout << "Thread count: " << thread_count << std::endl;
    std::cout << "Keep-Alive: " << (keep_alive ? "enabled" : "disabled") << std::endl;
    if (keep_alive) {
        std::cout << "Keep-Alive timeout: " << timeout << " seconds" << std::endl;
    }
    std::cout << "============================" << std::endl;
}

void monitor_server_stats() {
    auto start_time = std::chrono::steady_clock::now();
    size_t last_request_count = 0;
    
    while (server_instance && !g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(30)); // Report every 30 seconds
        
        if (!server_instance || g_shutdown_requested) break;
        
        try {
            auto now = std::chrono::steady_clock::now();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            size_t current_requests = server_instance->get_total_requests();
            size_t requests_per_30s = current_requests - last_request_count;
            
            std::cout << "\n=== Server Stats (Uptime: " << uptime << "s) ===" << std::endl;
            std::cout << "Total requests: " << current_requests << std::endl;
            std::cout << "Requests/30s: " << requests_per_30s << std::endl;
            std::cout << "Active connections: " << server_instance->get_active_connections() << std::endl;
            std::cout << "Avg requests/s: " << (uptime > 0 ? current_requests / uptime : 0) << std::endl;
            std::cout << "==============================" << std::endl;
            
            last_request_count = current_requests;
        } catch (const std::exception& e) {
            if (!g_shutdown_requested) {
                std::cerr << "Stats monitoring error: " << e.what() << std::endl;
            }
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    // Default configuration
    int port = 8080;
    std::string doc_root = "./www";
    size_t thread_count = 4;
    bool keep_alive_enabled = true;
    int keep_alive_timeout = 5;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
                if (port <= 0 || port > 65535) {
                    std::cerr << "Error: Port must be between 1 and 65535" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-d" || arg == "--docroot") {
            if (i + 1 < argc) {
                doc_root = argv[++i];
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) {
                thread_count = std::stoi(argv[++i]);
                if (thread_count == 0) {
                    std::cerr << "Error: Thread count must be greater than 0" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-k" || arg == "--keep-alive") {
            keep_alive_enabled = true;
        }
        else if (arg == "--no-keep-alive") {
            keep_alive_enabled = false;
        }
        else if (arg == "-T" || arg == "--timeout") {
            if (i + 1 < argc) {
                keep_alive_timeout = std::stoi(argv[++i]);
                if (keep_alive_timeout <= 0) {
                    std::cerr << "Error: Timeout must be greater than 0" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else {
            // Legacy positional argument support for backward compatibility
            if (i == 1) {
                port = std::stoi(arg);
            } else if (i == 2) {
                doc_root = arg;
            } else if (i == 3) {
                thread_count = std::stoi(arg);
            } else {
                std::cerr << "Error: Unknown argument: " << arg << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    // Set up signal handlers for graceful shutdown
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe signals

    // Print configuration
    print_server_info(port, doc_root, thread_count, keep_alive_enabled, keep_alive_timeout);

    // Create and initialize server
    try {
        WebServer server(port, doc_root, thread_count);
        server_instance = &server;

        // Enable Keep-Alive if requested
        if (keep_alive_enabled) {
            server.enable_keep_alive(true, keep_alive_timeout);
        }
        
        // Enable HTTP/2 support
        server.enable_http2(true);

        if (!server.initialize()) {
            std::cerr << "Failed to initialize server" << std::endl;
            return 1;
        }

        // Start monitoring thread for server statistics
        std::thread stats_thread;
        try {
            stats_thread = std::thread(monitor_server_stats);
        } catch (const std::exception& e) {
            std::cerr << "Failed to start stats thread: " << e.what() << std::endl;
        }

        // Start the server (this will run until shutdown)
        std::cout << "\nServer ready! Access it at:" << std::endl;
        std::cout << "  Web Interface: http://localhost:" << port << std::endl;
        std::cout << "  API Docs: http://localhost:" << port << "/api/docs" << std::endl;
        std::cout << "  Dashboard: http://localhost:" << port << "/dashboard" << std::endl;
        std::cout << "  WebSocket: ws://localhost:" << port << "/ws" << std::endl;
        std::cout << "\nPress Ctrl+C to stop the server\n" << std::endl;
        
        server.start();
        
        // Clean up stats thread
        if (stats_thread.joinable()) {
            stats_thread.detach(); // Let it finish on its own
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        g_shutdown_requested = true;
        return 1;
    }

    server_instance = nullptr;
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
}