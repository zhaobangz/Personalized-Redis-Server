# Redis-like Server Implementation

This project is a custom C++ implementation of a Redis-like in-memory key-value store, designed as a high-performance database, cache, and message broker. It stores data in the server's main memory (RAM) rather than on disk, enabling extremely fast read and write operations with sub-millisecond latency.

## Features

- **In-memory storage**: All data is stored in RAM for maximum speed
- **Key-value operations**: GET, SET, DEL, EXISTS, KEYS
- **Numeric operations**: INCR, DECR
- **Type introspection**: TYPE
- **Utility commands**: PING, ECHO, FLUSHDB
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
- **Common utilities (common.h)**: Hash functions and container macros (container_of)

### Key Features
- **Event-driven I/O**: Uses `poll()` for efficient socket handling
- **Non-blocking sockets**: All connections are set to non-blocking mode
- **Request pipelining**: Supports multiple requests per connection
- **TTL management**: Uses a heap to efficiently track and expire keys
- **Thread pool**: Handles large data structure deletions asynchronously

## Build Instructions

### Prerequisites
- C++11 compatible compiler (g++ or Clang)
- POSIX-compliant system (Linux/macOS)
- pthread library

### Quick Build
```bash
cd "redis "
make
```

This builds both the server (`server`) and the test client (`test_client`).

### Manual Compilation
```bash
cd "redis "
g++ -std=c++11 -pthread *.cpp -o server
g++ -std=c++11 -pthread test_client.cpp -o test_client
```

## Usage

### Starting the Server
```bash
./redis /server
```
The server listens on port 1234 (0.0.0.0).

### Running Tests
```bash
make test
```
Or manually:
```bash
# Terminal 1
./redis /server

# Terminal 2
./redis /test_client
```

