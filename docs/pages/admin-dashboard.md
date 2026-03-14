# Admin Dashboard

The server ships with a web-based admin dashboard for real-time monitoring.

## Two dashboard URLs

- **Admin dashboard (full metrics)**: `http://localhost:8080/admin-dashboard`  
  This is the main monitoring page: live metrics, charts, request rate, CPU/memory, connections, and queue size. It uses WebSocket to update in real time.

- **Dashboard (simpler page)**: `http://localhost:8080/dashboard` or `http://localhost:8080/dashboard.html`  
  A simpler dashboard page; the “admin” one above is the one with full real-time metrics.

## What the admin dashboard shows

- **Requests per second** – current throughput
- **Total requests** – count since server start
- **Active connections** – number of open connections
- **Memory and CPU** – server process usage
- **Queue size** – pending tasks in the thread pool
- **Charts** – request rate and system metrics over time

You can use it during load testing to watch how the server behaves and to spot bottlenecks.

## Interactive features

- Auto-refresh of metrics (via WebSocket)
- Option to export or clear chart data (if implemented in the dashboard HTML)
- Responsive layout so it works on different screen sizes

The dashboard is static HTML/JS/CSS served from the document root (e.g. `www/admin-dashboard.html`). The server only serves the file and provides the WebSocket endpoint (`/ws`) and API (`/api/stats`) that the page uses.
