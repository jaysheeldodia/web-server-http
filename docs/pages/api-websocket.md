# WebSocket API

The WebSocket endpoint and the JSON message formats the server sends.

## Connection

- **Endpoint**: `ws://localhost:8080/ws` (or `wss://...` if TLS is enabled)
- **Protocol**: Messages are JSON. The server pushes updates; the client can also send messages (e.g. to subscribe to metrics).

## Message types

The server sends objects with a `type` field and often a `data` field.

### metrics

General metrics snapshot.

```json
{
  "type": "metrics",
  "data": {
    "total_requests": 1250,
    "requests_per_minute": 45,
    "timestamp": 1630454400000
  }
}
```

### request_rate

Request rate over time (for charts).

```json
{
  "type": "request_rate",
  "data": [
    {
      "timestamp": 1630454400000,
      "count": 12
    }
  ]
}
```

### system_metrics

System and server metrics (memory, CPU, connections, queue).

```json
{
  "type": "system_metrics",
  "data": [
    {
      "timestamp": 1630454400000,
      "memory_mb": 45,
      "cpu_percent": 12.5,
      "active_connections": 8,
      "total_requests": 1250,
      "requests_per_second": 2.1,
      "queue_size": 0,
      "thread_count": 4
    }
  ]
}
```

## JavaScript client example

```javascript
const ws = new WebSocket('ws://localhost:8080/ws');

ws.onopen = function() {
  console.log('Connected');
};

ws.onmessage = function(event) {
  const msg = JSON.parse(event.data);
  switch (msg.type) {
    case 'metrics':
      updateMetrics(msg.data);
      break;
    case 'system_metrics':
      updateCharts(msg.data);
      break;
    case 'request_rate':
      updateRateChart(msg.data);
      break;
  }
};

ws.onclose = function() {
  console.log('Closed');
};
```
