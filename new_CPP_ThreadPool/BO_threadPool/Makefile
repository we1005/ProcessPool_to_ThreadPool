# 基于对象的线程池 Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -O2
DEBUG_FLAGS = -g -DDEBUG
TARGET = test_threadpool
SOURCES = threadpool.c test.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = threadpool.h

# 默认目标
all: $(TARGET)

# 编译可执行文件
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

# 编译目标文件
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# 调试版本
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean $(TARGET)

# 运行测试
test: $(TARGET)
	./$(TARGET)

# 性能分析版本
profile: CFLAGS += -pg
profile: clean $(TARGET)

# 静态库
libthreadpool.a: threadpool.o
	ar rcs $@ $^

# 动态库
libthreadpool.so: threadpool.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<

# 安装头文件和库
install: libthreadpool.a libthreadpool.so
	sudo cp threadpool.h /usr/local/include/
	sudo cp libthreadpool.a /usr/local/lib/
	sudo cp libthreadpool.so /usr/local/lib/
	sudo ldconfig

# 清理
clean:
	rm -f $(OBJECTS) $(TARGET) libthreadpool.a libthreadpool.so gmon.out

# 代码格式化
format:
	indent -linux *.c *.h

# 内存检查
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# 代码统计
stats:
	@echo "=== 代码统计 ==="
	@echo "头文件行数:"
	@wc -l *.h
	@echo "源文件行数:"
	@wc -l *.c
	@echo "总行数:"
	@cat *.h *.c | wc -l
	@echo "函数数量:"
	@grep -c "^[a-zA-Z_][a-zA-Z0-9_]*.*(.*).*{" *.c

# 帮助信息
help:
	@echo "可用目标:"
	@echo "  all      - 编译程序 (默认)"
	@echo "  debug    - 编译调试版本"
	@echo "  test     - 运行测试"
	@echo "  profile  - 编译性能分析版本"
	@echo "  libthreadpool.a  - 编译静态库"
	@echo "  libthreadpool.so - 编译动态库"
	@echo "  install  - 安装库和头文件"
	@echo "  clean    - 清理编译文件"
	@echo "  format   - 格式化代码"
	@echo "  valgrind - 内存检查"
	@echo "  stats    - 代码统计"
	@echo "  help     - 显示此帮助信息"

.PHONY: all debug test profile install clean format valgrind stats help