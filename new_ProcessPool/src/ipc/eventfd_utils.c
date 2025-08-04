#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/eventfd.h>
#include <fcntl.h>

// ============================================================================
// EventFD创建和管理
// ============================================================================

int create_eventfd(void) {
    int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (efd == -1) {
        return -1;
    }
    
    return efd;
}

int create_eventfd_blocking(void) {
    int efd = eventfd(0, EFD_CLOEXEC);
    if (efd == -1) {
        return -1;
    }
    
    return efd;
}

int create_eventfd_semaphore(void) {
    int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    if (efd == -1) {
        return -1;
    }
    
    return efd;
}

void close_eventfd(int efd) {
    if (efd >= 0) {
        close(efd);
    }
}

// ============================================================================
// EventFD读写操作
// ============================================================================

int eventfd_write_value(int efd, uint64_t value) {
    if (efd < 0 || value == 0) {
        return -1;
    }
    
    ssize_t result = write(efd, &value, sizeof(value));
    if (result == sizeof(value)) {
        return 0;
    } else if (result == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -2; // 非阻塞模式下暂时无法写入
        }
        return -1; // 其他错误
    } else {
        return -1; // 部分写入，不应该发生
    }
}

int eventfd_read_value(int efd, uint64_t* value) {
    if (efd < 0 || !value) {
        return -1;
    }
    
    ssize_t result = read(efd, value, sizeof(*value));
    if (result == sizeof(*value)) {
        return 0;
    } else if (result == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -2; // 非阻塞模式下暂时无法读取
        }
        return -1; // 其他错误
    } else {
        return -1; // 部分读取，不应该发生
    }
}

int eventfd_signal(int efd) {
    return eventfd_write_value(efd, 1);
}

int eventfd_wait(int efd) {
    uint64_t value;
    return eventfd_read_value(efd, &value);
}

int eventfd_try_wait(int efd) {
    uint64_t value;
    int result = eventfd_read_value(efd, &value);
    if (result == -2) {
        return 0; // 没有信号
    }
    return result;
}

// ============================================================================
// EventFD批量操作
// ============================================================================

int eventfd_signal_multiple(int efd, uint64_t count) {
    if (count == 0) {
        return 0;
    }
    
    return eventfd_write_value(efd, count);
}

int eventfd_drain(int efd, uint64_t* total_count) {
    if (efd < 0) {
        return -1;
    }
    
    uint64_t count = 0;
    uint64_t value;
    
    // 持续读取直到没有更多数据
    while (eventfd_read_value(efd, &value) == 0) {
        count += value;
    }
    
    if (total_count) {
        *total_count = count;
    }
    
    return count > 0 ? 0 : -1;
}

// ============================================================================
// EventFD状态查询
// ============================================================================

bool eventfd_is_signaled(int efd) {
    if (efd < 0) {
        return false;
    }
    
    uint64_t value;
    int result = eventfd_read_value(efd, &value);
    
    if (result == 0) {
        // 有信号，需要重新写回去（因为我们只是查询状态）
        eventfd_write_value(efd, value);
        return true;
    }
    
    return false;
}

int eventfd_get_value(int efd, uint64_t* value) {
    if (efd < 0 || !value) {
        return -1;
    }
    
    // 读取当前值
    int result = eventfd_read_value(efd, value);
    
    if (result == 0) {
        // 读取成功，重新写回去
        eventfd_write_value(efd, *value);
        return 0;
    } else if (result == -2) {
        // 没有数据
        *value = 0;
        return 0;
    }
    
    return result;
}

// ============================================================================
// EventFD同步原语
// ============================================================================

// EventFD互斥锁实现
typedef struct {
    int efd;
    bool initialized;
} eventfd_mutex_t;

int eventfd_mutex_init(eventfd_mutex_t* mutex) {
    if (!mutex) {
        return -1;
    }
    
    mutex->efd = create_eventfd_blocking();
    if (mutex->efd == -1) {
        return -1;
    }
    
    // 初始化为解锁状态
    if (eventfd_signal(mutex->efd) != 0) {
        close_eventfd(mutex->efd);
        return -1;
    }
    
    mutex->initialized = true;
    return 0;
}

void eventfd_mutex_destroy(eventfd_mutex_t* mutex) {
    if (mutex && mutex->initialized) {
        close_eventfd(mutex->efd);
        mutex->initialized = false;
    }
}

int eventfd_mutex_lock(eventfd_mutex_t* mutex) {
    if (!mutex || !mutex->initialized) {
        return -1;
    }
    
    uint64_t value;
    return eventfd_read_value(mutex->efd, &value);
}

