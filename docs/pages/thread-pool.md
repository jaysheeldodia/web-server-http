# Thread Pool

How the server handles many requests at once without creating a new thread per request.

## What the thread pool does

The server uses a **fixed-size thread pool**:

- At startup, a fixed number of **worker threads** are created (default 4, set with `-t` / `--threads`).
- There is **one shared task queue**: when a new client connection is accepted, a task (function) that handles that connection is added to the queue.
- Each worker thread repeatedly: waits for a task, takes one from the queue (protected by a mutex and condition variable), runs it, then goes back to wait for the next task.
- When the server shuts down, the pool is stopped and all workers finish cleanly.

So: one queue, one mutex, no work stealing, no lock-free queue, no dynamic sizing, and no task priorities. This keeps the implementation simple and predictable.

## Configuration

| Parameter | Default | Description |
|-----------|---------|-------------|
| Worker threads | 4 | Set via `-t` or `--threads` on the command line |

The thread count is passed from `main.cpp` into the `WebServer` constructor; the pool is created there. There is no separate “queue size” or “max threads” option in the current implementation.

## Usage in code

The pool exposes an **enqueue** operation: you give it a callable (e.g. a lambda that handles one client), and it runs on one of the worker threads. The server enqueues a task per accepted connection. See `include/core/thread_pool.h` and `src/core/thread_pool.cpp` for the implementation.

## Tuning

For I/O-heavy workloads (many connections waiting on network), using roughly 2–4× the number of CPU cores is often reasonable. For CPU-heavy work, match the number of cores. Start with the default (4) and adjust with `-t` if needed.
