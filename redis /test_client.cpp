// Comprehensive test client for the Redis-like server
// Tests all commands via the binary protocol
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

// ─── Protocol constants (mirrors server) ──────────────────────────────────
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

enum {
    ERR_UNKNOWN = 1,
    ERR_TOO_BIG = 2,
    ERR_BAD_TYP = 3,
    ERR_BAD_ARG = 4,
};

// ─── Buffer helpers ────────────────────────────────────────────────────────
typedef std::vector<uint8_t> Buffer;

static void buf_append(Buffer &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void append_u32(Buffer &buf, uint32_t val) {
    buf_append(buf, (const uint8_t *)&val, 4);
}

static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
    if (cur + 4 > end) return false;
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

// ─── Request builder ───────────────────────────────────────────────────────
static Buffer build_request(const std::vector<std::string> &cmd) {
    size_t payload_size = 4;  // nstr
    for (const auto &s : cmd) {
        payload_size += 4 + s.size();
    }
    Buffer buf;
    append_u32(buf, (uint32_t)payload_size);
    append_u32(buf, (uint32_t)cmd.size());
    for (const auto &s : cmd) {
        append_u32(buf, (uint32_t)s.size());
        buf_append(buf, (const uint8_t *)s.data(), s.size());
    }
    return buf;
}

// ─── Response parser ───────────────────────────────────────────────────────
struct ParsedResponse {
    int tag = -1;
    int64_t int_val = 0;
    double  dbl_val = 0.0;
    std::string str_val;
    uint32_t err_code = 0;
    std::vector<ParsedResponse> arr;
    bool is_nil = false;
};

static bool parse_response(const uint8_t *&cur, const uint8_t *end, ParsedResponse &out) {
    if (cur >= end) return false;
    out.tag = *cur++;
    switch (out.tag) {
    case TAG_NIL:
        out.is_nil = true;
        return true;
    case TAG_ERR: {
        uint32_t code = 0, len = 0;
        if (!read_u32(cur, end, code)) return false;
        if (!read_u32(cur, end, len))  return false;
        if (cur + len > end) return false;
        out.err_code = code;
        out.str_val.assign(cur, cur + len);
        cur += len;
        return true;
    }
    case TAG_STR: {
        uint32_t len = 0;
        if (!read_u32(cur, end, len)) return false;
        if (cur + len > end) return false;
        out.str_val.assign(cur, cur + len);
        cur += len;
        return true;
    }
    case TAG_INT:
        if (cur + 8 > end) return false;
        memcpy(&out.int_val, cur, 8);
        cur += 8;
        return true;
    case TAG_DBL:
        if (cur + 8 > end) return false;
        memcpy(&out.dbl_val, cur, 8);
        cur += 8;
        return true;
    case TAG_ARR: {
        uint32_t n = 0;
        if (!read_u32(cur, end, n)) return false;
        out.arr.resize(n);
        for (uint32_t i = 0; i < n; i++) {
            if (!parse_response(cur, end, out.arr[i])) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

// ─── Socket helpers ────────────────────────────────────────────────────────
static int connect_to_server(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    return fd;
}

static bool send_request(int fd, const Buffer &req) {
    size_t sent = 0;
    while (sent < req.size()) {
        ssize_t rv = write(fd, req.data() + sent, req.size() - sent);
        if (rv <= 0) return false;
        sent += (size_t)rv;
    }
    return true;
}

static bool recv_response(int fd, ParsedResponse &out) {
    uint8_t header[4];
    size_t got = 0;
    while (got < 4) {
        ssize_t rv = read(fd, header + got, 4 - got);
        if (rv <= 0) return false;
        got += (size_t)rv;
    }
    uint32_t msg_len = 0;
    memcpy(&msg_len, header, 4);
    if (msg_len > 32 * 1024 * 1024) return false;

    Buffer body(msg_len);
    got = 0;
    while (got < msg_len) {
        ssize_t rv = read(fd, body.data() + got, msg_len - got);
        if (rv <= 0) return false;
        got += (size_t)rv;
    }

    const uint8_t *cur = body.data();
    const uint8_t *end = cur + msg_len;
    return parse_response(cur, end, out);
}

static void close_conn(int fd) {
    close(fd);
}

// ─── Test framework ────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;
static int tests_fail = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        fprintf(stderr, "  TEST %3d: %-55s ", tests_run, name); \
    } while(0)

#define PASS() \
    do { \
        fprintf(stderr, "\033[32m✓ PASS\033[0m\n"); \
        tests_pass++; \
    } while(0)

#define FAIL(msg, ...) \
    do { \
        fprintf(stderr, "\033[31m✗ FAIL\033[0m"); \
        if (*(msg)) fprintf(stderr, " — " msg, ##__VA_ARGS__); \
        fprintf(stderr, "\n"); \
        tests_fail++; \
    } while(0)

// ─── Convenience helpers ───────────────────────────────────────────────────
static bool cmd_expect_int(int fd, const std::vector<std::string> &cmd,
                           int64_t expected) {
    ParsedResponse r;
    if (!send_request(fd, build_request(cmd))) return false;
    if (!recv_response(fd, r)) return false;
    return r.tag == TAG_INT && r.int_val == expected;
}

static bool cmd_expect_str(int fd, const std::vector<std::string> &cmd,
                            const std::string &expected) {
    ParsedResponse r;
    if (!send_request(fd, build_request(cmd))) return false;
    if (!recv_response(fd, r)) return false;
    return r.tag == TAG_STR && r.str_val == expected;
}

static bool cmd_expect_nil(int fd, const std::vector<std::string> &cmd) {
    ParsedResponse r;
    if (!send_request(fd, build_request(cmd))) return false;
    if (!recv_response(fd, r)) return false;
    return r.tag == TAG_NIL;
}

static bool cmd_expect_dbl(int fd, const std::vector<std::string> &cmd,
                            double expected) {
    ParsedResponse r;
    if (!send_request(fd, build_request(cmd))) return false;
    if (!recv_response(fd, r)) return false;
    return r.tag == TAG_DBL && r.dbl_val == expected;
}

static bool cmd_expect_err(int fd, const std::vector<std::string> &cmd,
                            uint32_t expected_code) {
    ParsedResponse r;
    if (!send_request(fd, build_request(cmd))) return false;
    if (!recv_response(fd, r)) return false;
    return r.tag == TAG_ERR && r.err_code == expected_code;
}

static bool cmd_ok(int fd, const std::vector<std::string> &cmd,
                   ParsedResponse &out) {
    if (!send_request(fd, build_request(cmd))) return false;
    if (!recv_response(fd, out)) return false;
    return true;
}

// Fire-and-forget: send a command, discard the response
static void cmd_send(int fd, const std::vector<std::string> &cmd) {
    ParsedResponse dummy;
    cmd_ok(fd, cmd, dummy);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test suites
// ═══════════════════════════════════════════════════════════════════════════

static void test_ping(int fd) {
    TEST("PING returns PONG");
    if (cmd_expect_str(fd, {"ping"}, "PONG")) PASS(); else FAIL("");
}

static void test_echo(int fd) {
    TEST("ECHO returns same string");
    if (cmd_expect_str(fd, {"echo", "hello world"}, "hello world")) PASS();
    else FAIL("");

    TEST("ECHO empty string");
    if (cmd_expect_str(fd, {"echo", ""}, "")) PASS(); else FAIL("");
}

static void test_set_get(int fd) {
    TEST("SET and GET a key");
    ParsedResponse r;
    if (!cmd_ok(fd, {"set", "testkey", "testval"}, r)) { FAIL("send/recv"); return; }
    if (r.tag == TAG_NIL) {
        if (cmd_expect_str(fd, {"get", "testkey"}, "testval")) PASS(); else FAIL("GET mismatch");
    } else {
        FAIL("SET returned non-nil: tag=%d", r.tag);
    }

    TEST("SET overwrites existing key");
    if (!cmd_ok(fd, {"set", "testkey", "newval"}, r)) { FAIL("send/recv"); return; }
    if (r.tag == TAG_NIL) {
        if (cmd_expect_str(fd, {"get", "testkey"}, "newval")) PASS(); else FAIL("overwrite failed");
    } else {
        FAIL("SET returned non-nil");
    }

    TEST("GET non-existent key returns nil");
    if (cmd_expect_nil(fd, {"get", "nonexistent"})) PASS(); else FAIL("");
}

static void test_del(int fd) {
    TEST("DEL existing key returns 1");
    cmd_send(fd, {"set", "delkey", "val"});
    if (cmd_expect_int(fd, {"del", "delkey"}, 1)) PASS(); else FAIL("");

    TEST("DEL non-existent key returns 0");
    if (cmd_expect_int(fd, {"del", "delkey"}, 0)) PASS(); else FAIL("");

    TEST("GET after DEL returns nil");
    if (cmd_expect_nil(fd, {"get", "delkey"})) PASS(); else FAIL("");
}

static void test_exists(int fd) {
    TEST("EXISTS for existing key returns 1");
    cmd_send(fd, {"set", "existkey", "val"});
    if (cmd_expect_int(fd, {"exists", "existkey"}, 1)) PASS(); else FAIL("");

    TEST("EXISTS for non-existing key returns 0");
    if (cmd_expect_int(fd, {"exists", "nonexistent"}, 0)) PASS(); else FAIL("");
}

static void test_type(int fd) {
    TEST("TYPE for string key");
    cmd_send(fd, {"set", "type_str", "hello"});
    if (cmd_expect_str(fd, {"type", "type_str"}, "string")) PASS(); else FAIL("");

    TEST("TYPE for zset key");
    cmd_send(fd, {"zadd", "type_zset", "1.0", "member1"});
    if (cmd_expect_str(fd, {"type", "type_zset"}, "zset")) PASS(); else FAIL("");

    TEST("TYPE for list key");
    cmd_send(fd, {"lpush", "type_list", "item"});
    if (cmd_expect_str(fd, {"type", "type_list"}, "list")) PASS(); else FAIL("");

    TEST("TYPE for hash key");
    cmd_send(fd, {"hset", "type_hash", "f", "v"});
    if (cmd_expect_str(fd, {"type", "type_hash"}, "hash")) PASS(); else FAIL("");

    TEST("TYPE for non-existent key returns 'none'");
    if (cmd_expect_str(fd, {"type", "no_such_key"}, "none")) PASS(); else FAIL("");
}

static void test_keys(int fd) {
    TEST("KEYS returns all keys");
    cmd_send(fd, {"flushdb"});
    cmd_send(fd, {"set", "keys_a", "1"});
    cmd_send(fd, {"set", "keys_b", "2"});
    cmd_send(fd, {"set", "keys_c", "3"});

    ParsedResponse r;
    if (!cmd_ok(fd, {"keys"}, r))  { FAIL("send/recv"); return; }
    if (r.tag != TAG_ARR)          { FAIL("not an array (tag=%d)", r.tag); return; }
    int found = 0;
    for (auto &elem : r.arr) {
        if (elem.tag == TAG_STR &&
            (elem.str_val == "keys_a" || elem.str_val == "keys_b" || elem.str_val == "keys_c"))
            found++;
    }
    if (found == 3) PASS(); else FAIL("found %d/3 keys", found);
}

static void test_incr_decr(int fd) {
    TEST("INCR on non-existent key creates with value 1");
    if (cmd_expect_int(fd, {"incr", "counter"}, 1)) PASS(); else FAIL("");

    TEST("INCR increments existing value");
    if (cmd_expect_int(fd, {"incr", "counter"}, 2)) PASS(); else FAIL("");

    TEST("INCR multiple times");
    for (int i = 0; i < 5; i++) cmd_send(fd, {"incr", "counter"});
    {
        ParsedResponse r;
        cmd_ok(fd, {"get", "counter"}, r);
        if (r.tag == TAG_STR && r.str_val == "7") PASS();
        else FAIL("expected str '7', got tag=%d val='%s'", r.tag, r.str_val.c_str());
    }

    TEST("DECR on non-existent key creates with value -1");
    if (cmd_expect_int(fd, {"decr", "dcounter"}, -1)) PASS(); else FAIL("");

    TEST("DECR decrements existing value");
    if (cmd_expect_int(fd, {"decr", "dcounter"}, -2)) PASS(); else FAIL("");

    TEST("INCR on non-integer value returns error");
    cmd_send(fd, {"set", "notint", "hello"});
    if (cmd_expect_err(fd, {"incr", "notint"}, ERR_BAD_ARG)) PASS(); else FAIL("");
}

static void test_expire_ttl(int fd) {
    TEST("PEXPIRE sets TTL (returns 1 for existing key)");
    cmd_send(fd, {"set", "ttlkey", "value"});
    if (cmd_expect_int(fd, {"pexpire", "ttlkey", "5000"}, 1)) PASS(); else FAIL("");

    TEST("PEXPIRE on non-existent key returns 0");
    if (cmd_expect_int(fd, {"pexpire", "no_such_key", "1000"}, 0)) PASS(); else FAIL("");

    TEST("PTTL returns TTL for key with expiry");
    ParsedResponse r;
    if (!cmd_ok(fd, {"pttl", "ttlkey"}, r)) { FAIL("send/recv"); return; }
    if (r.tag == TAG_INT && r.int_val > 0 && r.int_val <= 5000) PASS();
    else FAIL("expected positive int <= 5000, got tag=%d val=%lld", r.tag, (long long)r.int_val);

    TEST("PTTL on key without TTL returns -1");
    cmd_send(fd, {"set", "nottl", "val"});
    if (cmd_expect_int(fd, {"pttl", "nottl"}, -1)) PASS(); else FAIL("");

    TEST("PTTL on non-existent key returns -2");
    if (cmd_expect_int(fd, {"pttl", "no_such_key"}, -2)) PASS(); else FAIL("");
}

static void test_ttl_expiration(int fd) {
    TEST("TTL expiration — key is deleted after TTL elapses");
    cmd_send(fd, {"set", "expkey", "expval"});
    cmd_send(fd, {"pexpire", "expkey", "10"});  // 10ms TTL

    // Poll until the key expires (up to 2 seconds)
    bool expired = false;
    for (int i = 0; i < 400; i++) {
        ParsedResponse r;
        cmd_ok(fd, {"exists", "expkey"}, r);
        if (r.tag == TAG_INT && r.int_val == 0) {
            expired = true;
            break;
        }
        usleep(5000);  // 5ms
    }
    if (expired) PASS(); else FAIL("key did not expire after 2s");
}

static void test_zset(int fd) {
    // ZADD
    TEST("ZADD new member returns 1");
    if (cmd_expect_int(fd, {"zadd", "zs", "1.5", "alice"}, 1)) PASS(); else FAIL("");

    TEST("ZADD duplicate member returns 0 (score update)");
    if (cmd_expect_int(fd, {"zadd", "zs", "2.0", "alice"}, 0)) PASS(); else FAIL("");

    TEST("ZADD multiple members");
    {
        bool ok = true;
        if (!cmd_expect_int(fd, {"zadd", "zs", "3.0", "bob"}, 1)) ok = false;
        if (!cmd_expect_int(fd, {"zadd", "zs", "0.5", "charlie"}, 1)) ok = false;
        if (ok) PASS(); else FAIL("one of the additions failed");
    }

    // ZSCORE
    TEST("ZSCORE returns correct score");
    if (cmd_expect_dbl(fd, {"zscore", "zs", "alice"}, 2.0)) PASS(); else FAIL("");

    TEST("ZSCORE for non-existent member returns nil");
    if (cmd_expect_nil(fd, {"zscore", "zs", "nobody"})) PASS(); else FAIL("");

    TEST("ZSCORE on non-existent key returns nil");
    if (cmd_expect_nil(fd, {"zscore", "no_zset", "alice"})) PASS(); else FAIL("");

    // ZREM
    TEST("ZREM existing member returns 1");
    if (cmd_expect_int(fd, {"zrem", "zs", "charlie"}, 1)) PASS(); else FAIL("");

    TEST("ZREM non-existent member returns 0");
    if (cmd_expect_int(fd, {"zrem", "zs", "charlie"}, 0)) PASS(); else FAIL("");

    TEST("ZREM on non-existent key returns 0");
    if (cmd_expect_int(fd, {"zrem", "no_such_zset", "alice"}, 0)) PASS(); else FAIL("");

    // ZQUERY — full scan
    TEST("ZQUERY returns members in order with pagination");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"zquery", "zs", "0", "", "0", "10"}, r)) {
            FAIL("send/recv");
        } else if (r.tag != TAG_ARR) {
            FAIL("not an array (tag=%d)", r.tag);
        } else if (r.arr.size() == 4 &&
                   r.arr[0].tag == TAG_STR && r.arr[0].str_val == "alice" &&
                   r.arr[1].tag == TAG_DBL && r.arr[1].dbl_val == 2.0 &&
                   r.arr[2].tag == TAG_STR && r.arr[2].str_val == "bob" &&
                   r.arr[3].tag == TAG_DBL && r.arr[3].dbl_val == 3.0) {
            PASS();
        } else {
            FAIL("unexpected content: size=%zu", r.arr.size());
            for (size_t i = 0; i < r.arr.size(); i++) {
                fprintf(stderr, "        [%zu] tag=%d", i, r.arr[i].tag);
                if (r.arr[i].tag == TAG_STR) fprintf(stderr, " str='%s'", r.arr[i].str_val.c_str());
                if (r.arr[i].tag == TAG_DBL) fprintf(stderr, " dbl=%.1f", r.arr[i].dbl_val);
                fprintf(stderr, "\n");
            }
        }
    }

    // ZQUERY with offset
    TEST("ZQUERY with offset");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"zquery", "zs", "0", "", "1", "10"}, r)) {
            FAIL("send/recv");
        } else if (r.tag != TAG_ARR) {
            FAIL("not an array");
        } else if (r.arr.size() == 2 &&
                   r.arr[0].tag == TAG_STR && r.arr[0].str_val == "bob" &&
                   r.arr[1].tag == TAG_DBL && r.arr[1].dbl_val == 3.0) {
            PASS();
        } else {
            FAIL("size=%zu", r.arr.size());
        }
    }

    // ZQUERY with limit
    TEST("ZQUERY with limit");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"zquery", "zs", "0", "", "0", "1"}, r)) {
            FAIL("send/recv");
        } else if (r.tag != TAG_ARR || r.arr.size() != 2 ||
                   r.arr[0].str_val != "alice") {
            FAIL("size=%zu", r.arr.size());
        } else {
            PASS();
        }
    }

    // ZQUERY on non-existent key
    TEST("ZQUERY on non-existent key returns empty array");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"zquery", "no_zset", "0", "", "0", "10"}, r)) {
            FAIL("send/recv");
        } else if (r.tag == TAG_ARR && r.arr.size() == 0) {
            PASS();
        } else {
            FAIL("tag=%d size=%zu", r.tag, r.arr.size());
        }
    }

    // ZADD on wrong type
    TEST("ZADD on string key returns error");
    cmd_send(fd, {"set", "strkey", "val"});
    if (cmd_expect_err(fd, {"zadd", "strkey", "1.0", "m"}, ERR_BAD_TYP)) PASS();
    else FAIL("");
}

