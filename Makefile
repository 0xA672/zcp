# zcp — ZON package manager for Zig
# Build system for the ZON lexer & smoke tests

CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -g -O0
LDFLAGS  :=
SRC      := src/lexer.cpp
TEST_SRC := tests/smoke.cpp
TEST_BIN := tests/smoke

.PHONY: all test clean

all: test

$(TEST_BIN): $(TEST_SRC) $(SRC) include/lexer.hpp
	$(CXX) $(CXXFLAGS) -I include $(TEST_SRC) $(SRC) $(LDFLAGS) -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_BIN)
