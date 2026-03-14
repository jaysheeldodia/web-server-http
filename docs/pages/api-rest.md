# REST API Reference

The server exposes a small REST-style API that returns JSON. All of these are HTTP endpoints you can call with `curl` or from a browser/frontend.

## Server statistics

### GET /api/stats

Returns current server performance statistics.

**Example**

```bash
curl http://localhost:8080/api/stats
```

**Response**

```json
{
  "success": true,
  "message": "Server statistics",
  "data": {
    "total_requests": 1250,
    "active_connections": 15,
    "thread_count": 4,
    "queue_size": 2
  }
}
```

## User management (demo)

The server keeps an in-memory list of users for demonstration. Data is lost when the server restarts.

### GET /api/users

List all users.

**Response**

```json
{
  "success": true,
  "message": "Users list retrieved",
  "data": [
    {
      "id": "1",
      "name": "John Doe",
      "email": "john.doe@example.com"
    }
  ]
}
```

### POST /api/users

Create a new user. Send a JSON body with `name` and `email`.

**Request**

```json
{
  "name": "Jane Smith",
  "email": "jane.smith@example.com"
}
```

**Response**

```json
{
  "success": true,
  "message": "User created successfully",
  "data": {
    "id": 2,
    "name": "Jane Smith",
    "email": "jane.smith@example.com"
  }
}
```

### GET /api/users/{id}

Get one user by numeric ID.

**Response**

```json
{
  "success": true,
  "message": "User data retrieved",
  "data": {
    "id": 1,
    "name": "John Doe",
    "email": "john.doe@example.com"
  }
}
```

## API documentation page

### GET /api/docs

Returns an HTML page that describes the API and lists these endpoints. Open in a browser:

- `http://localhost:8080/api/docs`

This is the interactive API documentation the server serves.
