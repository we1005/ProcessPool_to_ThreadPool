#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

// ============================================================================
// 共享内存管理
// ============================================================================

shared_memory_t* shm_create(const char* name, size_t size) {
    if (!name || size == 0) {
        return NULL;
    }
    
    // 创建共享内存对象
    int shm_fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd == -1) {
        if (errno == EEXIST) {
            // 共享内存已存在，先删除再创建
            shm_unlink(name);
            shm_fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
        }
        
        if (shm_fd == -1) {
            return NULL;
        }
    }
    
    // 设置共享内存大小
    if (ftruncate(shm_fd, size) == -1) {
        close(shm_fd);
        shm_unlink(name);
        return NULL;
    }
    
    // 映射共享内存
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        close(shm_fd);
        shm_unlink(name);
        return NULL;
    }
    
    close(shm_fd); // 映射后可以关闭文件描述符
    
    // 初始化共享内存结构
    shared_memory_t* shm = (shared_memory_t*)addr;
    memset(shm, 0, sizeof(shared_memory_t));
    
    // 设置魔数和版本
    shm->magic = SHM_MAGIC;
    shm->version = SHM_VERSION;
    shm->size = size;
    
    // 初始化原子变量
    ATOMIC_STORE(&shm->producer_pos, 0);
    ATOMIC_STORE(&shm->consumer_pos, 0);
    ATOMIC_STORE(&shm->total_submitted, 0);
    ATOMIC_STORE(&shm->total_completed, 0);
    ATOMIC_STORE(&shm->total_failed, 0);
    
    // 初始化互斥锁（进程间共享）
    pthread_mutexattr_t mutex_attr;
    if (pthread_mutexattr_init(&mutex_attr) == 0) {
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&shm->mutex, &mutex_attr);
        pthread_mutexattr_destroy(&mutex_attr);
    }
    
    // 初始化条件变量（进程间共享）
    pthread_condattr_t cond_attr;
    if (pthread_condattr_init(&cond_attr) == 0) {
        pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&shm->not_empty, &cond_attr);
        pthread_cond_init(&shm->not_full, &cond_attr);
        pthread_condattr_destroy(&cond_attr);
    }
    
    return shm;
}

shared_memory_t* shm_open_existing(const char* name, size_t size) {
    if (!name || size == 0) {
        return NULL;
    }
    
    // 打开现有的共享内存对象
    int shm_fd = shm_open(name, O_RDWR, 0666);
    if (shm_fd == -1) {
        return NULL;
    }
    
    // 映射共享内存
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        close(shm_fd);
        return NULL;
    }
    
    close(shm_fd);
    
    shared_memory_t* shm = (shared_memory_t*)addr;
    
    // 验证魔数和版本
    if (shm->magic != SHM_MAGIC || shm->version != SHM_VERSION) {
        munmap(addr, size);
        return NULL;
    }
    
    return shm;
}

void shm_destroy(shared_memory_t* shm, const char* name, size_t size) {
    if (!shm || !name) {
        return;
    }
    
    // 销毁同步原语
    pthread_mutex_destroy(&shm->mutex);
    pthread_cond_destroy(&shm->not_empty);
    pthread_cond_destroy(&shm->not_full);
    
    // 取消映射
    munmap(shm, size);
    
    // 删除共享内存对象
    shm_unlink(name);
}

// ============================================================================
// 共享内存队列操作
// ============================================================================

static inline uint32_t shm_queue_next_pos(uint32_t pos, uint32_t size) {
    return (pos + 1) % size;
}

static inline bool shm_queue_is_full(shared_memory_t* shm) {
    uint32_t producer_pos = ATOMIC_LOAD(&shm->producer_pos);
    uint32_t consumer_pos = ATOMIC_LOAD(&shm->consumer_pos);
    return shm_queue_next_pos(producer_pos, shm->queue_size) == consumer_pos;
}

static inline bool shm_queue_is_empty(shared_memory_t* shm) {
    uint32_t producer_pos = ATOMIC_LOAD(&shm->producer_pos);
    uint32_t consumer_pos = ATOMIC_LOAD(&shm->consumer_pos);
    return producer_pos == consumer_pos;
}

