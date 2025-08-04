# Makefile for Process Pool Library
# Compatible with Linux 2.6.27 and earlier

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200112L
DEBUG_FLAGS = -g -DDEBUG
RELEASE_FLAGS = -O2 -DNDEBUG
LDFLAGS = 

# Source files
SRCS = process_pool.c utils.c worker.c
OBJS = $(SRCS:.c=.o)
HEADERS = process_pool.h

# Target executables
LIB_TARGET = libprocesspool.a
EXAMPLE_TARGET = example
TEST_TARGET = test_pool

# Default target
all: $(LIB_TARGET) $(EXAMPLE_TARGET)

# Debug build
debug: CFLAGS += $(DEBUG_FLAGS)
debug: all

# Release build
release: CFLAGS += $(RELEASE_FLAGS)
release: all

# Static library
$(LIB_TARGET): $(OBJS)
	@echo "Creating static library $@"
	ar rcs $@ $^
	@echo "Library created successfully"

# Example executable
$(EXAMPLE_TARGET): example.o $(LIB_TARGET)
	@echo "Linking $@"
	$(CC) $(CFLAGS) -o $@ example.o -L. -lprocesspool $(LDFLAGS)
	@echo "Example executable created"

# Test executable
$(TEST_TARGET): test.o $(LIB_TARGET)
	@echo "Linking $@"
	$(CC) $(CFLAGS) -o $@ test.o -L. -lprocesspool $(LDFLAGS)
	@echo "Test executable created"

# Object files
%.o: %.c $(HEADERS)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts"
	rm -f *.o $(LIB_TARGET) $(EXAMPLE_TARGET) $(TEST_TARGET)
	rm -f core core.*
	@echo "Clean completed"

# Install library and headers (optional)
install: $(LIB_TARGET)
	@echo "Installing library and headers"
	mkdir -p /usr/local/lib
	mkdir -p /usr/local/include
	cp $(LIB_TARGET) /usr/local/lib/
	cp $(HEADERS) /usr/local/include/
	@echo "Installation completed"

# Uninstall
uninstall:
	@echo "Uninstalling library and headers"
	rm -f /usr/local/lib/$(LIB_TARGET)
	rm -f /usr/local/include/process_pool.h
	@echo "Uninstallation completed"

# Run example
run-example: $(EXAMPLE_TARGET)
	@echo "Running example program"
	./$(EXAMPLE_TARGET)

# Run tests
test: $(TEST_TARGET)
	@echo "Running tests"
	./$(TEST_TARGET)

# Check for memory leaks with valgrind (if available)
valgrind-example: $(EXAMPLE_TARGET)
	@echo "Running example with valgrind"
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(EXAMPLE_TARGET)

valgrind-test: $(TEST_TARGET)
	@echo "Running tests with valgrind"
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TEST_TARGET)

# Static analysis with cppcheck (if available)
cppcheck:
	@echo "Running static analysis"
	cppcheck --enable=all --std=c99 --platform=unix64 $(SRCS) $(HEADERS)

# Format code (if clang-format is available)
format:
	@echo "Formatting code"
	clang-format -i $(SRCS) $(HEADERS) example.c

# Show help
help:
	@echo "Available targets:"
	@echo "  all          - Build library and example (default)"
	@echo "  debug        - Build with debug flags"
	@echo "  release      - Build with optimization flags"
	@echo "  clean        - Remove build artifacts"
	@echo "  install      - Install library and headers to /usr/local"
	@echo "  uninstall    - Remove installed files"
	@echo "  run-example  - Build and run example program"
	@echo "  test         - Build and run tests"
	@echo "  valgrind-*   - Run with memory leak detection"
	@echo "  cppcheck     - Run static analysis"
	@echo "  format       - Format source code"
	@echo "  help         - Show this help message"

# Phony targets
.PHONY: all debug release clean install uninstall run-example test valgrind-example valgrind-test cppcheck format help

# Dependency tracking
-include $(OBJS:.o=.d)

# Generate dependency files
%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$