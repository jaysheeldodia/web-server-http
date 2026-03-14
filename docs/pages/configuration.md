# Configuration

Command-line options and how to tune the server.

## Command-line options

| Option | Default | Description |
|--------|---------|-------------|
| `-p`, `--port` | 8080 | Port the server listens on |
| `-d`, `--docroot` | ./www | Document root directory for static files |
| `-t`, `--threads` | 4 | Number of worker threads in the thread pool |
| `-k`, `--keep-alive` | enabled | Enable HTTP Keep-Alive |
| `--no-keep-alive` | — | Disable Keep-Alive |
| `-T`, `--timeout` | 5 | Keep-Alive timeout in seconds |
| `-h`, `--help` | — | Show usage and exit |

Examples:

```bash
./bin/webserver                          # Defaults: port 8080, doc root ./www, 4 threads
./bin/webserver -p 8081                  # Custom port
./bin/webserver -p 8080 -d /var/www/html # Custom document root
./bin/webserver -t 8                     # 8 worker threads
./bin/webserver -k -T 10                 # Keep-Alive with 10 second timeout
```

## TLS/SSL

To run with HTTPS you need a certificate and private key. For local testing you can create a self-signed certificate:

```bash
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes
mkdir -p config
mv cert.pem key.pem config/
```

TLS is enabled in code when certificate files are present and TLS is turned on (see `src/core/main.cpp`). The server can be built to use TLS with ALPN for HTTP/2 negotiation.

## Performance tuning

**Thread count**: Use roughly the number of CPU cores for CPU-bound work, or 2–4× cores for I/O-bound work. Start with the default (4) or set `-t` to match your machine.

**Connection limits**: For many concurrent connections, raise the system limit on open files:

```bash
ulimit -n 65536
# For a permanent change, adjust /etc/security/limits.conf as needed
```
