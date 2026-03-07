# Redis-like Server Implementation

This project is a custom C++ implementation of a Redis-like in-memory key-value store, designed as a high-performance database, cache, and message broker. It stores data in the server's main memory (RAM) rather than on disk, enabling extremely fast read and write operations with sub-millisecond latency.

## Features

- **In-memory storage**: All data is stored in RAM for maximum speed
- **Key-value operations**: Basic GET, SET, DEL commands
- **TTL (Time To Live)**: Automatic expiration of keys with PEXPIRE and PTTL
- **Sorted Sets (ZSET)**: Ordered collections with scores, supporting ZADD, ZREM, ZSCORE, ZQUERY
- **Thread pool**: Multi-threaded request handling for better performance
- **Custom binary protocol**: Efficient serialization for client-server communication
- **Connection management**: Idle timeout and connection pooling
- **Data structures**: Custom implementations of AVL trees, hashtables, heaps, and doubly-linked lists

## Architecture

The server is built using several custom data structures and components:

### Core Components
- **Server (14_server.cpp)**: Main server loop handling connections, requests, and responses
- **Data Structures**:
  - `hashtable.cpp/h`: Hash table with progressive rehashing
  - `zset.cpp/h`: Sorted sets using AVL trees and hash tables
  - `avl.cpp/h`: Self-balancing AVL tree implementation
  - `heap.cpp/h`: Min-heap for TTL management
  - `list.h`: Doubly-linked list utilities
  - `thread_pool.cpp/h`: Thread pool for asynchronous operations
- **Common utilities (common.h)**: Hash functions and container macros

### Key Features
- **Event-driven I/O**: Uses `poll()` for efficient socket handling
- **Non-blocking sockets**: All connections are set to non-blocking mode
- **Request pipelining**: Supports multiple requests per connection
- **TTL management**: Uses a heap to efficiently track and expire keys
- **Thread pool**: Handles large data structure deletions asynchronously

## Build Instructions

### Prerequisites
- C++11 compatible compiler (g++ recommended)
- POSIX-compliant system (Linux/macOS)
- pthread library

### Compilation
Navigate to the `redis/` directory and compile all source files:

```bash
cd redis
g++ -std=c++11 -pthread *.cpp -o server
```

This will create an executable named `server` in the `redis/` directory.

## Usage

### Starting the Server
Run the compiled server:

```bash
./redis/server
```

The server will start listening on port 1234 (hardcoded in the source).

### Connecting to the Server
You can connect using any TCP client, but you'll need to use the custom binary protocol. For testing, you can use:

- **telnet** (though it won't handle binary data well)
- **netcat (nc)**: `nc localhost 1234`
- A custom client that implements the protocol

### Protocol Overview
The server uses a custom binary protocol:

1. **Message Format**: `[4-byte length][payload]`
2. **Request**: Array of strings: `[nstr][len1][str1][len2][str2]...`
3. **Response**: Tagged data types (NIL, ERR, STR, INT, DBL, ARR)

### Supported Commands

#### Basic Key-Value Operations
- `GET <key>`: Retrieve the value of a key
- `SET <key> <value>`: Set the value of a key
- `DEL <key>`: Delete a key
- `KEYS`: List all keys

#### TTL Operations
- `PEXPIRE <key> <ttl_ms>`: Set expiration time in milliseconds
- `PTTL <key>`: Get remaining TTL in milliseconds

#### Sorted Set Operations
- `ZADD <zset> <score> <name>`: Add member to sorted set
- `ZREM <zset> <name>`: Remove member from sorted set
- `ZSCORE <zset> <name>`: Get score of a member
- `ZQUERY <zset> <score> <name> <offset> <limit>`: Query members with pagination

### Example Usage

1. Start the server
2. Connect with a client that can send binary data
3. Send commands using the protocol format

For development/testing, you might want to implement a simple client or modify the protocol to use text-based commands.

## Project Structure

- `redis/`: Main source code directory
  - `14_server.cpp`: Main server implementation
  - Data structure implementations (*.cpp/*.h)
- `README.md`: This file
- `basics_compNetwork.txt`: Notes on computer networking basics
- `server.txt`: Server and networking concepts
- `socket_notes.txt`: Socket programming notes
- `tech.txt`: Technology overview and computer engineering concepts

## Notes

- This is a learning implementation, not a production-ready Redis clone
- The server uses a simple custom protocol, not the official Redis protocol
- Connection idle timeout is set to 5 seconds
- The server supports up to 200,000 arguments per request (safety limit)
- Large data structures are deleted asynchronously using the thread pool

## Future Improvements

- Implement more Redis commands
- Add persistence (RDB/AOF)
- Support for clustering/replication
- Official Redis protocol compatibility
- Configuration file support
- Better error handling and logging
