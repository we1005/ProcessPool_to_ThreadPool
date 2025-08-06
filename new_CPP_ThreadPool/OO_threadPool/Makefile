# 面向对象的线程池 Makefile

CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++14 -pthread -O2
DEBUG_FLAGS = -g -DDEBUG
TARGET = test_threadpool
SOURCES = ThreadPool.cpp test.cpp
OBJECTS = $(SOURCES:.cpp=.o)
HEADERS = ThreadPool.h

# 默认目标
all: $(TARGET)

# 编译可执行文件
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# 编译目标文件
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 调试版本
debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: clean $(TARGET)

# C++17版本
cpp17: CXXFLAGS += -std=c++17
cpp17: clean $(TARGET)

# C++20版本
cpp20: CXXFLAGS += -std=c++20
cpp20: clean $(TARGET)

# 运行测试
test: $(TARGET)
	./$(TARGET)

# 性能分析版本
profile: CXXFLAGS += -pg
profile: clean $(TARGET)

# 优化版本
optimized: CXXFLAGS += -O3 -DNDEBUG -march=native
optimized: clean $(TARGET)

# 静态库
libthreadpool.a: ThreadPool.o
	ar rcs $@ $^

# 动态库
libthreadpool.so: ThreadPool.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $<

# 安装头文件和库
install: libthreadpool.a libthreadpool.so
	sudo cp ThreadPool.h /usr/local/include/
	sudo cp libthreadpool.a /usr/local/lib/
	sudo cp libthreadpool.so /usr/local/lib/
	sudo ldconfig

# 清理
clean:
	rm -f $(OBJECTS) $(TARGET) libthreadpool.a libthreadpool.so gmon.out

# 代码格式化
format:
	clang-format -i *.cpp *.h

# 静态分析
analyze:
	clang-tidy *.cpp -- $(CXXFLAGS)

# 内存检查
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# 线程检查
helgrind: $(TARGET)
	valgrind --tool=helgrind ./$(TARGET)

# 代码覆盖率
coverage: CXXFLAGS += --coverage
coverage: clean $(TARGET)
	./$(TARGET)
	gcov $(SOURCES)
	lcov --capture --directory . --output-file coverage.info
	genhtml coverage.info --output-directory coverage_html

# 基准测试
benchmark: optimized
	time ./$(TARGET)

# 代码统计
stats:
	@echo "=== 代码统计 ==="
	@echo "头文件行数:"
	@wc -l *.h
	@echo "源文件行数:"
	@wc -l *.cpp
	@echo "总行数:"
	@cat *.h *.cpp | wc -l
	@echo "类数量:"
	@grep -c "^class " *.h
	@echo "成员函数数量:"
	@grep -c "^[[:space:]]*[a-zA-Z_][a-zA-Z0-9_]*.*(.*).*[;{]" *.h *.cpp
	@echo "模板函数数量:"
	@grep -c "^[[:space:]]*template" *.h *.cpp

# 依赖关系检查
deps:
	@echo "=== 依赖关系 ==="
	@echo "包含的标准库头文件:"
	@grep "#include <" *.h *.cpp | sort | uniq
	@echo "包含的自定义头文件:"
	@grep "#include \"" *.h *.cpp | sort | uniq

# 编译器版本信息
compiler-info:
	@echo "=== 编译器信息 ==="
	@$(CXX) --version
	@echo "支持的C++标准:"
	@echo "C++11: " && $(CXX) -std=c++11 -E -x c++ /dev/null > /dev/null 2>&1 && echo "✓" || echo "✗"
	@echo "C++14: " && $(CXX) -std=c++14 -E -x c++ /dev/null > /dev/null 2>&1 && echo "✓" || echo "✗"
	@echo "C++17: " && $(CXX) -std=c++17 -E -x c++ /dev/null > /dev/null 2>&1 && echo "✓" || echo "✗"
	@echo "C++20: " && $(CXX) -std=c++20 -E -x c++ /dev/null > /dev/null 2>&1 && echo "✓" || echo "✗"

# 帮助信息
help:
	@echo "可用目标:"
	@echo "  all        - 编译程序 (默认)"
	@echo "  debug      - 编译调试版本"
	@echo "  cpp17      - 使用C++17标准编译"
	@echo "  cpp20      - 使用C++20标准编译"
	@echo "  test       - 运行测试"
	@echo "  profile    - 编译性能分析版本"
	@echo "  optimized  - 编译优化版本"
	@echo "  libthreadpool.a  - 编译静态库"
	@echo "  libthreadpool.so - 编译动态库"
	@echo "  install    - 安装库和头文件"
	@echo "  clean      - 清理编译文件"
	@echo "  format     - 格式化代码"
	@echo "  analyze    - 静态代码分析"
	@echo "  valgrind   - 内存检查"
	@echo "  helgrind   - 线程检查"
	@echo "  coverage   - 代码覆盖率分析"
	@echo "  benchmark  - 基准测试"
	@echo "  stats      - 代码统计"
	@echo "  deps       - 依赖关系检查"
	@echo "  compiler-info - 编译器信息"
	@echo "  help       - 显示此帮助信息"

.PHONY: all debug cpp17 cpp20 test profile optimized install clean format analyze valgrind helgrind coverage benchmark stats deps compiler-info help