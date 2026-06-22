# Makefile for Redis-like Server
CXX      := g++
CXXFLAGS := -std=c++11 -pthread

SRCS := 14_server.cpp avl.cpp hashtable.cpp heap.cpp zset.cpp thread_pool.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := server

TEST_SRC := test_client.cpp
TEST_BIN := test_client

.PHONY: all clean test run

all: $(TARGET) $(TEST_BIN)

$(TARGET): $(SRCS) common.h avl.h hashtable.h heap.h list.h thread_pool.h zset.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

$(TEST_BIN): $(TEST_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_SRC)

# Run the server (background), run tests, then stop the server
test: $(TARGET) $(TEST_BIN)
	@echo "Starting server..."
	@./$(TARGET) & SERVER_PID=$$!; \
	sleep 0.5; \
	echo "Running tests..."; \
	./$(TEST_BIN) 127.0.0.1 1234; STATUS=$$?; \
	kill $$SERVER_PID 2>/dev/null || true; \
	wait $$SERVER_PID 2>/dev/null || true; \
	exit $$STATUS

# Build only
build: $(TARGET)

clean:
	rm -f $(TARGET) $(TEST_BIN) $(OBJS)
