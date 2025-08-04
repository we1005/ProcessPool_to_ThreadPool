# C语言进程池实现

一个基于C语言和Linux系统的高性能进程池实现，采用Master-Worker架构，兼容Linux 2.6.27及更早版本。

## 特性

- ✅ **高性能**: 进程复用、非阻塞I/O、高效任务分发
- ✅ **高可靠性**: 完善错误处理、进程监控、优雅关闭
- ✅ **易用性**: 简洁API、丰富示例、详细文档
- ✅ **兼容性**: 严格使用Linux 2.6.27前特性
- ✅ **工业级**: 遵循最佳实践、完整测试覆盖

## 快速开始

### 编译

```bash
make                # 编译库和示例
make debug          # 调试版本
make release        # 发布版本
make clean          # 清理
```

### 运行示例

```bash
make run-example    # 运行示例程序
make test           # 运行单元测试
```

### 基本用法

```c
#include "process_pool.h"

int main() {
    // 创建进程池，4个工作进程
    process_pool_t* pool = process_pool_create(4, NULL);
    
    // 启动进程池
    pid_t runner = fork();
    if (runner == 0) {
        process_pool_run(pool);
        exit(0);
    }
    
    // 提交任务
    int task_id = process_pool_submit_task(pool, "hello", 5);
    
    // 获取结果
    task_result_t result;
    if (process_pool_get_result(pool, &result, 5000) == 0) {
        printf("Result: %.*s\n", result.result_len, result.result_data);
    }
    
    // 清理
    process_pool_stop(pool);
    kill(runner, SIGTERM);
    waitpid(runner, NULL, 0);
    process_pool_destroy(pool);
    
    return 0;
}
```

## 架构设计

### 核心组件

1. **主进程 (Master)**: 管理工作进程生命周期，任务分发和结果收集
2. **工作进程 (Worker)**: 执行具体任务处理
3. **进程间通信 (IPC)**: 使用pipe()和select()实现高效通信
4. **任务队列**: 环形缓冲区实现的任务存储

### 文件结构

```
ProcessPool/
├── process_pool.h      # 头文件 - 接口定义和数据结构
├── process_pool.c      # 主模块 - 进程池核心功能
├── worker.c           # 工作进程模块
├── utils.c            # 工具函数模块
├── example.c          # 使用示例
├── test.c             # 单元测试
├── Makefile           # 编译脚本
└── README.md          # 项目说明
```

## API接口

### 核心函数

- `process_pool_create()` - 创建进程池
- `process_pool_submit_task()` - 提交任务
- `process_pool_get_result()` - 获取结果
- `process_pool_run()` - 运行主循环
- `process_pool_destroy()` - 销毁进程池

### 自定义任务处理

```c
int my_task_handler(const char* task_data, int data_len, 
                   char* result_data, int* result_len) {
    // 自定义任务处理逻辑
    // ...
    return 0;
}

process_pool_t* pool = process_pool_create(4, my_task_handler);
```

## 技术特性

### Linux 2.6.27兼容性

使用的系统调用：
- `fork()` - 创建子进程
- `pipe()` - 创建管道
- `select()` - I/O多路复用
- `sigaction()` - 信号处理
- `waitpid()` - 进程等待
- `kill()` - 发送信号
- `fcntl()` - 文件控制

避免使用的新特性：
- `epoll` (Linux 2.6+)
- `signalfd` (Linux 2.6.22+)
- `eventfd` (Linux 2.6.22+)
- `timerfd` (Linux 2.6.25+)

### 工业最佳实践

1. **错误处理**: 所有系统调用都检查返回值
2. **资源管理**: 自动关闭文件描述符，防止内存泄漏
3. **信号安全**: 使用SA_RESTART标志，避免竞态条件
4. **性能优化**: 非阻塞I/O，进程复用，高效任务分发

## 配置参数

```c
#define MAX_WORKERS 32          // 最大工作进程数
#define MAX_TASK_QUEUE 1024     // 任务队列最大长度
#define PIPE_BUF_SIZE 4096      // 管道缓冲区大小
#define MAX_TASK_DATA 1024      // 任务数据最大长度
```

## 性能特性

- **任务吞吐量**: 取决于任务复杂度和工作进程数
- **延迟**: 通常在毫秒级别
- **内存占用**: 基础占用约几MB，随工作进程数线性增长
- **CPU占用**: 主进程CPU占用很低，主要开销在工作进程

## 代码统计

| 文件 | 行数 | 功能描述 |
|------|------|----------|
| process_pool.h | ~200 | 接口定义和数据结构 |
| process_pool.c | ~400 | 进程池核心功能 |
| worker.c | ~300 | 工作进程实现 |
| utils.c | ~250 | 工具函数 |
| example.c | ~350 | 使用示例 |
| test.c | ~400 | 单元测试 |
| **总计** | **~1900** | **完整实现** |

## 扩展性

1. **任务处理函数**: 支持自定义任务处理逻辑
2. **通信协议**: 可扩展任务和结果数据格式
3. **调度策略**: 可实现不同的任务分发算法
4. **监控接口**: 可添加更多状态监控功能

## 许可证

本项目采用MIT许可证，可自由使用、修改和分发。

## 贡献

欢迎提交Issue和Pull Request来改进这个项目。

---

**注意**: 这是一个教学和参考实现，展示了如何使用传统的Linux系统调用构建高性能的进程池。在生产环境中使用时，请根据具体需求进行适当的测试和优化。