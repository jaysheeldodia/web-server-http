# Installation Guide

System requirements and platform-specific steps.

## System requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 1 core | 4+ cores |
| RAM | 512 MB | 2+ GB |
| GCC | 5.4 | 9.0+ |
| OpenSSL | 1.0.2 | 1.1.1+ |
| nghttp2 | 1.30.0 | 1.40.0+ |

## Ubuntu 20.04+

```bash
sudo apt-get update
sudo apt-get install -y build-essential libssl-dev libnghttp2-dev git
git clone https://github.com/jaysheeldodia/web-server-http.git
cd web-server-http
make
./bin/webserver
```

## CentOS 8+

```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install openssl-devel libnghttp2-devel git
git clone https://github.com/jaysheeldodia/web-server-http.git
cd web-server-http
make
./bin/webserver
```

## macOS

```bash
# Install Homebrew if needed, then:
brew install openssl nghttp2 git
git clone https://github.com/jaysheeldodia/web-server-http.git
cd web-server-http
make
./bin/webserver
```

On macOS you may need to adjust compiler or linker flags if you use a different Xcode or command-line tools version.