static inline uint32_t shm_queue_size(shared_memory_t* shm) {
    uint32_t producer_pos = ATOMIC_LOAD(&shm->producer_pos);
    uint32_t consumer_pos = ATOMIC_LOAD(&shm->consumer_pos);
    
    if (producer_pos >= consumer_pos) {
        return producer_pos - consumer_pos;
    } else {
        return shm->queue_size - consumer_pos + producer_pos;
    }
}

int shm_queue_enqueue(shared_memory_t* shm, const void* data, size_t data_size) {
    if (!shm || !data || data_size == 0 || data_size > MAX_TASK_DATA_SIZE) {
        return -1;
    }
    
    pthread_mutex_lock(&shm->mutex);
    
    // 等待队列有空间
    while (shm_queue_is_full(shm)) {
        pthread_cond_wait(&shm->not_full, &shm->mutex);
    }
    
    uint32_t producer_pos = ATOMIC_LOAD(&shm->producer_pos);
    
    // 计算数据存储位置
    char* queue_data = (char*)shm + sizeof(shared_memory_t);
    char* slot = queue_data + producer_pos * MAX_TASK_DATA_SIZE;
    
    // 存储数据大小（前4字节）
    *(uint32_t*)slot = (uint32_t)data_size;
    
    // 存储数据内容
    memcpy(slot + sizeof(uint32_t), data, data_size);
    
    // 内存屏障确保数据写入完成
    MEMORY_BARRIER();
    
    // 更新生产者位置
    ATOMIC_STORE(&shm->producer_pos, shm_queue_next_pos(producer_pos, shm->queue_size));
    
    // 更新统计信息
    ATOMIC_ADD(&shm->total_submitted, 1);
    
    // 通知消费者
    pthread_cond_signal(&shm->not_empty);
    
    pthread_mutex_unlock(&shm->mutex);
    
    return 0;
}

int shm_queue_dequeue(shared_memory_t* shm, void* data, size_t* data_size, uint32_t timeout_ms) {
    if (!shm || !data || !data_size) {
        return -1;
    }
    
    pthread_mutex_lock(&shm->mutex);
    
    int result = 0;
    
    if (timeout_ms == 0) {
        // 无限等待
        while (shm_queue_is_empty(shm)) {
            pthread_cond_wait(&shm->not_empty, &shm->mutex);
        }
    } else {
        // 超时等待
        struct timespec abs_timeout;
        clock_gettime(CLOCK_REALTIME, &abs_timeout);
        
        abs_timeout.tv_sec += timeout_ms / 1000;
        abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (abs_timeout.tv_nsec >= 1000000000) {
            abs_timeout.tv_sec++;
            abs_timeout.tv_nsec -= 1000000000;
        }
        
        while (shm_queue_is_empty(shm)) {
            int ret = pthread_cond_timedwait(&shm->not_empty, &shm->mutex, &abs_timeout);
            if (ret == ETIMEDOUT) {
                result = -1;
                goto cleanup;
            } else if (ret != 0) {
                result = -1;
                goto cleanup;
            }
        }
    }
    
    uint32_t consumer_pos = ATOMIC_LOAD(&shm->consumer_pos);
    
    // 计算数据读取位置
    char* queue_data = (char*)shm + sizeof(shared_memory_t);
    char* slot = queue_data + consumer_pos * MAX_TASK_DATA_SIZE;
    
    // 读取数据大小
    uint32_t stored_size = *(uint32_t*)slot;
    
    if (stored_size > *data_size) {
        // 缓冲区太小
        *data_size = stored_size;
        result = -1;
        goto cleanup;
    }
    
    // 读取数据内容
    memcpy(data, slot + sizeof(uint32_t), stored_size);
    *data_size = stored_size;
    
    // 内存屏障确保数据读取完成
    MEMORY_BARRIER();
    
    // 更新消费者位置
    ATOMIC_STORE(&shm->consumer_pos, shm_queue_next_pos(consumer_pos, shm->queue_size));
    
    // 通知生产者
    pthread_cond_signal(&shm->not_full);
    
cleanup:
    pthread_mutex_unlock(&shm->mutex);
    return result;
}