static void test_list(int fd) {
    TEST("LPUSH creates list and returns length");
    if (cmd_expect_int(fd, {"lpush", "mylist", "world"}, 1)) PASS(); else FAIL("");

    TEST("LPUSH prepends and returns new length");
    if (cmd_expect_int(fd, {"lpush", "mylist", "hello"}, 2)) PASS(); else FAIL("");

    TEST("RPUSH appends and returns new length");
    if (cmd_expect_int(fd, {"rpush", "mylist", "!"}, 3)) PASS(); else FAIL("");

    TEST("LRANGE returns correct elements");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"lrange", "mylist", "0", "-1"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_ARR && r.arr.size() == 3 &&
            r.arr[0].tag == TAG_STR && r.arr[0].str_val == "hello" &&
            r.arr[1].tag == TAG_STR && r.arr[1].str_val == "world" &&
            r.arr[2].tag == TAG_STR && r.arr[2].str_val == "!") PASS();
        else FAIL("size=%zu", r.arr.size());
    }

    TEST("LRANGE with range subset");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"lrange", "mylist", "1", "1"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_ARR && r.arr.size() == 1 &&
            r.arr[0].str_val == "world") PASS();
        else FAIL("size=%zu", r.arr.size());
    }

    TEST("LRANGE with negative indices");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"lrange", "mylist", "-2", "-1"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_ARR && r.arr.size() == 2 &&
            r.arr[0].str_val == "world" && r.arr[1].str_val == "!") PASS();
        else FAIL("size=%zu", r.arr.size());
    }

    TEST("LLEN returns correct length");
    if (cmd_expect_int(fd, {"llen", "mylist"}, 3)) PASS(); else FAIL("");

    TEST("LLEN on non-existent key returns 0");
    if (cmd_expect_int(fd, {"llen", "nolist"}, 0)) PASS(); else FAIL("");

    TEST("LPOP removes and returns first element");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"lpop", "mylist"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_STR && r.str_val == "hello") PASS();
        else FAIL("tag=%d val=%s", r.tag, r.str_val.c_str());
    }

    TEST("RPOP removes and returns last element");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"rpop", "mylist"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_STR && r.str_val == "!") PASS();
        else FAIL("tag=%d val=%s", r.tag, r.str_val.c_str());
    }

    TEST("LPOP on non-existent key returns nil");
    if (cmd_expect_nil(fd, {"lpop", "nolist"})) PASS(); else FAIL("");

    TEST("RPOP on non-existent key returns nil");
    if (cmd_expect_nil(fd, {"rpop", "nolist"})) PASS(); else FAIL("");

    TEST("List type mismatch returns error");
    cmd_send(fd, {"set", "strkey", "val"});
    if (cmd_expect_err(fd, {"lpush", "strkey", "x"}, ERR_BAD_TYP)) PASS(); else FAIL("");

    TEST("LRANGE on non-existent key returns empty array");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"lrange", "nolist", "0", "-1"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_ARR && r.arr.size() == 0) PASS();
        else FAIL("size=%zu", r.arr.size());
    }
}

