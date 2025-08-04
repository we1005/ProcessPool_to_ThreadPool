#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>

// ============================================================================
// 性能计数器
// ============================================================================

typedef struct {
    _Atomic uint64_t value;
    uint64_t last_reset_time;
    char name[64];
} performance_counter_t;

typedef struct {
    _Atomic uint64_t count;
    _Atomic uint64_t total_time;
    _Atomic uint64_t min_time;
    _Atomic uint64_t max_time;
    uint64_t last_reset_time;
    char name[64];
} latency_tracker_t;

typedef struct {
    uint64_t values[METRICS_HISTOGRAM_BUCKETS];
    uint64_t bucket_boundaries[METRICS_HISTOGRAM_BUCKETS];
    _Atomic uint64_t total_count;
    _Atomic uint64_t total_sum;
    uint64_t last_reset_time;
    char name[64];
    pthread_mutex_t mutex;
} histogram_t;

typedef struct {
    performance_counter_t counters[METRICS_MAX_COUNTERS];
    latency_tracker_t latencies[METRICS_MAX_LATENCIES];
    histogram_t histograms[METRICS_MAX_HISTOGRAMS];
    int counter_count;
    int latency_count;
    int histogram_count;
    pthread_mutex_t mutex;
    uint64_t start_time;
} metrics_registry_t;

static metrics_registry_t g_metrics = {0};
static bool g_metrics_initialized = false;

// ============================================================================
// 初始化和清理
// ============================================================================

int metrics_init(void) {
    if (g_metrics_initialized) {
        return 0;
    }
    
    memset(&g_metrics, 0, sizeof(g_metrics));
    
    if (pthread_mutex_init(&g_metrics.mutex, NULL) != 0) {
        return -1;
    }
    
    // 初始化直方图
    for (int i = 0; i < METRICS_MAX_HISTOGRAMS; i++) {
        if (pthread_mutex_init(&g_metrics.histograms[i].mutex, NULL) != 0) {
            // 清理已初始化的互斥锁
            for (int j = 0; j < i; j++) {
                pthread_mutex_destroy(&g_metrics.histograms[j].mutex);
            }
            pthread_mutex_destroy(&g_metrics.mutex);
            return -1;
        }
    }
    
    g_metrics.start_time = get_time_ns();
    g_metrics_initialized = true;
    
    return 0;
}

void metrics_cleanup(void) {
    if (!g_metrics_initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_metrics.mutex);
    
    // 清理直方图互斥锁
    for (int i = 0; i < METRICS_MAX_HISTOGRAMS; i++) {
        pthread_mutex_destroy(&g_metrics.histograms[i].mutex);
    }
    
    pthread_mutex_unlock(&g_metrics.mutex);
    pthread_mutex_destroy(&g_metrics.mutex);
    
    g_metrics_initialized = false;
}

// ============================================================================
// 性能计数器
// ============================================================================

int metrics_counter_register(const char* name) {
    if (!g_metrics_initialized || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_metrics.mutex);
    
    if (g_metrics.counter_count >= METRICS_MAX_COUNTERS) {
        pthread_mutex_unlock(&g_metrics.mutex);
        return -1;
    }
    
    // 检查是否已存在
    for (int i = 0; i < g_metrics.counter_count; i++) {
        if (strcmp(g_metrics.counters[i].name, name) == 0) {
            pthread_mutex_unlock(&g_metrics.mutex);
            return i;
        }
    }
    
    int index = g_metrics.counter_count++;
    performance_counter_t* counter = &g_metrics.counters[index];
    
    ATOMIC_STORE(&counter->value, 0);
    counter->last_reset_time = get_time_ns();
    strncpy(counter->name, name, sizeof(counter->name) - 1);
    counter->name[sizeof(counter->name) - 1] = '\0';
    
    pthread_mutex_unlock(&g_metrics.mutex);
    return index;
}

void metrics_counter_inc(int counter_id) {
    if (!g_metrics_initialized || counter_id < 0 || counter_id >= g_metrics.counter_count) {
        return;
    }
    
    ATOMIC_FETCH_ADD(&g_metrics.counters[counter_id].value, 1);
}

void metrics_counter_add(int counter_id, uint64_t value) {
    if (!g_metrics_initialized || counter_id < 0 || counter_id >= g_metrics.counter_count) {
        return;
    }
    
    ATOMIC_FETCH_ADD(&g_metrics.counters[counter_id].value, value);
}