### Connecting to the Server
You can connect using any TCP client, but you'll need to use the custom binary protocol. Use the provided `test_client` as a reference implementation or connect with:
- **telnet** (though it won't handle binary data well)
- **netcat (nc)**: `nc localhost 1234`

### Protocol Overview
The server uses a custom binary protocol:

1. **Message Format**: `[4-byte length][payload]`
2. **Request**: Array of strings: `[nstr:4][len1:4][str1][len2:4][str2]...`
3. **Response**: Tagged data types (NIL, ERR, STR, INT, DBL, ARR)

All integers are 4-byte little-endian (`uint32_t`), with INT64 and DBL using 8 bytes.

### Supported Commands

#### Basic Key-Value Operations
| Command | Syntax | Description |
|---------|--------|-------------|
| GET | `GET <key>` | Retrieve the value of a key (returns nil if not found) |
| SET | `SET <key> <value>` | Set the value of a key |
| DEL | `DEL <key>` | Delete a key (returns 1 if deleted, 0 if not found) |
| EXISTS | `EXISTS <key>` | Check if a key exists (returns 1 or 0) |
| KEYS | `KEYS` | List all keys (returns array of strings) |
| TYPE | `TYPE <key>` | Get the type of a key ("string", "zset", or "none") |

#### Numeric Operations
| Command | Syntax | Description |
|---------|--------|-------------|
| INCR | `INCR <key>` | Increment integer value by 1 (creates key with value 1 if absent) |
| DECR | `DECR <key>` | Decrement integer value by 1 (creates key with value -1 if absent) |

#### TTL Operations
| Command | Syntax | Description |
|---------|--------|-------------|
| PEXPIRE | `PEXPIRE <key> <ttl_ms>` | Set expiration time in milliseconds (returns 1 or 0) |
| PTTL | `PTTL <key>` | Get remaining TTL in ms (-1: no TTL, -2: key not found) |

#### Sorted Set Operations
| Command | Syntax | Description |
|---------|--------|-------------|
| ZADD | `ZADD <zset> <score> <name>` | Add member to sorted set (returns 1 for new, 0 for update) |
| ZREM | `ZREM <zset> <name>` | Remove member from sorted set (returns 1 or 0) |
| ZSCORE | `ZSCORE <zset> <name>` | Get score of a member (returns double or nil) |
| ZQUERY | `ZQUERY <zset> <score> <name> <offset> <limit>` | Query members with pagination (returns flat array of name/score pairs) |

#### Server Commands
| Command | Syntax | Description |
|---------|--------|-------------|
| PING | `PING` | Returns "PONG" |
| ECHO | `ECHO <message>` | Returns the message back |
| FLUSHDB | `FLUSHDB` | Delete all keys and clear all data structures |

### Protocol Data Types (Response Tags)

| Tag | Value | Description |
|-----|-------|-------------|
| NIL | 0 | Null/nil value (1 byte) |
| ERR | 1 | Error: [code:4][len:4][msg:...] |
| STR | 2 | String: [len:4][data:...] |
| INT | 3 | 64-bit signed integer (8 bytes) |
| DBL | 4 | 64-bit double (8 bytes) |
| ARR | 5 | Array: [count:4][items...] |

### Error Codes

| Code | Name | Description |
|------|------|-------------|
| 1 | ERR_UNKNOWN | Unknown command |
| 2 | ERR_TOO_BIG | Response too big |
| 3 | ERR_BAD_TYP | Unexpected value type |
| 4 | ERR_BAD_ARG | Bad argument |

## Test Suite

The project includes a comprehensive test client (`test_client.cpp`) that tests all server functionality:

### Test Coverage (50 tests)
- **Basic commands** (3 tests): PING, ECHO, ECHO empty string
- **Key-value operations** (12 tests): SET/GET, DEL, EXISTS, TYPE, KEYS
- **Numeric operations** (6 tests): INCR, DECR, error handling
- **TTL operations** (6 tests): PEXPIRE, PTTL, TTL expiration
- **Sorted set operations** (14 tests): ZADD, ZSCORE, ZREM, ZQUERY with pagination/offset/limit, type errors
- **Database operations** (3 tests): FLUSHDB, data clearing
- **Error handling** (2 tests): Unknown commands, argument validation
- **Advanced cases** (4 tests): Large values (10KB), binary data with null bytes, empty keys/values

```bash
make test
```

Expected output:
```
╔══════════════════════════════════════════════════════╗
║   Redis-like Server — Comprehensive Test Suite      ║
╚══════════════════════════════════════════════════════╝
...
║  RESULTS:  50 total,  50 passed,   0 failed       ║
║         ✓ ALL TESTS PASSED!                        ║
╚══════════════════════════════════════════════════════╝
```

## Project Structure

```
redis_server/
├── README.md                    # This file
├── redis /                      # Main source code directory
│   ├── 14_server.cpp            # Main server implementation
│   ├── test_client.cpp          # Comprehensive test client
│   ├── Makefile                 # Build system
│   ├── common.h                 # Hash functions and container_of macro
│   ├── avl.cpp / avl.h          # AVL tree implementation
│   ├── hashtable.cpp / hashtable.h # Hash table with progressive rehashing
│   ├── zset.cpp / zset.h        # Sorted set (AVL tree + hash table)
│   ├── heap.cpp / heap.h        # Min-heap for TTL management
│   ├── list.h                   # Intrusive doubly-linked list
│   └── thread_pool.cpp / thread_pool.h # Thread pool for async deletions
├── basics_compNetwork.txt       # Notes on computer networking basics
├── server.txt                   # Server and networking concepts
├── socket_notes.txt             # Socket programming notes
└── tech.txt                     # Technology overview and computer engineering concepts
```

## Notes

- This is a learning implementation, not a production-ready Redis clone
- The server uses a simple custom binary protocol, not the official Redis protocol (RESP)
- Connection idle timeout is set to 5 seconds
- The server supports up to 200,000 arguments per request (safety limit)
- Large data structures (>1000 members) are deleted asynchronously using the thread pool
- The `container_of` macro uses `__typeof__` for GCC/Clang portability

## Future Improvements

- More Redis commands (MGET/MSET, LPUSH/RPUSH/LPOP/RPOP, HSET/HGET, SADD/SMEMBERS)
- Persistence (RDB/AOF)
- Clustering and replication support
- Official Redis protocol (RESP) compatibility
- Configuration file support
- Better error handling and logging
- Lua scripting
