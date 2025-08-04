#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <stdarg.h>
#include <ctype.h>

// ============================================================================
// 时间相关函数
// ============================================================================

uint64_t get_time_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
    return 0;
}

uint64_t get_time_us(void) {
    return get_time_ns() / 1000;
}

uint64_t get_time_ms(void) {
    return get_time_ns() / 1000000;
}

uint64_t get_realtime_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
    return 0;
}

void sleep_ns(uint64_t nanoseconds) {
    struct timespec ts;
    ts.tv_sec = nanoseconds / 1000000000ULL;
    ts.tv_nsec = nanoseconds % 1000000000ULL;
    nanosleep(&ts, NULL);
}

void sleep_us(uint64_t microseconds) {
    sleep_ns(microseconds * 1000);
}

void sleep_ms(uint64_t milliseconds) {
    sleep_ns(milliseconds * 1000000);
}

char* format_time_ns(uint64_t nanoseconds, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return NULL;
    }
    
    if (nanoseconds < 1000) {
        snprintf(buffer, buffer_size, "%lu ns", nanoseconds);
    } else if (nanoseconds < 1000000) {
        snprintf(buffer, buffer_size, "%.2f μs", nanoseconds / 1000.0);
    } else if (nanoseconds < 1000000000) {
        snprintf(buffer, buffer_size, "%.2f ms", nanoseconds / 1000000.0);
    } else {
        snprintf(buffer, buffer_size, "%.2f s", nanoseconds / 1000000000.0);
    }
    
    return buffer;
}

char* format_timestamp(uint64_t timestamp_ns, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return NULL;
    }
    
    time_t seconds = timestamp_ns / 1000000000ULL;
    uint64_t nanoseconds = timestamp_ns % 1000000000ULL;
    
    struct tm* tm_info = localtime(&seconds);
    if (!tm_info) {
        snprintf(buffer, buffer_size, "Invalid timestamp");
        return buffer;
    }
    
    snprintf(buffer, buffer_size, "%04d-%02d-%02d %02d:%02d:%02d.%09lu",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, nanoseconds);
    
    return buffer;
}

// ============================================================================
// 日志系统
// ============================================================================

static int g_log_level = 2; // 默认INFO级别
static FILE* g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_log_to_console = true;
static bool g_log_with_timestamp = true;
static bool g_log_with_thread_id = true;

static const char* log_level_names[] = {
    "ERROR", "WARN", "INFO", "DEBUG", "TRACE"
};

static const char* log_level_colors[] = {
    "\033[31m", // ERROR - Red
    "\033[33m", // WARN  - Yellow
    "\033[32m", // INFO  - Green
    "\033[36m", // DEBUG - Cyan
    "\033[37m"  // TRACE - White
};

static const char* log_reset_color = "\033[0m";

void log_set_level(int level) {
    if (level >= 0 && level <= 4) {
        g_log_level = level;
    }
}

int log_get_level(void) {
    return g_log_level;
}

void log_set_file(const char* filename) {
    pthread_mutex_lock(&g_log_mutex);
    
    if (g_log_file && g_log_file != stdout && g_log_file != stderr) {
        fclose(g_log_file);
    }
    
    if (filename) {
        g_log_file = fopen(filename, "a");
        if (!g_log_file) {
            g_log_file = stderr;
        }
    } else {
        g_log_file = NULL;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
}

void log_set_console(bool enable) {
    g_log_to_console = enable;
}

void log_set_timestamp(bool enable) {
    g_log_with_timestamp = enable;
}

void log_set_thread_id(bool enable) {
    g_log_with_thread_id = enable;
}

void log_message(process_pool_t* pool, int level, const char* format, ...) {
    if (level > g_log_level) {
        return;
    }
    
    pthread_mutex_lock(&g_log_mutex);
    
    char buffer[4096];
    char* ptr = buffer;
    size_t remaining = sizeof(buffer);
    
    // 添加时间戳
    if (g_log_with_timestamp) {
        uint64_t now = get_realtime_ns();
        char timestamp[64];
        format_timestamp(now, timestamp, sizeof(timestamp));
        int written = snprintf(ptr, remaining, "[%s] ", timestamp);
        if (written > 0 && written < remaining) {
            ptr += written;
            remaining -= written;
        }
    }
    
    // 添加线程ID
    if (g_log_with_thread_id) {
        pthread_t tid = pthread_self();
        int written = snprintf(ptr, remaining, "[%lu] ", (unsigned long)tid);
        if (written > 0 && written < remaining) {
            ptr += written;
            remaining -= written;
        }
    }
    
    // 添加进程池名称
    if (pool && pool->config.pool_name[0]) {
        int written = snprintf(ptr, remaining, "[%s] ", pool->config.pool_name);
        if (written > 0 && written < remaining) {
            ptr += written;
            remaining -= written;
        }
    }
    
    // 添加日志级别
    int written = snprintf(ptr, remaining, "[%s] ", log_level_names[level]);
    if (written > 0 && written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    // 添加消息内容
    va_list args;
    va_start(args, format);
    written = vsnprintf(ptr, remaining, format, args);
    va_end(args);
    
    if (written > 0 && written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    // 添加换行符
    if (remaining > 1) {
        *ptr++ = '\n';
        *ptr = '\0';
    }
    
    // 输出到控制台
    if (g_log_to_console) {
        if (isatty(STDERR_FILENO)) {
            // 支持颜色的终端
            fprintf(stderr, "%s%s%s", log_level_colors[level], buffer, log_reset_color);
        } else {
            // 不支持颜色的终端
            fprintf(stderr, "%s", buffer);
        }
        fflush(stderr);
    }
    
    // 输出到文件
    if (g_log_file) {
        fprintf(g_log_file, "%s", buffer);
        fflush(g_log_file);
    }
    
    pthread_mutex_unlock(&g_log_mutex);
}

void log_cleanup(void) {
    pthread_mutex_lock(&g_log_mutex);
    
    if (g_log_file && g_log_file != stdout && g_log_file != stderr) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
}

// ============================================================================
// 内存管理
// ============================================================================

void* safe_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    void* ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    
    return ptr;
}

void* safe_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }
    
    // 检查溢出
    if (count > SIZE_MAX / size) {
        return NULL;
    }
    
    return calloc(count, size);
}

