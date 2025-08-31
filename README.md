# C++ Multithreaded HTTP Webserver

A high-performance, multithreaded HTTP webserver written in C++. This project demonstrates modern C++ techniques for building a robust, extensible webserver with support for static file serving, Keep-Alive connections, configurable thread pool, and more.

## Features

- **Multithreaded Request Handling:** Efficiently handles multiple simultaneous connections using a thread pool.
- **Configurable Server:** Set port, document root, thread count, Keep-Alive, and timeout via command-line options.
- **Static File Serving:** Serves HTML, CSS, JSON, and other static files from the `www/` directory.
- **Keep-Alive Support:** Optional persistent connections for improved performance.
- **Graceful Shutdown:** Handles signals for safe server termination.
- **Request Logging:** Tracks and logs incoming requests.
- **Extensible Design:** Modular codebase with clear separation (server, HTTP request, file handler, thread pool).
- **Unit & Load Testing:** Includes test utilities for server validation and benchmarking.

## Directory Structure

```
.
├── include/         # Header files (server, http_request, file_handler, thread_pool)
├── src/             # Source files (main.cpp, server.cpp, etc.)
├── www/             # Static web content (HTML, CSS, JSON)
├── tests/           # Test scripts and utilities
├── Makefile         # Build instructions
├── README.md        # Project documentation
```

## Getting Started

### Prerequisites

- Linux (tested on Ubuntu)
- g++ (C++14 or newer)
- Make

### Build

```bash
make
```

This will build the main server executable (`webserver`), load tester, and server tester.

### Run the Server

```bash
./webserver [OPTIONS]
```

#### Options

- `-p, --port PORT` &nbsp;&nbsp;&nbsp;&nbsp;Server port (default: 8080)
- `-d, --docroot PATH` &nbsp;&nbsp;Document root directory (default: ./www)
- `-t, --threads COUNT` &nbsp;&nbsp;Thread pool size (default: 4)
- `-k, --keep-alive` &nbsp;&nbsp;&nbsp;&nbsp;Enable Keep-Alive (default: enabled)
- `-T, --timeout SECONDS` &nbsp;&nbsp;Keep-Alive timeout (default: 5)
- `-h, --help` &nbsp;&nbsp;&nbsp;&nbsp;Show help message

#### Examples

```bash
./webserver
./webserver -p 8081
./webserver -d ./www -t 8
./webserver -k -T 10
```

### Accessing the Server

Open your browser and navigate to:

```
http://localhost:8080/
```

Static files are served from the `www/` directory.

## Testing

- **Unit Tests:** Run `server_test.cpp` and other test files in `tests/` for validation.
- **Load Testing:** Use `load_tester` to benchmark server performance.

## Code Overview

- **`main.cpp`**: Entry point, parses arguments, starts server.
- **`server.h/cpp`**: Core server logic, connection handling, graceful shutdown.
- **`http_request.h/cpp`**: HTTP request parsing and validation.
- **`file_handler.h/cpp`**: Static file serving, MIME type resolution.
- **`thread_pool.h/cpp`**: Thread pool implementation for concurrent request processing.

## Extending

- Add new HTTP methods or features by extending `HttpRequest` and `WebServer`.
- Customize MIME types in `FileHandler`.
- Add new tests in the `tests/` directory.

## License

MIT License. See [LICENSE](LICENSE) for details.

## Author

[jaysheeldodia](https://github.com/jaysheeldodia)
