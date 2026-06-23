# Redis-like Server Implementation

A custom C++ implementation of an in-memory key-value store inspired by Redis. Built from scratch with custom data structures — no external dependencies beyond the C++ standard library and POSIX APIs.

## Features

### Data Types
- **Strings**: GET, SET, DEL, EXISTS, INCR, DECR
- **Lists**: LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE (with negative index support)
- **Hashes**: HSET, HGET, HDEL, HGETALL, HEXISTS
- **Sorted Sets**: ZADD, ZREM, ZSCORE, ZQUERY (with pagination, offset, limit)
- **TTL**: PEXPIRE, PTTL with automatic key expiration via min-heap

### Persistence
- **AOF (Append-Only File)**: All write commands are logged to `server.aof`
- **Crash recovery**: On startup, AOF log is replayed to restore state
- **Graceful shutdown**: SIGTERM/SIGINT triggers clean shutdown with AOF fsync

### Server Features
- **Event-driven I/O**: `poll()`-based event loop with non-blocking sockets
- **Request pipelining**: Multiple requests per connection
- **Thread pool**: Asynchronous deletion of large data structures
- **Connection management**: Idle timeout (5s), connection pooling
- **Statistics**: INFO command with uptime, request count, key count, connections
- **Binary protocol**: Efficient tagged serialization (NIL, ERR, STR, INT, DBL, ARR)

### Custom Data Structures
- **AVL Tree** (`avl.cpp/h`): Self-balancing binary search tree with rank queries
- **Hash Table** (`hashtable.cpp/h`): Progressive rehashing with load-factor triggering
- **Min-Heap** (`heap.cpp/h`): TTL expiration tracking
- **Intrusive Doubly-Linked List** (`list.h`): Connection idle timer management
- **Thread Pool** (`thread_pool.cpp/h`): Background deletion of large containers

## Quick Start

### Build
```bash
cd "redis "
make
```

### Run Server
```bash
./redis /server
# Server listening on port 1234
```

### Run Tests
```bash
cd "redis "
make test
```
Output:
```
╔══════════════════════════════════════════════════════╗
║   Redis-like Server — Comprehensive Test Suite      ║
╚══════════════════════════════════════════════════════╝
...
║  RESULTS:  82 total,  82 passed,   0 failed       ║
║         ✓ ALL TESTS PASSED!                        ║
╚══════════════════════════════════════════════════════╝
```

### Manual Compilation
```bash
g++ -std=c++11 -pthread 14_server.cpp avl.cpp hashtable.cpp heap.cpp zset.cpp thread_pool.cpp -o server
g++ -std=c++11 -pthread test_client.cpp -o test_client
```

## Command Reference

### String Commands
| Command | Syntax | Returns |
|---------|--------|---------|
| GET | `GET <key>` | String value or nil |
| SET | `SET <key> <value>` | nil |
| DEL | `DEL <key>` | 1 if deleted, 0 if not found |
| EXISTS | `EXISTS <key>` | 1 or 0 |
| INCR | `INCR <key>` | New integer value (auto-creates with 1) |
| DECR | `DECR <key>` | New integer value (auto-creates with -1) |
| TYPE | `TYPE <key>` | "string", "list", "hash", "zset", or "none" |
| KEYS | `KEYS` | Array of all key names |

### List Commands
| Command | Syntax | Returns |
|---------|--------|---------|
| LPUSH | `LPUSH <key> <value>` | New list length |
| RPUSH | `RPUSH <key> <value>` | New list length |
| LPOP | `LPOP <key>` | First element or nil |
| RPOP | `RPOP <key>` | Last element or nil |
| LLEN | `LLEN <key>` | List length (0 if not found) |
| LRANGE | `LRANGE <key> <start> <stop>` | Array of elements (supports negative indices) |

### Hash Commands
| Command | Syntax | Returns |
|---------|--------|---------|
| HSET | `HSET <key> <field> <value>` | 1 if new field, 0 if updated |
| HGET | `HGET <key> <field>` | Field value or nil |
| HDEL | `HDEL <key> <field>` | 1 if deleted, 0 if not found |
| HEXISTS | `HEXISTS <key> <field>` | 1 or 0 |
| HGETALL | `HGETALL <key>` | Array of [field1, val1, field2, val2, ...] |

### Sorted Set Commands
| Command | Syntax | Returns |
|---------|--------|---------|
| ZADD | `ZADD <key> <score> <member>` | 1 if new, 0 if updated |
| ZREM | `ZREM <key> <member>` | 1 if removed, 0 if not found |
| ZSCORE | `ZSCORE <key> <member>` | Score (double) or nil |
| ZQUERY | `ZQUERY <key> <score> <name> <offset> <limit>` | Flat array of [name1, score1, name2, score2, ...] |