void* safe_realloc(void* ptr, size_t new_size) {
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    
    void* new_ptr = realloc(ptr, new_size);
    if (!new_ptr && ptr) {
        // realloc失败，原指针仍然有效
        return NULL;
    }
    
    return new_ptr;
}

char* safe_strdup(const char* str) {
    if (!str) {
        return NULL;
    }
    
    size_t len = strlen(str) + 1;
    char* copy = malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    
    return copy;
}

char* safe_strndup(const char* str, size_t max_len) {
    if (!str) {
        return NULL;
    }
    
    size_t len = strnlen(str, max_len);
    char* copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    
    return copy;
}

void safe_free(void** ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

// ============================================================================
// 字符串处理
// ============================================================================

char* trim_whitespace(char* str) {
    if (!str) {
        return NULL;
    }
    
    // 去除前导空白
    while (isspace(*str)) {
        str++;
    }
    
    if (*str == '\0') {
        return str;
    }
    
    // 去除尾随空白
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        end--;
    }
    
    *(end + 1) = '\0';
    
    return str;
}

int safe_snprintf(char* buffer, size_t size, const char* format, ...) {
    if (!buffer || size == 0 || !format) {
        return -1;
    }
    
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    
    // 确保字符串以null结尾
    buffer[size - 1] = '\0';
    
    return result;
}

bool string_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) {
        return false;
    }
    
    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);
    
    if (prefix_len > str_len) {
        return false;
    }
    
    return strncmp(str, prefix, prefix_len) == 0;
}

bool string_ends_with(const char* str, const char* suffix) {
    if (!str || !suffix) {
        return false;
    }
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) {
        return false;
    }
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

char* string_replace(const char* str, const char* old_substr, const char* new_substr) {
    if (!str || !old_substr || !new_substr) {
        return NULL;
    }
    
    size_t old_len = strlen(old_substr);
    size_t new_len = strlen(new_substr);
    
    if (old_len == 0) {
        return safe_strdup(str);
    }
    
    // 计算替换后的长度
    const char* pos = str;
    size_t count = 0;
    while ((pos = strstr(pos, old_substr)) != NULL) {
        count++;
        pos += old_len;
    }
    
    if (count == 0) {
        return safe_strdup(str);
    }
    
    size_t result_len = strlen(str) + count * (new_len - old_len) + 1;
    char* result = malloc(result_len);
    if (!result) {
        return NULL;
    }
    
    char* dst = result;
    const char* src = str;
    
    while ((pos = strstr(src, old_substr)) != NULL) {
        size_t prefix_len = pos - src;
        memcpy(dst, src, prefix_len);
        dst += prefix_len;
        
        memcpy(dst, new_substr, new_len);
        dst += new_len;
        
        src = pos + old_len;
    }
    
    strcpy(dst, src);
    
    return result;
}

// ============================================================================
// 系统信息
// ============================================================================

int get_cpu_count(void) {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

size_t get_memory_size(void) {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    }
    return 0;
}

size_t get_available_memory(void) {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.freeram * info.mem_unit;
    }
    return 0;
}

long get_page_size(void) {
    return sysconf(_SC_PAGESIZE);
}

int get_max_open_files(void) {
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        return (int)rlim.rlim_cur;
    }
    return -1;
}

int set_max_open_files(int max_files) {
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        return -1;
    }
    
    rlim.rlim_cur = max_files;
    if (rlim.rlim_cur > rlim.rlim_max) {
        rlim.rlim_cur = rlim.rlim_max;
    }
    
    return setrlimit(RLIMIT_NOFILE, &rlim);
}

// ============================================================================
// 进程和线程信息
// ============================================================================

pid_t get_process_id(void) {
    return getpid();
}

pid_t get_parent_process_id(void) {
    return getppid();
}