int eventfd_mutex_trylock(eventfd_mutex_t* mutex) {
    if (!mutex || !mutex->initialized) {
        return -1;
    }
    
    uint64_t value;
    int result = eventfd_read_value(mutex->efd, &value);
    if (result == -2) {
        return -2; // 锁被占用
    }
    return result;
}

int eventfd_mutex_unlock(eventfd_mutex_t* mutex) {
    if (!mutex || !mutex->initialized) {
        return -1;
    }
    
    return eventfd_signal(mutex->efd);
}

// EventFD信号量实现
typedef struct {
    int efd;
    uint32_t initial_count;
    bool initialized;
} eventfd_semaphore_t;

int eventfd_semaphore_init(eventfd_semaphore_t* sem, uint32_t initial_count) {
    if (!sem || initial_count == 0) {
        return -1;
    }
    
    sem->efd = create_eventfd_semaphore();
    if (sem->efd == -1) {
        return -1;
    }
    
    // 设置初始计数
    if (eventfd_write_value(sem->efd, initial_count) != 0) {
        close_eventfd(sem->efd);
        return -1;
    }
    
    sem->initial_count = initial_count;
    sem->initialized = true;
    return 0;
}

void eventfd_semaphore_destroy(eventfd_semaphore_t* sem) {
    if (sem && sem->initialized) {
        close_eventfd(sem->efd);
        sem->initialized = false;
    }
}

int eventfd_semaphore_wait(eventfd_semaphore_t* sem) {
    if (!sem || !sem->initialized) {
        return -1;
    }
    
    uint64_t value;
    return eventfd_read_value(sem->efd, &value);
}

int eventfd_semaphore_trywait(eventfd_semaphore_t* sem) {
    if (!sem || !sem->initialized) {
        return -1;
    }
    
    uint64_t value;
    int result = eventfd_read_value(sem->efd, &value);
    if (result == -2) {
        return -2; // 没有可用资源
    }
    return result;
}

int eventfd_semaphore_post(eventfd_semaphore_t* sem) {
    if (!sem || !sem->initialized) {
        return -1;
    }
    
    return eventfd_signal(sem->efd);
}

int eventfd_semaphore_post_multiple(eventfd_semaphore_t* sem, uint32_t count) {
    if (!sem || !sem->initialized || count == 0) {
        return -1;
    }
    
    return eventfd_write_value(sem->efd, count);
}

// ============================================================================
// EventFD事件通知器
// ============================================================================

typedef struct {
    int efd;
    _Atomic uint64_t event_count;
    bool initialized;
} eventfd_notifier_t;

int eventfd_notifier_init(eventfd_notifier_t* notifier) {
    if (!notifier) {
        return -1;
    }
    
    notifier->efd = create_eventfd();
    if (notifier->efd == -1) {
        return -1;
    }
    
    ATOMIC_STORE(&notifier->event_count, 0);
    notifier->initialized = true;
    return 0;
}

void eventfd_notifier_destroy(eventfd_notifier_t* notifier) {
    if (notifier && notifier->initialized) {
        close_eventfd(notifier->efd);
        notifier->initialized = false;
    }
}

int eventfd_notifier_notify(eventfd_notifier_t* notifier) {
    if (!notifier || !notifier->initialized) {
        return -1;
    }
    
    ATOMIC_ADD(&notifier->event_count, 1);
    return eventfd_signal(notifier->efd);
}

int eventfd_notifier_notify_multiple(eventfd_notifier_t* notifier, uint32_t count) {
    if (!notifier || !notifier->initialized || count == 0) {
        return -1;
    }
    
    ATOMIC_ADD(&notifier->event_count, count);
    return eventfd_write_value(notifier->efd, count);
}

int eventfd_notifier_wait(eventfd_notifier_t* notifier, uint64_t* events_received) {
    if (!notifier || !notifier->initialized) {
        return -1;
    }
    
    uint64_t value;
    int result = eventfd_read_value(notifier->efd, &value);
    
    if (result == 0 && events_received) {
        *events_received = value;
    }
    
    return result;
}

int eventfd_notifier_try_wait(eventfd_notifier_t* notifier, uint64_t* events_received) {
    if (!notifier || !notifier->initialized) {
        return -1;
    }
    
    uint64_t value;
    int result = eventfd_read_value(notifier->efd, &value);
    
    if (result == 0 && events_received) {
        *events_received = value;
    } else if (result == -2 && events_received) {
        *events_received = 0;
    }
    
    return result;
}

uint64_t eventfd_notifier_get_total_events(eventfd_notifier_t* notifier) {
    if (!notifier || !notifier->initialized) {
        return 0;
    }
    
    return ATOMIC_LOAD(&notifier->event_count);
}