uint64_t metrics_counter_get(int counter_id) {
    if (!g_metrics_initialized || counter_id < 0 || counter_id >= g_metrics.counter_count) {
        return 0;
    }
    
    return ATOMIC_LOAD(&g_metrics.counters[counter_id].value);
}

void metrics_counter_reset(int counter_id) {
    if (!g_metrics_initialized || counter_id < 0 || counter_id >= g_metrics.counter_count) {
        return;
    }
    
    ATOMIC_STORE(&g_metrics.counters[counter_id].value, 0);
    g_metrics.counters[counter_id].last_reset_time = get_time_ns();
}

// ============================================================================
// 延迟跟踪器
// ============================================================================

int metrics_latency_register(const char* name) {
    if (!g_metrics_initialized || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_metrics.mutex);
    
    if (g_metrics.latency_count >= METRICS_MAX_LATENCIES) {
        pthread_mutex_unlock(&g_metrics.mutex);
        return -1;
    }
    
    // 检查是否已存在
    for (int i = 0; i < g_metrics.latency_count; i++) {
        if (strcmp(g_metrics.latencies[i].name, name) == 0) {
            pthread_mutex_unlock(&g_metrics.mutex);
            return i;
        }
    }
    
    int index = g_metrics.latency_count++;
    latency_tracker_t* tracker = &g_metrics.latencies[index];
    
    ATOMIC_STORE(&tracker->count, 0);
    ATOMIC_STORE(&tracker->total_time, 0);
    ATOMIC_STORE(&tracker->min_time, UINT64_MAX);
    ATOMIC_STORE(&tracker->max_time, 0);
    tracker->last_reset_time = get_time_ns();
    strncpy(tracker->name, name, sizeof(tracker->name) - 1);
    tracker->name[sizeof(tracker->name) - 1] = '\0';
    
    pthread_mutex_unlock(&g_metrics.mutex);
    return index;
}

void metrics_latency_record(int latency_id, uint64_t latency_ns) {
    if (!g_metrics_initialized || latency_id < 0 || latency_id >= g_metrics.latency_count) {
        return;
    }
    
    latency_tracker_t* tracker = &g_metrics.latencies[latency_id];
    
    ATOMIC_FETCH_ADD(&tracker->count, 1);
    ATOMIC_FETCH_ADD(&tracker->total_time, latency_ns);
    
    // 更新最小值
    uint64_t current_min = ATOMIC_LOAD(&tracker->min_time);
    while (latency_ns < current_min) {
        if (ATOMIC_COMPARE_EXCHANGE_WEAK(&tracker->min_time, &current_min, latency_ns)) {
            break;
        }
    }
    
    // 更新最大值
    uint64_t current_max = ATOMIC_LOAD(&tracker->max_time);
    while (latency_ns > current_max) {
        if (ATOMIC_COMPARE_EXCHANGE_WEAK(&tracker->max_time, &current_max, latency_ns)) {
            break;
        }
    }
}

latency_stats_t metrics_latency_get(int latency_id) {
    latency_stats_t stats = {0};
    
    if (!g_metrics_initialized || latency_id < 0 || latency_id >= g_metrics.latency_count) {
        return stats;
    }
    
    latency_tracker_t* tracker = &g_metrics.latencies[latency_id];
    
    stats.count = ATOMIC_LOAD(&tracker->count);
    stats.total_time = ATOMIC_LOAD(&tracker->total_time);
    stats.min_time = ATOMIC_LOAD(&tracker->min_time);
    stats.max_time = ATOMIC_LOAD(&tracker->max_time);
    
    if (stats.count > 0) {
        stats.avg_time = stats.total_time / stats.count;
    }
    
    if (stats.min_time == UINT64_MAX) {
        stats.min_time = 0;
    }
    
    return stats;
}

void metrics_latency_reset(int latency_id) {
    if (!g_metrics_initialized || latency_id < 0 || latency_id >= g_metrics.latency_count) {
        return;
    }
    
    latency_tracker_t* tracker = &g_metrics.latencies[latency_id];
    
    ATOMIC_STORE(&tracker->count, 0);
    ATOMIC_STORE(&tracker->total_time, 0);
    ATOMIC_STORE(&tracker->min_time, UINT64_MAX);
    ATOMIC_STORE(&tracker->max_time, 0);
    tracker->last_reset_time = get_time_ns();
}

// ============================================================================
// 直方图
// ============================================================================

