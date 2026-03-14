# Benchmarks

How to observe performance and what the server reports.

## Using the admin dashboard

The best way to see live performance is the **admin dashboard**:

- **URL**: `http://localhost:8080/admin-dashboard`
- It shows requests per second, total requests, active connections, memory and CPU usage, thread pool queue size, and charts that update over time.

Use it while generating load (e.g. with `curl` in a loop or a load-test script) to see how the server behaves.

## API statistics

The REST API exposes basic counters:

```bash
curl http://localhost:8080/api/stats
```

The response is JSON with fields such as `total_requests`, `active_connections`, `thread_count`, and `queue_size`. You can script against this for simple monitoring.

## Throughput (reference)

Under typical conditions, a small thread pool (e.g. 4 workers) can handle on the order of tens of thousands of simple requests per second, depending on hardware and workload. Exact numbers depend on request type, response size, and network. Use the dashboard and `/api/stats` to measure your own setup.

## Load testing

To stress-test the server you can:

- Use the test programs under `tests/unit/` (e.g. load test) if you build them.
- Run many concurrent `curl` requests (e.g. in a loop or with a tool like `ab` or `wrk` if installed).

Watch the admin dashboard and system resources while doing this to spot bottlenecks.