### TTL Commands
| Command | Syntax | Returns |
|---------|--------|---------|
| PEXPIRE | `PEXPIRE <key> <ttl_ms>` | 1 if set, 0 if key not found |
| PTTL | `PTTL <key>` | Remaining ms, -1 if no TTL, -2 if key not found |

### Server Commands
| Command | Syntax | Returns |
|---------|--------|---------|
| PING | `PING` | "PONG" |
| ECHO | `ECHO <message>` | The message |
| INFO | `INFO` | Server statistics |
| FLUSHDB | `FLUSHDB` | "OK" (deletes all keys) |

## Binary Protocol

### Request Format
```
[4-byte total_length][4-byte nstr][4-byte len1][str1]...[4-byte lenN][strN]
```
All integers are little-endian.

### Response Format
```
[4-byte length][1-byte tag][payload...]
```

| Tag | Value | Payload |
|-----|-------|---------|
| NIL | 0 | (none) |
| ERR | 1 | [4-byte code][4-byte msg_len][msg] |
| STR | 2 | [4-byte len][data] |
| INT | 3 | [8-byte int64] |
| DBL | 4 | [8-byte double] |
| ARR | 5 | [4-byte count][items...] |

### Error Codes
| Code | Name | Description |
|------|------|-------------|
| 1 | ERR_UNKNOWN | Unknown command |
| 2 | ERR_TOO_BIG | Response exceeds max size |
| 3 | ERR_BAD_TYP | Wrong value type for operation |
| 4 | ERR_BAD_ARG | Invalid argument |

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  Event Loop                      │
│  poll() → accept/read/write → process_timers()   │
├─────────────────────────────────────────────────┤
│  Command Parser    │  Response Serializer        │
│  (binary protocol) │  (tagged types)             │
├─────────────────────────────────────────────────┤
│              In-Memory Database                  │
│  ┌──────────┐  ┌──────┐  ┌───────┐  ┌────────┐ │
│  │  String  │  │ List │  │ Hash  │  │ ZSet   │ │
│  │  (str)   │  │(deque)│  │ (HMap)│  │(AVL+HM)│ │
│  └──────────┘  └──────┘  └───────┘  └────────┘ │
│         All indexed by top-level HMap            │
├─────────────────────────────────────────────────┤
│  TTL Heap    │  Idle List   │  Thread Pool       │
│  (expiry)    │  (timeouts)  │  (async del)       │
├─────────────────────────────────────────────────┤
│              AOF Persistence                     │
│  Write commands → server.aof → replay on boot    │
└─────────────────────────────────────────────────┘
```

## Project Structure

```
redis_server/
├── README.md
├── redis /                      # Source directory
│   ├── 14_server.cpp            # Server: event loop, commands, persistence
│   ├── test_client.cpp          # 82-test comprehensive test suite
│   ├── Makefile                 # Build system (make, make test, make clean)
│   ├── common.h                 # FNV hash, container_of macro
│   ├── avl.cpp / avl.h          # AVL tree (insert, delete, rank queries)
│   ├── hashtable.cpp / hashtable.h # Progressive-rehashing hash table
│   ├── zset.cpp / zset.h        # Sorted set (AVL tree + hash table)
│   ├── heap.cpp / heap.h        # Min-heap (TTL expiration)
│   ├── list.h                   # Intrusive doubly-linked list
│   └── thread_pool.cpp / thread_pool.h # Thread pool for async operations
├── basics_compNetwork.txt
├── server.txt
├── socket_notes.txt
└── tech.txt
```

## Test Coverage (82 tests)

- **Basic commands** (3): PING, ECHO, ECHO empty
- **Key-Value** (14): SET/GET, DEL, EXISTS, TYPE (all 4 types), KEYS
- **Numeric** (6): INCR, DECR, edge cases, type errors
- **TTL** (6): PEXPIRE, PTTL, automatic expiration
- **Sorted Sets** (14): ZADD, ZSCORE, ZREM, ZQUERY (pagination/offset/limit), errors
- **Lists** (14): LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE (range/negative), errors
- **Hashes** (15): HSET, HGET, HDEL, HGETALL, HEXISTS, errors
- **Database** (3): FLUSHDB, key clearing
- **Server** (1): INFO statistics
- **Error handling** (2): Unknown commands, argument validation
- **Advanced** (4): Large values (10KB), binary data, empty keys/values

## Notes

- The server uses a custom binary protocol, not Redis RESP
- Connection idle timeout: 5 seconds
- Max arguments per request: 200,000
- Large data structures (>1000 members) are deleted asynchronously
- `container_of` macro uses `__typeof__` for GCC/Clang portability
- AOF log is replayed on startup for crash recovery