static void test_hash(int fd) {
    TEST("HSET new field returns 1");
    if (cmd_expect_int(fd, {"hset", "myhash", "field1", "val1"}, 1)) PASS(); else FAIL("");

    TEST("HSET existing field returns 0");
    if (cmd_expect_int(fd, {"hset", "myhash", "field1", "val2"}, 0)) PASS(); else FAIL("");

    TEST("HGET returns correct value");
    if (cmd_expect_str(fd, {"hget", "myhash", "field1"}, "val2")) PASS(); else FAIL("");

    TEST("HGET non-existent field returns nil");
    if (cmd_expect_nil(fd, {"hget", "myhash", "nofield"})) PASS(); else FAIL("");

    TEST("HGET non-existent hash returns nil");
    if (cmd_expect_nil(fd, {"hget", "nohash", "field1"})) PASS(); else FAIL("");

    TEST("HEXISTS for existing field returns 1");
    if (cmd_expect_int(fd, {"hexists", "myhash", "field1"}, 1)) PASS(); else FAIL("");

    TEST("HEXISTS for non-existing field returns 0");
    if (cmd_expect_int(fd, {"hexists", "myhash", "nofield"}, 0)) PASS(); else FAIL("");

    TEST("HEXISTS for non-existing hash returns 0");
    if (cmd_expect_int(fd, {"hexists", "nohash", "field1"}, 0)) PASS(); else FAIL("");

    TEST("Multiple HSET fields");
    cmd_send(fd, {"hset", "myhash", "field2", "v2"});
    cmd_send(fd, {"hset", "myhash", "field3", "v3"});
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"hget", "myhash", "field2"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_STR && r.str_val == "v2") PASS();
        else FAIL("tag=%d", r.tag);
    }

    TEST("HGETALL returns all field-value pairs");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"hgetall", "myhash"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_ARR && r.arr.size() >= 6) {  // 3 fields * 2 = 6 elements
            // Verify key-value pairs are interleaved
            int found = 0;
            for (size_t i = 0; i + 1 < r.arr.size(); i += 2) {
                if (r.arr[i].tag == TAG_STR && r.arr[i+1].tag == TAG_STR) {
                    if (r.arr[i].str_val == "field1" && r.arr[i+1].str_val == "val2") found++;
                    if (r.arr[i].str_val == "field2" && r.arr[i+1].str_val == "v2") found++;
                    if (r.arr[i].str_val == "field3" && r.arr[i+1].str_val == "v3") found++;
                }
            }
            if (found == 3) PASS(); else FAIL("found %d/3 pairs", found);
        } else {
            FAIL("tag=%d size=%zu", r.tag, r.arr.size());
        }
    }

    TEST("HGETALL on non-existent hash returns empty array");
    {
        ParsedResponse r;
        if (!cmd_ok(fd, {"hgetall", "nohash"}, r)) { FAIL("send/recv"); return; }
        if (r.tag == TAG_ARR && r.arr.size() == 0) PASS();
        else FAIL("size=%zu", r.arr.size());
    }

    TEST("HDEL existing field returns 1");
    if (cmd_expect_int(fd, {"hdel", "myhash", "field3"}, 1)) PASS(); else FAIL("");

    TEST("HDEL non-existing field returns 0");
    if (cmd_expect_int(fd, {"hdel", "myhash", "field3"}, 0)) PASS(); else FAIL("");

    TEST("HDEL on non-existing hash returns 0");
    if (cmd_expect_int(fd, {"hdel", "nohash", "x"}, 0)) PASS(); else FAIL("");

    TEST("Hash type mismatch returns error");
    cmd_send(fd, {"set", "strkey2", "val"});
    if (cmd_expect_err(fd, {"hset", "strkey2", "f", "v"}, ERR_BAD_TYP)) PASS(); else FAIL("");
}

