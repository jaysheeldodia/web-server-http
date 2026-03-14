# Quick Start

Get the server built and running in a few minutes.

## Prerequisites

- A C++14-capable compiler (e.g. GCC 5.4+ or Clang 3.4+)
- OpenSSL development libraries
- nghttp2 library (for HTTP/2)
- Make

## Install dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential libssl-dev libnghttp2-dev

# CentOS/RHEL
sudo yum install gcc-c++ openssl-devel libnghttp2-devel

# macOS
brew install openssl nghttp2
```

## Build and run

```bash
# Clone the repository
git clone https://github.com/jaysheeldodia/web-server-http.git
cd web-server-http

# Build the server
make

# Start the server (default port 8080, document root ./www)
./bin/webserver

# Or use custom port and document root
./bin/webserver -p 3000 -d /var/www/html
```

## First request

```bash
# Basic request
curl http://localhost:8080/

# Server statistics (JSON API)
curl http://localhost:8080/api/stats

# Open the admin dashboard in a browser
# http://localhost:8080/admin-dashboard
```

Once the server is running, open the admin dashboard to see real-time performance metrics.