pthread_t get_thread_id(void) {
    return pthread_self();
}

int set_thread_name(const char* name) {
    if (!name) {
        return -1;
    }
    
    return pthread_setname_np(pthread_self(), name);
}

int get_thread_name(char* name, size_t size) {
    if (!name || size == 0) {
        return -1;
    }
    
    return pthread_getname_np(pthread_self(), name, size);
}

// ============================================================================
// 错误处理
// ============================================================================

const char* get_error_string(int error_code) {
    return strerror(error_code);
}

void print_error(const char* prefix) {
    if (prefix) {
        fprintf(stderr, "%s: %s\n", prefix, strerror(errno));
    } else {
        fprintf(stderr, "Error: %s\n", strerror(errno));
    }
}

void print_errno(const char* prefix, int error_code) {
    if (prefix) {
        fprintf(stderr, "%s: %s\n", prefix, strerror(error_code));
    } else {
        fprintf(stderr, "Error: %s\n", strerror(error_code));
    }
}

// ============================================================================
// 数学和统计函数
// ============================================================================

uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

uint64_t max_u64(uint64_t a, uint64_t b) {
    return a > b ? a : b;
}

uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

double calculate_average(const uint64_t* values, size_t count) {
    if (!values || count == 0) {
        return 0.0;
    }
    
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += values[i];
    }
    
    return (double)sum / count;
}

uint64_t calculate_percentile(uint64_t* values, size_t count, double percentile) {
    if (!values || count == 0 || percentile < 0.0 || percentile > 100.0) {
        return 0;
    }
    
    // 简单的选择排序（对于小数组）
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (values[i] > values[j]) {
                uint64_t temp = values[i];
                values[i] = values[j];
                values[j] = temp;
            }
        }
    }
    
    double index = (percentile / 100.0) * (count - 1);
    size_t lower = (size_t)index;
    size_t upper = lower + 1;
    
    if (upper >= count) {
        return values[count - 1];
    }
    
    double weight = index - lower;
    return (uint64_t)(values[lower] * (1.0 - weight) + values[upper] * weight);
}

// ============================================================================
// 随机数生成
// ============================================================================

static _Atomic uint64_t g_random_seed = 1;

void set_random_seed(uint64_t seed) {
    if (seed == 0) {
        seed = get_time_ns();
    }
    ATOMIC_STORE(&g_random_seed, seed);
}

uint64_t get_random_u64(void) {
    // 简单的线性同余生成器
    uint64_t seed = ATOMIC_LOAD(&g_random_seed);
    uint64_t next = seed * 1103515245ULL + 12345ULL;
    ATOMIC_STORE(&g_random_seed, next);
    return next;
}

uint32_t get_random_u32(void) {
    return (uint32_t)(get_random_u64() >> 32);
}

uint32_t get_random_range(uint32_t min_val, uint32_t max_val) {
    if (min_val >= max_val) {
        return min_val;
    }
    
    uint32_t range = max_val - min_val + 1;
    return min_val + (get_random_u32() % range);
}

double get_random_double(void) {
    return (double)get_random_u64() / (double)UINT64_MAX;
}

// ============================================================================
// 哈希函数
// ============================================================================

uint64_t hash_string(const char* str) {
    if (!str) {
        return 0;
    }
    
    uint64_t hash = 5381;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash;
}

uint64_t hash_memory(const void* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 5381;
    
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) + hash) + bytes[i];
    }
    
    return hash;
}

uint32_t hash_u64(uint64_t value) {
    // MurmurHash3的64位到32位哈希
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    
    return (uint32_t)value;
}

// ============================================================================
// 调试和诊断
// ============================================================================

void dump_memory(const void* data, size_t size, const char* title) {
    if (!data || size == 0) {
        return;
    }
    
    const uint8_t* bytes = (const uint8_t*)data;
    
    printf("=== Memory Dump: %s ===\n", title ? title : "Unknown");
    printf("Address: %p, Size: %zu bytes\n", data, size);
    
    for (size_t i = 0; i < size; i += 16) {
        printf("%08zx: ", i);
        
        // 十六进制显示
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02x ", bytes[i + j]);
            } else {
                printf("   ");
            }
        }
        
        printf(" ");
        
        // ASCII显示
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            uint8_t c = bytes[i + j];
            printf("%c", isprint(c) ? c : '.');
        }
        
        printf("\n");
    }
    
    printf("========================\n");
}

void print_stack_trace(void) {
    // 简单的栈跟踪实现（需要-rdynamic编译选项）
    printf("Stack trace not implemented\n");
}

void assert_fail(const char* assertion, const char* file, int line, const char* function) {
    fprintf(stderr, "Assertion failed: %s\n", assertion);
    fprintf(stderr, "File: %s, Line: %d, Function: %s\n", file, line, function);
    print_stack_trace();
    abort();
}

// 自定义断言宏
#ifdef DEBUG
#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            assert_fail(#expr, __FILE__, __LINE__, __func__); \
        } \
    } while (0)
#else
#define ASSERT(expr) ((void)0)
#endif