int shm_queue_try_enqueue(shared_memory_t* shm, const void* data, size_t data_size) {
    if (!shm || !data || data_size == 0 || data_size > MAX_TASK_DATA_SIZE) {
        return -1;
    }
    
    if (pthread_mutex_trylock(&shm->mutex) != 0) {
        return -1; // 无法获取锁
    }
    
    int result = -1;
    
    if (!shm_queue_is_full(shm)) {
        uint32_t producer_pos = ATOMIC_LOAD(&shm->producer_pos);
        
        // 计算数据存储位置
        char* queue_data = (char*)shm + sizeof(shared_memory_t);
        char* slot = queue_data + producer_pos * MAX_TASK_DATA_SIZE;
        
        // 存储数据
        *(uint32_t*)slot = (uint32_t)data_size;
        memcpy(slot + sizeof(uint32_t), data, data_size);
        
        MEMORY_BARRIER();
        
        // 更新位置
        ATOMIC_STORE(&shm->producer_pos, shm_queue_next_pos(producer_pos, shm->queue_size));
        ATOMIC_ADD(&shm->total_submitted, 1);
        
        // 通知消费者
        pthread_cond_signal(&shm->not_empty);
        
        result = 0;
    }
    
    pthread_mutex_unlock(&shm->mutex);
    return result;
}

int shm_queue_try_dequeue(shared_memory_t* shm, void* data, size_t* data_size) {
    if (!shm || !data || !data_size) {
        return -1;
    }
    
    if (pthread_mutex_trylock(&shm->mutex) != 0) {
        return -1; // 无法获取锁
    }
    
    int result = -1;
    
    if (!shm_queue_is_empty(shm)) {
        uint32_t consumer_pos = ATOMIC_LOAD(&shm->consumer_pos);
        
        // 计算数据读取位置
        char* queue_data = (char*)shm + sizeof(shared_memory_t);
        char* slot = queue_data + consumer_pos * MAX_TASK_DATA_SIZE;
        
        // 读取数据
        uint32_t stored_size = *(uint32_t*)slot;
        
        if (stored_size <= *data_size) {
            memcpy(data, slot + sizeof(uint32_t), stored_size);
            *data_size = stored_size;
            
            MEMORY_BARRIER();
            
            // 更新位置
            ATOMIC_STORE(&shm->consumer_pos, shm_queue_next_pos(consumer_pos, shm->queue_size));
            
            // 通知生产者
            pthread_cond_signal(&shm->not_full);
            
            result = 0;
        } else {
            *data_size = stored_size; // 返回所需大小
        }
    }
    
    pthread_mutex_unlock(&shm->mutex);
    return result;
}

// ============================================================================
// 共享内存统计信息
// ============================================================================

void shm_get_stats(shared_memory_t* shm, shm_stats_t* stats) {
    if (!shm || !stats) {
        return;
    }
    
    pthread_mutex_lock(&shm->mutex);
    
    stats->queue_size = shm->queue_size;
    stats->current_size = shm_queue_size(shm);
    stats->is_full = shm_queue_is_full(shm);
    stats->is_empty = shm_queue_is_empty(shm);
    stats->total_submitted = ATOMIC_LOAD(&shm->total_submitted);
    stats->total_completed = ATOMIC_LOAD(&shm->total_completed);
    stats->total_failed = ATOMIC_LOAD(&shm->total_failed);
    
    pthread_mutex_unlock(&shm->mutex);
}

void shm_reset_stats(shared_memory_t* shm) {
    if (!shm) {
        return;
    }
    
    ATOMIC_STORE(&shm->total_submitted, 0);
    ATOMIC_STORE(&shm->total_completed, 0);
    ATOMIC_STORE(&shm->total_failed, 0);
}

// ============================================================================
// 共享内存调试和诊断
// ============================================================================

void shm_dump_info(shared_memory_t* shm, const char* name) {
    if (!shm) {
        return;
    }
    
    printf("=== Shared Memory Info: %s ===\n", name ? name : "Unknown");
    printf("Magic: 0x%08x\n", shm->magic);
    printf("Version: %u\n", shm->version);
    printf("Size: %zu bytes\n", shm->size);
    printf("Queue Size: %u\n", shm->queue_size);
    
    pthread_mutex_lock(&shm->mutex);
    
    printf("Producer Position: %u\n", ATOMIC_LOAD(&shm->producer_pos));
    printf("Consumer Position: %u\n", ATOMIC_LOAD(&shm->consumer_pos));
    printf("Current Queue Size: %u\n", shm_queue_size(shm));
    printf("Queue Full: %s\n", shm_queue_is_full(shm) ? "Yes" : "No");
    printf("Queue Empty: %s\n", shm_queue_is_empty(shm) ? "Yes" : "No");
    printf("Total Submitted: %lu\n", ATOMIC_LOAD(&shm->total_submitted));
    printf("Total Completed: %lu\n", ATOMIC_LOAD(&shm->total_completed));
    printf("Total Failed: %lu\n", ATOMIC_LOAD(&shm->total_failed));
    
    pthread_mutex_unlock(&shm->mutex);
    
    printf("================================\n");
}