static void test_info(int fd) {
    TEST("INFO returns server statistics");
    ParsedResponse r;
    if (!cmd_ok(fd, {"info"}, r)) { FAIL("send/recv"); return; }
    if (r.tag == TAG_STR && r.str_val.find("uptime_ms:") != std::string::npos &&
        r.str_val.find("requests_processed:") != std::string::npos &&
        r.str_val.find("total_keys:") != std::string::npos) PASS();
    else FAIL("tag=%d missing expected fields", r.tag);
}

static void test_flushdb(int fd) {
    TEST("FLUSHDB returns OK");
    cmd_send(fd, {"set", "flush1", "val1"});
    cmd_send(fd, {"set", "flush2", "val2"});
    if (cmd_expect_str(fd, {"flushdb"}, "OK")) PASS(); else FAIL("");

    TEST("FLUSHDB clears all keys");
    if (cmd_expect_int(fd, {"exists", "flush1"}, 0)) PASS(); else FAIL("");

    TEST("FLUSHDB clears zsets too");
    cmd_send(fd, {"zadd", "flush_z", "1.0", "m1"});
    cmd_send(fd, {"flushdb"});
    ParsedResponse r;
    if (!cmd_ok(fd, {"keys"}, r)) { FAIL("send/recv"); return; }
    if (r.tag == TAG_ARR && r.arr.size() == 0) PASS(); else FAIL("keys not empty");
}

