# WebSocket Support

The server supports **WebSocket** for real-time, two-way communication. The admin dashboard uses it to stream live metrics to the browser.

## How it works

1. The client sends an HTTP request with `Upgrade: websocket` and a `Sec-WebSocket-Key`.
2. The server validates the handshake and replies with `101 Switching Protocols`.
3. After that, both sides send **frames** (text, binary, ping, pong, close) over the same connection.

So the connection is upgraded from HTTP to WebSocket; no new port is needed.

## Endpoint

- **URL**: `ws://localhost:8080/ws` (or `ws://host:port/ws`). The path `/websocket` is also accepted.

## Dashboard integration

The admin dashboard opens a WebSocket to `/ws` and receives JSON messages with metric updates (e.g. request counts, CPU, memory). That way the page updates in real time without polling.

## Client example

```javascript
const ws = new WebSocket('ws://localhost:8080/ws');

ws.onopen = function() {
  console.log('Connected');
};

ws.onmessage = function(event) {
  const data = JSON.parse(event.data);
  console.log('Received:', data);
  // e.g. data.type === 'system_metrics', data.data has metrics
};

ws.onclose = function() {
  console.log('Closed');
};
```

The server sends messages in JSON; the exact shape depends on the message type (e.g. `metrics`, `system_metrics`, `request_rate`). See [WebSocket API](api-websocket.md) for the message formats.