static void init_histogram_buckets(histogram_t* hist, const uint64_t* boundaries, int bucket_count) {
    if (bucket_count > METRICS_HISTOGRAM_BUCKETS) {
        bucket_count = METRICS_HISTOGRAM_BUCKETS;
    }
    
    for (int i = 0; i < bucket_count; i++) {
        hist->bucket_boundaries[i] = boundaries[i];
        hist->values[i] = 0;
    }
    
    // 填充剩余的桶
    for (int i = bucket_count; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        hist->bucket_boundaries[i] = UINT64_MAX;
        hist->values[i] = 0;
    }
}

int metrics_histogram_register(const char* name, const uint64_t* boundaries, int bucket_count) {
    if (!g_metrics_initialized || !name || !boundaries || bucket_count <= 0) {
        return -1;
    }
    
    pthread_mutex_lock(&g_metrics.mutex);
    
    if (g_metrics.histogram_count >= METRICS_MAX_HISTOGRAMS) {
        pthread_mutex_unlock(&g_metrics.mutex);
        return -1;
    }
    
    // 检查是否已存在
    for (int i = 0; i < g_metrics.histogram_count; i++) {
        if (strcmp(g_metrics.histograms[i].name, name) == 0) {
            pthread_mutex_unlock(&g_metrics.mutex);
            return i;
        }
    }
    
    int index = g_metrics.histogram_count++;
    histogram_t* hist = &g_metrics.histograms[index];
    
    init_histogram_buckets(hist, boundaries, bucket_count);
    ATOMIC_STORE(&hist->total_count, 0);
    ATOMIC_STORE(&hist->total_sum, 0);
    hist->last_reset_time = get_time_ns();
    strncpy(hist->name, name, sizeof(hist->name) - 1);
    hist->name[sizeof(hist->name) - 1] = '\0';
    
    pthread_mutex_unlock(&g_metrics.mutex);
    return index;
}

void metrics_histogram_observe(int histogram_id, uint64_t value) {
    if (!g_metrics_initialized || histogram_id < 0 || histogram_id >= g_metrics.histogram_count) {
        return;
    }
    
    histogram_t* hist = &g_metrics.histograms[histogram_id];
    
    pthread_mutex_lock(&hist->mutex);
    
    // 找到合适的桶
    for (int i = 0; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        if (value <= hist->bucket_boundaries[i]) {
            hist->values[i]++;
            break;
        }
    }
    
    pthread_mutex_unlock(&hist->mutex);
    
    ATOMIC_FETCH_ADD(&hist->total_count, 1);
    ATOMIC_FETCH_ADD(&hist->total_sum, value);
}

histogram_stats_t metrics_histogram_get(int histogram_id) {
    histogram_stats_t stats = {0};
    
    if (!g_metrics_initialized || histogram_id < 0 || histogram_id >= g_metrics.histogram_count) {
        return stats;
    }
    
    histogram_t* hist = &g_metrics.histograms[histogram_id];
    
    pthread_mutex_lock(&hist->mutex);
    
    stats.total_count = ATOMIC_LOAD(&hist->total_count);
    stats.total_sum = ATOMIC_LOAD(&hist->total_sum);
    
    for (int i = 0; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        stats.buckets[i] = hist->values[i];
        stats.bucket_boundaries[i] = hist->bucket_boundaries[i];
    }
    
    pthread_mutex_unlock(&hist->mutex);
    
    if (stats.total_count > 0) {
        stats.average = (double)stats.total_sum / stats.total_count;
    }
    
    return stats;
}

void metrics_histogram_reset(int histogram_id) {
    if (!g_metrics_initialized || histogram_id < 0 || histogram_id >= g_metrics.histogram_count) {
        return;
    }
    
    histogram_t* hist = &g_metrics.histograms[histogram_id];
    
    pthread_mutex_lock(&hist->mutex);
    
    for (int i = 0; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        hist->values[i] = 0;
    }
    
    pthread_mutex_unlock(&hist->mutex);
    
    ATOMIC_STORE(&hist->total_count, 0);
    ATOMIC_STORE(&hist->total_sum, 0);
    hist->last_reset_time = get_time_ns();
}

// ============================================================================
// 系统资源监控
// ============================================================================

