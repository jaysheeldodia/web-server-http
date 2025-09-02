# Professional HTTP/2 Web Server - Directory Structure

## Project Organization

This project has been professionally reorganized with a clean, scalable directory structure:

```
webserver-project/
├── README.md                    # Project documentation
├── Makefile                     # Professional build system
├── bin/                         # Compiled executables
│   └── webserver               # Main server binary
├── build/                       # Build artifacts
│   └── obj/                    # Object files (organized by module)
│       ├── core/
│       ├── handlers/
│       ├── network/
│       └── utils/
├── config/                      # Configuration files
├── docs/                        # Documentation
├── examples/                    # Example usage
├── include/                     # Header files (organized by module)
│   ├── core/                   # Core server functionality
│   │   ├── globals.h
│   │   ├── server.h
│   │   ├── shutdown_coordinator.h
│   │   └── thread_pool.h
│   ├── handlers/               # Protocol and content handlers
│   │   ├── file_handler.h
│   │   ├── http2_handler.h
│   │   ├── json_handler.h
│   │   └── websocket_handler.h
│   ├── network/                # Network layer
│   │   └── http_request.h
│   └── utils/                  # Utility functions
├── scripts/                     # Build and test scripts
│   ├── quick_tls_test.sh
│   ├── test_complete_tls.sh
│   ├── test_enhanced_features.sh
│   ├── test_http2.sh
│   └── test_tls_detection.sh
├── src/                         # Source files (organized by module)
│   ├── core/                   # Core server implementation
│   │   ├── main.cpp
│   │   ├── server.cpp
│   │   └── thread_pool.cpp
│   ├── handlers/               # Protocol and content handlers
│   │   ├── file_handler.cpp
│   │   ├── http2_handler.cpp
│   │   ├── json_handler.cpp
│   │   └── websocket_handler.cpp
│   ├── network/                # Network layer implementation
│   │   └── http_request.cpp
│   └── utils/                  # Utility implementations
├── tests/                       # Test suites
│   ├── integration/            # Integration tests
│   ├── unit/                   # Unit tests
│   └── performance/            # Performance tests
├── tools/                       # Development tools
│   ├── debug_test.cpp
│   ├── load_test.cpp
│   └── server_test.cpp
└── www/                         # Web content
    ├── index.html
    ├── about.html
    ├── dashboard.html
    ├── demo.html
    ├── http2-test.html
    ├── data.json
    └── style.css
```

## Build System

### Features
- **Modular Compilation**: Organized by functional modules (core, handlers, network, utils)
- **Multiple Build Targets**: debug, release, and default builds
- **Professional Output**: Color-coded build messages with emojis
- **Dependency Tracking**: Automatic header dependency management
- **Clean Organization**: Separate object files by module

### Available Targets

```bash
make all          # Build main application (default)
make debug        # Build with debug symbols
make release      # Build optimized release version  
make test         # Run integration tests
make clean        # Remove build artifacts
make help         # Show available targets
make info         # Show project information
make tools        # Build development tools
```

### Build Configuration

- **Compiler**: g++ with C++14 standard
- **Optimization**: -O2 for default, -O3 for release, -O0 for debug
- **Libraries**: OpenSSL (ssl, crypto), nghttp2, pthread
- **Warnings**: Wall, Wextra enabled
- **Threading**: Full pthread support

## Module Organization

### Core Module (`src/core/`, `include/core/`)
- **server.cpp/h**: Main HTTP/2 server with TLS and ALPN support
- **main.cpp**: Application entry point with signal handling
- **thread_pool.cpp/h**: Thread pool management with graceful shutdown
- **globals.h**: Global constants and configuration
- **shutdown_coordinator.h**: Coordinated shutdown management

### Handlers Module (`src/handlers/`, `include/handlers/`)
- **http2_handler.cpp/h**: HTTP/2 protocol implementation with server push and priority
- **file_handler.cpp/h**: Static file serving with MIME type detection
- **json_handler.cpp/h**: JSON API handling
- **websocket_handler.cpp/h**: WebSocket protocol support

### Network Module (`src/network/`, `include/network/`)
- **http_request.cpp/h**: HTTP request parsing and response generation

### Utils Module (`src/utils/`, `include/utils/`)
- Reserved for utility functions and helper classes

## Enhanced Features Implemented

### HTTP/2 Support
- ✅ **ALPN Negotiation**: Automatic protocol negotiation (h2, http/1.1)
- ✅ **Server Push**: Intelligent resource pushing for performance optimization
- ✅ **Priority Handling**: Stream priority and dependency management
- ✅ **TLS Integration**: Complete SSL/TLS support with certificate handling

### Server Features
- ✅ **Multithreaded**: Professional thread pool implementation
- ✅ **Keep-Alive**: HTTP connection persistence
- ✅ **WebSocket**: Full WebSocket protocol support
- ✅ **JSON API**: RESTful API endpoints
- ✅ **Static Files**: Efficient file serving with caching
- ✅ **Performance Metrics**: Built-in monitoring and dashboard

## Development Workflow

1. **Build**: `make clean && make all`
2. **Test**: `make test`
3. **Debug**: `make debug` for development builds
4. **Release**: `make release` for production builds
5. **Information**: `make info` to view project statistics

## Professional Standards Met

- ✅ **Clean Architecture**: Modular design with clear separation of concerns
- ✅ **Scalable Structure**: Easy to extend with new modules and features
- ✅ **Build System**: Professional Makefile with multiple targets and dependency tracking
- ✅ **Documentation**: Comprehensive README and inline code documentation
- ✅ **Testing**: Integrated test framework with multiple test types
- ✅ **Tools**: Development utilities for debugging and performance testing

This reorganization transforms the project from a simple HTTP server into a professionally structured, enterprise-ready HTTP/2 web server with complete TLS support and modern development practices.
