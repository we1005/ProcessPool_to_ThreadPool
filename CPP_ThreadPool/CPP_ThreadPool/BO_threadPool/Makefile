# Makefile for BO_threadPool

# 编译器设置
CXX = g++
CXXFLAGS = -std=c++11 -Wall -g -pthread

# 目标文件
TARGET = testThreadPool

# 源文件
SOURCES = TaskQueue.cpp ThreadPool.cpp testThreadPool.cpp

# 对象文件
OBJECTS = $(SOURCES:.cpp=.o)

# 默认目标
all: $(TARGET)

# 链接目标文件
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# 编译源文件为对象文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理生成的文件
clean:
	rm -f $(OBJECTS) $(TARGET)

# 重新编译
rebuild: clean all

# 运行程序
run: $(TARGET)
	./$(TARGET)

# 声明伪目标
.PHONY: all clean rebuild run