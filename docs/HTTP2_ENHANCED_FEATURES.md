# HTTP/2 Enhanced Features Configuration Guide

## Features Implemented

### 1. ALPN (Application-Layer Protocol Negotiation)
- **Status**: ✅ IMPLEMENTED
- **Purpose**: Enables automatic protocol negotiation during TLS handshake
- **Protocols Supported**: 
  - `h2` (HTTP/2 over TLS)
  - `http/1.1` (HTTP/1.1 over TLS)
- **Usage**: Requires TLS to be enabled with valid certificates

### 2. Server Push
- **Status**: ✅ IMPLEMENTED
- **Purpose**: Proactively sends resources to clients before they request them
- **Auto-Push Rules**:
  - `index.html` → pushes `style.css`, `demo.html`
  - `dashboard.html` → pushes `style.css`, `data.json`
  - `demo.html` → pushes `style.css`
- **Benefits**: Reduces latency and improves page load times

### 3. Priority Handling
- **Status**: ✅ IMPLEMENTED
- **Purpose**: Manages stream dependencies and weights for optimal resource delivery
- **Features**:
  - Stream priority tracking
  - Dependency management
  - Weight-based scheduling
  - Exclusive dependency support

## Enabling TLS/ALPN

### Step 1: Generate Self-Signed Certificates (for testing)
```bash
# Generate private key
openssl genrsa -out key.pem 2048

# Generate certificate signing request
openssl req -new -key key.pem -out cert.csr

# Generate self-signed certificate
openssl x509 -req -days 365 -in cert.csr -signkey key.pem -out cert.pem

# Cleanup
rm cert.csr
```

### Step 2: Modify main.cpp
Uncomment the TLS enablement line in `src/main.cpp`:
```cpp
// Change this line:
// server.enable_tls(true, "cert.pem", "key.pem");
// To:
server.enable_tls(true, "cert.pem", "key.pem");
```

### Step 3: Rebuild and Test
```bash
make clean && make
./webserver

# Test ALPN negotiation
curl -v --http2 https://localhost:8080/ -k
```

## Testing the Features

### Test Server Push
```bash
# Start server
./webserver

# Monitor server logs while accessing:
curl --http2-prior-knowledge http://localhost:8080/

# Look for push promise messages in server output
```

### Test Priority Handling
```bash
# Multiple concurrent requests (observe priority handling in logs)
curl --http2-prior-knowledge http://localhost:8080/style.css &
curl --http2-prior-knowledge http://localhost:8080/demo.html &
curl --http2-prior-knowledge http://localhost:8080/data.json &
wait
```

### Test ALPN (requires TLS)
```bash
# After enabling TLS with certificates:
curl -v --http2 https://localhost:8080/ -k

# Look for ALPN negotiation in server logs:
# "ALPN negotiated protocol: h2"
```

## Browser Testing

Modern browsers support these features:

1. **Chrome/Edge**: 
   - Open DevTools → Network tab
   - Look for "Push" column to see server-pushed resources
   - Protocol column shows h2 for HTTP/2

2. **Firefox**:
   - about:networking → HTTP/2 tab shows active streams
   - DevTools Network tab shows protocol information

## Performance Benefits

### Server Push Benefits:
- **Reduced RTT**: Resources sent before client requests
- **Improved First Paint**: Critical CSS/JS pushed immediately
- **Better UX**: Faster perceived loading times

### Priority Handling Benefits:
- **Optimal Resource Order**: Critical resources prioritized
- **Bandwidth Management**: Fair sharing among streams
- **Responsive Loading**: User interactions get priority

### ALPN Benefits:
- **Seamless Upgrades**: Automatic HTTP/2 over TLS
- **Fallback Support**: Graceful degradation to HTTP/1.1
- **Security**: TLS encryption with protocol optimization

## Monitoring and Debugging

### Server Logs Show:
- Push promise submissions
- Priority frame handling
- ALPN protocol negotiation
- Stream lifecycle events

### Client Tools:
- Browser DevTools (Network tab)
- `curl -v` for verbose output
- `nghttp` for advanced HTTP/2 testing

## Production Considerations

1. **Certificate Management**: Use proper CA-signed certificates
2. **Push Policies**: Implement smart push logic based on usage patterns
3. **Priority Tuning**: Adjust stream priorities based on content type
4. **Cache Awareness**: Coordinate with cache policies for pushed resources

## Example Usage in Code

```cpp
// Enable all features
WebServer server(8080, "./www", 4);
server.enable_http2(true);
server.enable_tls(true, "cert.pem", "key.pem");

// Custom push resources can be added by modifying
// HTTP2Handler::identify_push_resources() method
```

This implementation provides a solid foundation for HTTP/2 enhanced features that can be further customized based on specific application needs.