resource_usage_t get_resource_usage(void) {
    resource_usage_t usage = {0};
    
    struct rusage rusage;
    if (getrusage(RUSAGE_SELF, &rusage) == 0) {
        usage.user_cpu_time = rusage.ru_utime.tv_sec * 1000000000ULL + rusage.ru_utime.tv_usec * 1000ULL;
        usage.system_cpu_time = rusage.ru_stime.tv_sec * 1000000000ULL + rusage.ru_stime.tv_usec * 1000ULL;
        usage.max_resident_set_size = rusage.ru_maxrss * 1024; // KB to bytes
        usage.page_faults = rusage.ru_majflt + rusage.ru_minflt;
        usage.voluntary_context_switches = rusage.ru_nvcsw;
        usage.involuntary_context_switches = rusage.ru_nivcsw;
    }
    
    // 读取内存使用情况
    FILE* status = fopen("/proc/self/status", "r");
    if (status) {
        char line[256];
        while (fgets(line, sizeof(line), status)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                sscanf(line, "VmRSS: %lu kB", &usage.current_rss);
                usage.current_rss *= 1024; // KB to bytes
            } else if (strncmp(line, "VmSize:", 7) == 0) {
                sscanf(line, "VmSize: %lu kB", &usage.virtual_memory_size);
                usage.virtual_memory_size *= 1024; // KB to bytes
            }
        }
        fclose(status);
    }
    
    usage.timestamp = get_time_ns();
    
    return usage;
}

process_stats_t get_process_stats(pid_t pid) {
    process_stats_t stats = {0};
    
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    
    FILE* stat_file = fopen(path, "r");
    if (!stat_file) {
        return stats;
    }
    
    // 读取/proc/pid/stat文件
    char comm[256];
    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned long flags, minflt, cminflt, majflt, cmajflt;
    unsigned long utime, stime, cutime, cstime, priority, nice;
    long num_threads, itrealvalue;
    unsigned long long starttime, vsize;
    long rss;
    
    int ret = fscanf(stat_file, "%d %s %c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %lld %llu %ld",
                     &stats.pid, comm, &state, &ppid, &pgrp, &session, &tty_nr, &tpgid,
                     &flags, &minflt, &cminflt, &majflt, &cmajflt,
                     &utime, &stime, &cutime, &cstime, &priority, &nice,
                     &num_threads, &itrealvalue, &starttime, &vsize, &rss);
    
    fclose(stat_file);
    
    if (ret >= 24) {
        stats.state = state;
        stats.ppid = ppid;
        stats.num_threads = num_threads;
        stats.priority = priority;
        stats.nice = nice;
        stats.user_time = utime;
        stats.system_time = stime;
        stats.virtual_memory = vsize;
        stats.resident_memory = rss * sysconf(_SC_PAGESIZE);
        stats.minor_faults = minflt;
        stats.major_faults = majflt;
        stats.start_time = starttime;
    }
    
    stats.timestamp = get_time_ns();
    
    return stats;
}

// ============================================================================
// 性能分析
// ============================================================================

typedef struct {
    uint64_t start_time;
    const char* name;
    int latency_id;
} perf_timer_t;

perf_timer_t perf_timer_start(const char* name) {
    perf_timer_t timer = {0};
    timer.start_time = get_time_ns();
    timer.name = name;
    timer.latency_id = -1;
    
    if (name) {
        timer.latency_id = metrics_latency_register(name);
    }
    
    return timer;
}

uint64_t perf_timer_end(perf_timer_t* timer) {
    if (!timer || timer->start_time == 0) {
        return 0;
    }
    
    uint64_t end_time = get_time_ns();
    uint64_t elapsed = end_time - timer->start_time;
    
    if (timer->latency_id >= 0) {
        metrics_latency_record(timer->latency_id, elapsed);
    }
    
    timer->start_time = 0;
    
    return elapsed;
}

// ============================================================================
// 报告生成
// ============================================================================