bool shm_validate(shared_memory_t* shm) {
    if (!shm) {
        return false;
    }
    
    // 检查魔数和版本
    if (shm->magic != SHM_MAGIC || shm->version != SHM_VERSION) {
        return false;
    }
    
    // 检查队列大小
    if (shm->queue_size == 0 || shm->queue_size > MAX_QUEUE_SIZE) {
        return false;
    }
    
    // 检查位置有效性
    uint32_t producer_pos = ATOMIC_LOAD(&shm->producer_pos);
    uint32_t consumer_pos = ATOMIC_LOAD(&shm->consumer_pos);
    
    if (producer_pos >= shm->queue_size || consumer_pos >= shm->queue_size) {
        return false;
    }
    
    return true;
}

int shm_repair(shared_memory_t* shm) {
    if (!shm) {
        return -1;
    }
    
    int repairs = 0;
    
    // 修复魔数
    if (shm->magic != SHM_MAGIC) {
        shm->magic = SHM_MAGIC;
        repairs++;
    }
    
    // 修复版本
    if (shm->version != SHM_VERSION) {
        shm->version = SHM_VERSION;
        repairs++;
    }
    
    // 修复队列位置
    uint32_t producer_pos = ATOMIC_LOAD(&shm->producer_pos);
    uint32_t consumer_pos = ATOMIC_LOAD(&shm->consumer_pos);
    
    if (producer_pos >= shm->queue_size) {
        ATOMIC_STORE(&shm->producer_pos, 0);
        repairs++;
    }
    
    if (consumer_pos >= shm->queue_size) {
        ATOMIC_STORE(&shm->consumer_pos, 0);
        repairs++;
    }
    
    return repairs;
}

// ============================================================================
// 共享内存性能测试
// ============================================================================

int shm_benchmark(shared_memory_t* shm, size_t num_operations, size_t data_size) {
    if (!shm || num_operations == 0 || data_size == 0 || data_size > MAX_TASK_DATA_SIZE) {
        return -1;
    }
    
    char* test_data = malloc(data_size);
    if (!test_data) {
        return -1;
    }
    
    // 填充测试数据
    memset(test_data, 0xAA, data_size);
    
    uint64_t start_time = get_time_ns();
    
    // 写入测试
    for (size_t i = 0; i < num_operations; i++) {
        if (shm_queue_try_enqueue(shm, test_data, data_size) != 0) {
            // 队列满，等待一下
            usleep(1000);
            i--; // 重试
        }
    }
    
    uint64_t write_time = get_time_ns();
    
    // 读取测试
    char* read_buffer = malloc(data_size);
    if (!read_buffer) {
        free(test_data);
        return -1;
    }
    
    for (size_t i = 0; i < num_operations; i++) {
        size_t read_size = data_size;
        if (shm_queue_try_dequeue(shm, read_buffer, &read_size) != 0) {
            // 队列空，等待一下
            usleep(1000);
            i--; // 重试
        }
    }
    
    uint64_t end_time = get_time_ns();
    
    // 计算性能指标
    double write_ops_per_sec = (double)num_operations * 1000000000.0 / (write_time - start_time);
    double read_ops_per_sec = (double)num_operations * 1000000000.0 / (end_time - write_time);
    double total_ops_per_sec = (double)(num_operations * 2) * 1000000000.0 / (end_time - start_time);
    
    printf("=== Shared Memory Benchmark ===\n");
    printf("Operations: %zu\n", num_operations);
    printf("Data Size: %zu bytes\n", data_size);
    printf("Write OPS: %.2f ops/sec\n", write_ops_per_sec);
    printf("Read OPS: %.2f ops/sec\n", read_ops_per_sec);
    printf("Total OPS: %.2f ops/sec\n", total_ops_per_sec);
    printf("==============================\n");
    
    free(test_data);
    free(read_buffer);
    
    return 0;
}