static void test_error_handling(int fd) {
    TEST("Unknown command returns ERR_UNKNOWN");
    if (cmd_expect_err(fd, {"nonexistent_cmd"}, ERR_UNKNOWN)) PASS(); else FAIL("");

    TEST("GET with wrong number of args returns unknown cmd");
    if (cmd_expect_err(fd, {"get", "a", "b"}, ERR_UNKNOWN)) PASS(); else FAIL("");
}

static void test_large_values(int fd) {
    TEST("SET and GET large value (10KB)");
    std::string large(10 * 1024, 'X');
    for (size_t i = 0; i < large.size(); i++) large[i] = 'A' + (i % 26);
    ParsedResponse r;
    cmd_ok(fd, {"set", "largekey", large}, r);
    if (!cmd_ok(fd, {"get", "largekey"}, r)) { FAIL("send/recv"); return; }
    if (r.tag == TAG_STR && r.str_val == large) PASS(); else FAIL("mismatch");
}

static void test_binary_values(int fd) {
    TEST("SET and GET binary data with null bytes");
    std::string binary = "pre";
    binary.push_back('\0');
    binary += "mid";
    binary.push_back('\0');
    binary += "post";
    ParsedResponse r;
    cmd_ok(fd, {"set", "binkey", binary}, r);
    if (!cmd_ok(fd, {"get", "binkey"}, r)) { FAIL("send/recv"); return; }
    if (r.tag == TAG_STR && r.str_val == binary) PASS();
    else FAIL("binary data corrupted: expected %zu bytes, got %zu",
              binary.size(), r.str_val.size());
}