void metrics_print_summary(FILE* output) {
    if (!output || !g_metrics_initialized) {
        return;
    }
    
    uint64_t now = get_time_ns();
    uint64_t uptime = now - g_metrics.start_time;
    
    fprintf(output, "\n=== Process Pool Metrics Summary ===\n");
    fprintf(output, "Uptime: %.2f seconds\n", uptime / 1e9);
    fprintf(output, "Timestamp: %lu ns\n", now);
    
    // 打印计数器
    if (g_metrics.counter_count > 0) {
        fprintf(output, "\n--- Counters ---\n");
        for (int i = 0; i < g_metrics.counter_count; i++) {
            performance_counter_t* counter = &g_metrics.counters[i];
            uint64_t value = ATOMIC_LOAD(&counter->value);
            uint64_t age = now - counter->last_reset_time;
            double rate = age > 0 ? (double)value / (age / 1e9) : 0.0;
            
            fprintf(output, "  %s: %lu (%.2f/sec)\n", counter->name, value, rate);
        }
    }
    
    // 打印延迟统计
    if (g_metrics.latency_count > 0) {
        fprintf(output, "\n--- Latencies ---\n");
        for (int i = 0; i < g_metrics.latency_count; i++) {
            latency_stats_t stats = metrics_latency_get(i);
            if (stats.count > 0) {
                char min_str[32], max_str[32], avg_str[32];
                format_time_ns(stats.min_time, min_str, sizeof(min_str));
                format_time_ns(stats.max_time, max_str, sizeof(max_str));
                format_time_ns(stats.avg_time, avg_str, sizeof(avg_str));
                
                fprintf(output, "  %s: count=%lu, min=%s, max=%s, avg=%s\n",
                        g_metrics.latencies[i].name, stats.count, min_str, max_str, avg_str);
            }
        }
    }
    
    // 打印直方图
    if (g_metrics.histogram_count > 0) {
        fprintf(output, "\n--- Histograms ---\n");
        for (int i = 0; i < g_metrics.histogram_count; i++) {
            histogram_stats_t stats = metrics_histogram_get(i);
            if (stats.total_count > 0) {
                fprintf(output, "  %s: count=%lu, sum=%lu, avg=%.2f\n",
                        g_metrics.histograms[i].name, stats.total_count, stats.total_sum, stats.average);
                
                // 打印桶分布
                for (int j = 0; j < METRICS_HISTOGRAM_BUCKETS && stats.bucket_boundaries[j] != UINT64_MAX; j++) {
                    if (stats.buckets[j] > 0) {
                        fprintf(output, "    <= %lu: %lu\n", stats.bucket_boundaries[j], stats.buckets[j]);
                    }
                }
            }
        }
    }
    
    // 打印资源使用情况
    resource_usage_t usage = get_resource_usage();
    fprintf(output, "\n--- Resource Usage ---\n");
    fprintf(output, "  Current RSS: %.2f MB\n", usage.current_rss / (1024.0 * 1024.0));
    fprintf(output, "  Max RSS: %.2f MB\n", usage.max_resident_set_size / (1024.0 * 1024.0));
    fprintf(output, "  Virtual Memory: %.2f MB\n", usage.virtual_memory_size / (1024.0 * 1024.0));
    fprintf(output, "  User CPU Time: %.2f seconds\n", usage.user_cpu_time / 1e9);
    fprintf(output, "  System CPU Time: %.2f seconds\n", usage.system_cpu_time / 1e9);
    fprintf(output, "  Page Faults: %lu\n", usage.page_faults);
    fprintf(output, "  Context Switches: %lu voluntary, %lu involuntary\n",
            usage.voluntary_context_switches, usage.involuntary_context_switches);
    
    fprintf(output, "\n=====================================\n\n");
}