void eventfd_notifier_reset_counter(eventfd_notifier_t* notifier) {
    if (notifier && notifier->initialized) {
        ATOMIC_STORE(&notifier->event_count, 0);
    }
}

// ============================================================================
// EventFD工具函数
// ============================================================================

int eventfd_set_nonblocking(int efd, bool nonblocking) {
    if (efd < 0) {
        return -1;
    }
    
    int flags = fcntl(efd, F_GETFL);
    if (flags == -1) {
        return -1;
    }
    
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    return fcntl(efd, F_SETFL, flags);
}

bool eventfd_is_nonblocking(int efd) {
    if (efd < 0) {
        return false;
    }
    
    int flags = fcntl(efd, F_GETFL);
    return (flags != -1) && (flags & O_NONBLOCK);
}

int eventfd_get_flags(int efd) {
    if (efd < 0) {
        return -1;
    }
    
    return fcntl(efd, F_GETFL);
}

// ============================================================================
// EventFD调试和诊断
// ============================================================================

void eventfd_dump_info(int efd, const char* name) {
    if (efd < 0) {
        printf("EventFD %s: Invalid file descriptor\n", name ? name : "Unknown");
        return;
    }
    
    printf("=== EventFD Info: %s ===\n", name ? name : "Unknown");
    printf("File Descriptor: %d\n", efd);
    
    int flags = fcntl(efd, F_GETFL);
    if (flags != -1) {
        printf("Flags: 0x%x\n", flags);
        printf("Non-blocking: %s\n", (flags & O_NONBLOCK) ? "Yes" : "No");
        printf("Close-on-exec: %s\n", (fcntl(efd, F_GETFD) & FD_CLOEXEC) ? "Yes" : "No");
    }
    
    uint64_t value;
    if (eventfd_get_value(efd, &value) == 0) {
        printf("Current Value: %lu\n", value);
        printf("Signaled: %s\n", value > 0 ? "Yes" : "No");
    } else {
        printf("Current Value: Unable to read\n");
    }
    
    printf("========================\n");
}

int eventfd_validate(int efd) {
    if (efd < 0) {
        return -1;
    }
    
    // 尝试获取文件状态
    int flags = fcntl(efd, F_GETFL);
    if (flags == -1) {
        return -1; // 无效的文件描述符
    }
    
    // 尝试非阻塞读取来测试是否为eventfd
    bool was_blocking = !(flags & O_NONBLOCK);
    
    if (was_blocking) {
        // 临时设置为非阻塞
        eventfd_set_nonblocking(efd, true);
    }
    
    uint64_t value;
    int result = eventfd_read_value(efd, &value);
    
    if (result == 0) {
        // 读取成功，写回去
        eventfd_write_value(efd, value);
    }
    
    if (was_blocking) {
        // 恢复阻塞模式
        eventfd_set_nonblocking(efd, false);
    }
    
    // 如果能够读取（即使是EAGAIN），说明是有效的eventfd
    return (result == 0 || result == -2) ? 0 : -1;
}

// ============================================================================
// EventFD性能测试
// ============================================================================

int eventfd_benchmark(size_t num_operations) {
    int efd = create_eventfd();
    if (efd == -1) {
        return -1;
    }
    
    uint64_t start_time = get_time_ns();
    
    // 写入测试
    for (size_t i = 0; i < num_operations; i++) {
        if (eventfd_signal(efd) != 0) {
            close_eventfd(efd);
            return -1;
        }
    }
    
    uint64_t write_time = get_time_ns();
    
    // 读取测试
    for (size_t i = 0; i < num_operations; i++) {
        uint64_t value;
        if (eventfd_read_value(efd, &value) != 0) {
            close_eventfd(efd);
            return -1;
        }
    }
    
    uint64_t end_time = get_time_ns();
    
    close_eventfd(efd);
    
    // 计算性能指标
    double write_ops_per_sec = (double)num_operations * 1000000000.0 / (write_time - start_time);
    double read_ops_per_sec = (double)num_operations * 1000000000.0 / (end_time - write_time);
    double total_ops_per_sec = (double)(num_operations * 2) * 1000000000.0 / (end_time - start_time);
    
    printf("=== EventFD Benchmark ===\n");
    printf("Operations: %zu\n", num_operations);
    printf("Write OPS: %.2f ops/sec\n", write_ops_per_sec);
    printf("Read OPS: %.2f ops/sec\n", read_ops_per_sec);
    printf("Total OPS: %.2f ops/sec\n", total_ops_per_sec);
    printf("========================\n");
    
    return 0;
}