static void test_edge_cases(int fd) {
    TEST("Empty key name");
    cmd_send(fd, {"set", "", "emptykey"});
    if (cmd_expect_str(fd, {"get", ""}, "emptykey")) PASS(); else FAIL("");

    TEST("Empty value");
    cmd_send(fd, {"set", "emptyval", ""});
    if (cmd_expect_str(fd, {"get", "emptyval"}, "")) PASS(); else FAIL("");
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════
int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int port = 1234;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    fprintf(stderr, "╔══════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║   Redis-like Server — Comprehensive Test Suite      ║\n");
    fprintf(stderr, "╚══════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "Connecting to %s:%d ... ", host, port);

    int fd = connect_to_server(host, port);
    if (fd < 0) {
        fprintf(stderr, "FAILED\n");
        fprintf(stderr, "\nMake sure the server is running:\n");
        fprintf(stderr, "  cd redis && ./server\n\n");
        return 1;
    }
    fprintf(stderr, "OK\n\n");

    // Flush before tests
    cmd_send(fd, {"flushdb"});

    // ── Run all test suites ──────────────────────────────────────────────
    fprintf(stderr, "─── Basic commands ───────────────────────────────────\n");
    test_ping(fd);
    test_echo(fd);

    fprintf(stderr, "\n─── Key-Value operations ─────────────────────────────\n");
    test_set_get(fd);
    test_del(fd);
    test_exists(fd);
    test_type(fd);
    test_keys(fd);

    fprintf(stderr, "\n─── Numeric operations ───────────────────────────────\n");
    test_incr_decr(fd);

    fprintf(stderr, "\n─── TTL operations ───────────────────────────────────\n");
    test_expire_ttl(fd);
    test_ttl_expiration(fd);

    fprintf(stderr, "\n─── Sorted Set operations ────────────────────────────\n");
    test_zset(fd);

    fprintf(stderr, "\n─── List operations ──────────────────────────────────\n");
    test_list(fd);

    fprintf(stderr, "\n─── Hash operations ──────────────────────────────────\n");
    test_hash(fd);

    fprintf(stderr, "\n─── Server info ──────────────────────────────────────\n");
    test_info(fd);

    fprintf(stderr, "\n─── Database operations ──────────────────────────────\n");
    test_flushdb(fd);

    fprintf(stderr, "\n─── Error handling ───────────────────────────────────\n");
    test_error_handling(fd);

    fprintf(stderr, "\n─── Advanced cases ───────────────────────────────────\n");
    test_large_values(fd);
    test_binary_values(fd);
    test_edge_cases(fd);

    close_conn(fd);

    // ── Summary ─────────────────────────────────────────────────────────
    fprintf(stderr, "\n╔══════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║  RESULTS: %3d total, %3d passed, %3d failed",
            tests_run, tests_pass, tests_fail);
    if (tests_fail == 0) {
        fprintf(stderr, "       ║\n");
        fprintf(stderr, "║         ✓ ALL TESTS PASSED!                        ║\n");
    } else {
        for (int i = 0; i < 39; i++) fprintf(stderr, " ");
        fprintf(stderr, "║\n");
        fprintf(stderr, "║         ✗ %d test(s) FAILED                          ║\n", tests_fail);
    }
    fprintf(stderr, "╚══════════════════════════════════════════════════════╝\n");

    return tests_fail > 0 ? 1 : 0;
}