void metrics_export_json(FILE* output) {
    if (!output || !g_metrics_initialized) {
        return;
    }
    
    uint64_t now = get_time_ns();
    uint64_t uptime = now - g_metrics.start_time;
    
    fprintf(output, "{\n");
    fprintf(output, "  \"timestamp\": %lu,\n", now);
    fprintf(output, "  \"uptime_ns\": %lu,\n", uptime);
    
    // 导出计数器
    fprintf(output, "  \"counters\": {\n");
    for (int i = 0; i < g_metrics.counter_count; i++) {
        performance_counter_t* counter = &g_metrics.counters[i];
        uint64_t value = ATOMIC_LOAD(&counter->value);
        fprintf(output, "    \"%s\": %lu", counter->name, value);
        if (i < g_metrics.counter_count - 1) fprintf(output, ",");
        fprintf(output, "\n");
    }
    fprintf(output, "  },\n");
    
    // 导出延迟统计
    fprintf(output, "  \"latencies\": {\n");
    for (int i = 0; i < g_metrics.latency_count; i++) {
        latency_stats_t stats = metrics_latency_get(i);
        fprintf(output, "    \"%s\": {\n", g_metrics.latencies[i].name);
        fprintf(output, "      \"count\": %lu,\n", stats.count);
        fprintf(output, "      \"total_time\": %lu,\n", stats.total_time);
        fprintf(output, "      \"min_time\": %lu,\n", stats.min_time);
        fprintf(output, "      \"max_time\": %lu,\n", stats.max_time);
        fprintf(output, "      \"avg_time\": %lu\n", stats.avg_time);
        fprintf(output, "    }");
        if (i < g_metrics.latency_count - 1) fprintf(output, ",");
        fprintf(output, "\n");
    }
    fprintf(output, "  },\n");
    
    // 导出资源使用情况
    resource_usage_t usage = get_resource_usage();
    fprintf(output, "  \"resource_usage\": {\n");
    fprintf(output, "    \"current_rss\": %lu,\n", usage.current_rss);
    fprintf(output, "    \"max_rss\": %lu,\n", usage.max_resident_set_size);
    fprintf(output, "    \"virtual_memory\": %lu,\n", usage.virtual_memory_size);
    fprintf(output, "    \"user_cpu_time\": %lu,\n", usage.user_cpu_time);
    fprintf(output, "    \"system_cpu_time\": %lu,\n", usage.system_cpu_time);
    fprintf(output, "    \"page_faults\": %lu,\n", usage.page_faults);
    fprintf(output, "    \"voluntary_context_switches\": %lu,\n", usage.voluntary_context_switches);
    fprintf(output, "    \"involuntary_context_switches\": %lu\n", usage.involuntary_context_switches);
    fprintf(output, "  }\n");
    
    fprintf(output, "}\n");
}

void metrics_reset_all(void) {
    if (!g_metrics_initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_metrics.mutex);
    
    // 重置所有计数器
    for (int i = 0; i < g_metrics.counter_count; i++) {
        metrics_counter_reset(i);
    }
    
    // 重置所有延迟跟踪器
    for (int i = 0; i < g_metrics.latency_count; i++) {
        metrics_latency_reset(i);
    }
    
    // 重置所有直方图
    for (int i = 0; i < g_metrics.histogram_count; i++) {
        metrics_histogram_reset(i);
    }
    
    pthread_mutex_unlock(&g_metrics.mutex);
}

// ============================================================================
// 预定义的指标
// ============================================================================

static int g_task_submitted_counter = -1;
static int g_task_completed_counter = -1;
static int g_task_failed_counter = -1;
static int g_task_cancelled_counter = -1;
static int g_worker_created_counter = -1;
static int g_worker_destroyed_counter = -1;
static int g_task_latency_tracker = -1;
static int g_queue_latency_tracker = -1;

void metrics_init_pool_metrics(void) {
    g_task_submitted_counter = metrics_counter_register("tasks_submitted");
    g_task_completed_counter = metrics_counter_register("tasks_completed");
    g_task_failed_counter = metrics_counter_register("tasks_failed");
    g_task_cancelled_counter = metrics_counter_register("tasks_cancelled");
    g_worker_created_counter = metrics_counter_register("workers_created");
    g_worker_destroyed_counter = metrics_counter_register("workers_destroyed");
    g_task_latency_tracker = metrics_latency_register("task_execution_time");
    g_queue_latency_tracker = metrics_latency_register("task_queue_time");
}

void metrics_task_submitted(void) {
    if (g_task_submitted_counter >= 0) {
        metrics_counter_inc(g_task_submitted_counter);
    }
}

void metrics_task_completed(uint64_t execution_time_ns) {
    if (g_task_completed_counter >= 0) {
        metrics_counter_inc(g_task_completed_counter);
    }
    if (g_task_latency_tracker >= 0) {
        metrics_latency_record(g_task_latency_tracker, execution_time_ns);
    }
}

void metrics_task_failed(void) {
    if (g_task_failed_counter >= 0) {
        metrics_counter_inc(g_task_failed_counter);
    }
}

void metrics_task_cancelled(void) {
    if (g_task_cancelled_counter >= 0) {
        metrics_counter_inc(g_task_cancelled_counter);
    }
}

void metrics_worker_created(void) {
    if (g_worker_created_counter >= 0) {
        metrics_counter_inc(g_worker_created_counter);
    }
}

void metrics_worker_destroyed(void) {
    if (g_worker_destroyed_counter >= 0) {
        metrics_counter_inc(g_worker_destroyed_counter);
    }
}

void metrics_task_queue_time(uint64_t queue_time_ns) {
    if (g_queue_latency_tracker >= 0) {
        metrics_latency_record(g_queue_latency_tracker, queue_time_ns);
    }
}