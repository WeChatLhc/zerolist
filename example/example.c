/**
 * @file example.c
 * @brief 双向循环链表使用示例
 * @author liuhc
 * @date 2025-11-06
 *
 * 本示例展示了双向循环链表库的各种使用场景和功能特性
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../zerolist.h"

// ===========================================
// 示例数据结构
// ===========================================

typedef struct
{
    int  id;
    char name[32];
} Person;

#define PERF_TEST_NODE_COUNT        200
#define PERF_TEST_ROUNDS            3
#define RANDOM_OP_NODE_COUNT        200
#define RANDOM_OP_ROUNDS            1000
#define RANDOM_OP_PROGRESS_INTERVAL 200

static void fill_person(Person* p, int id, const char* prefix)
{
    if (!p) return;
    p->id = id;
    snprintf(p->name, sizeof(p->name), "%s_%d", prefix ? prefix : "User", id);
}

static double now_ms(void)
{
#if defined(_WIN32)
    static double inv_freq = 0.0;
    LARGE_INTEGER counter;
    if (inv_freq == 0.0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        inv_freq = 1000.0 / (double)freq.QuadPart;
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * inv_freq;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

// ===========================================
// 回调函数示例
// ===========================================

/**
 * @brief 打印人员信息
 */
static void print_person(void* data)
{
    if (!data) return;
    Person* p = (Person*)data;
    printf("  [%02d] %s\n", p->id, p->name);
}

/**
 * @brief 按 ID 匹配人员
 */
static bool cmp_person_id(const void* a, const void* b)
{
    return ((const Person*)a)->id == ((const Person*)b)->id;
}

// ===========================================
// 示例 1: 静态模式（默认，适合 MCU 嵌入式）
// ===========================================

void example_static_mode(void)
{
    printf("\n========== 示例 1: 静态模式 ==========\n");

    // 定义静态链表（预分配 32 个节点）
    ZEROLIST_DEFINE(list, 32);
    ZEROLIST_INIT(list);

    // 准备测试数据
    Person people[10];
    for (int i = 0; i < 10; i++) {
        people[i].id = i + 1;
        sprintf(people[i].name, "User_%d", i + 1);
    }

    // 基本插入操作
    printf("\n1. 插入节点:\n");
    for (int i = 0; i < 5; i++) {
        zerolist_push_back(&list, &people[i]);
    }
    printf("   插入前链表内容:\n");
    zerolist_foreach(&list, print_person);
    zerolist_push_front(&list, &people[5]);
    printf("   插入后链表内容:\n");
    zerolist_foreach(&list, print_person);
    zerolist_insert_before(&list, &people[2], &people[6]);
    printf("   插入前链表内容:\n");
    zerolist_foreach(&list, print_person);
    printf("   插入后链表内容:\n");
    zerolist_foreach(&list, print_person);

    printf("   链表内容:\n");
    zerolist_foreach(&list, print_person);
    printf("   链表大小: %d\n", (int)zerolist_size(&list));

    // 查找操作
    printf("\n2. 查找节点:\n");
    Person* found = (Person*)zerolist_at(&list, 2);
    if (found) {
        printf("   索引 2: %s\n", found->name);
    }

    zerolist_node_t* node = zerolist_at(&list, 3);

    // 删除操作
    printf("\n3. 删除节点:\n");
    zerolist_remove_ptr(&list, &people[1]);
    zerolist_remove_if(&list, &people[3], cmp_person_id);
    zerolist_remove_at(&list, 0);

    printf("   删除后链表:\n");
    zerolist_foreach(&list, print_person);

    // 反转操作
    printf("\n4. 反转链表:\n");
    zerolist_reverse(&list);
    printf("   反转后:\n");
    zerolist_foreach(&list, print_person);

    // 清空链表
    printf("\n5. 清空链表:\n");
    zerolist_clear(&list);
    printf("   清空后大小: %d\n", (int)zerolist_size(&list));
}

// ===========================================
// 示例 2: 动态模式（适合通用 Linux 环境）
// ===========================================

#if LIST_USE_MALLOC
void example_dynamic_mode(void)
{
    printf("\n========== 示例 2: 动态模式 ==========\n");

    // 定义动态链表
    DEFINE_LINKED_LIST(list, 0);
    if (!INIT_LINKED_LIST(list)) {
        printf("初始化失败！\n");
        return;
    }

    // 动态分配测试数据
    Person* people = (Person*)malloc(10 * sizeof(Person));
    for (int i = 0; i < 10; i++) {
        people[i].id = i + 1;
        sprintf(people[i].name, "Dynamic_%d", i + 1);
    }

    // 插入大量节点
    printf("\n插入 10 个节点:\n");
    for (int i = 0; i < 10; i++) {
        zerolist_push_back(&list, &people[i]);
    }

    printf("链表内容:\n");
    zerolist_foreach(&list, print_person);
    printf("链表大小: %d\n", (int)zerolist_size(&list));

    // 安全遍历并删除
    printf("\n安全遍历并删除 ID 为偶数的节点:\n");
    LIST_FOR_EACH_SAFE(&list, node, tmp)
    {
        Person* p = (Person*)node->data;
        if (p && p->id % 2 == 0) {
            zerolist_remove_ptr(&list, p);
            printf("  删除: %s\n", p->name);
        }
    }

    printf("\n删除后链表:\n");
    zerolist_foreach(&list, print_person);

    // 清理
    zerolist_clear(&list);
    free(people);
}
#endif

// ===========================================
// 示例 3: 静态模式 + malloc 回退
// ===========================================

#if !LIST_USE_MALLOC && ZEROLIST_STATIC_FALLBACK_MALLOC
void example_static_with_fallback(void)
{
    printf("\n========== 示例 3: 静态模式 + malloc 回退 ==========\n");

    // 定义静态链表（只有 5 个节点）
    ZEROLIST_DEFINE(list, 5);
    ZEROLIST_INIT(list);

    Person people[10];
    for (int i = 0; i < 10; i++) {
        people[i].id = i + 1;
        sprintf(people[i].name, "Fallback_%d", i + 1);
    }

    printf("\n插入 10 个节点（静态缓冲区只有 5 个）:\n");
    for (int i = 0; i < 10; i++) {
        bool success = zerolist_push_back(&list, &people[i]);
        if (success) {
            printf("  [%d] %s - %s\n", i + 1, people[i].name,
                   i < 5 ? "静态节点" : "动态节点(malloc)");
        }
    }

    printf("\n链表大小: %d\n", (int)zerolist_size(&list));
    printf("链表内容:\n");
    zerolist_foreach(&list, print_person);

    // 清理（静态节点自动回收，动态节点自动 free）
    zerolist_clear(&list);
#if LIST_ENABLE_FAST_ALLOC
    printf("\n清空后空闲栈状态: free_top=%d, max_nodes=%d\n", (int)list.free_top,
           (int)list.max_nodes);
#endif

    printf("\n清空后再次插入 5 个节点，验证静态节点是否可重复利用:\n");
    for (int i = 0; i < 5; i++) {
        if (zerolist_push_back(&list, &people[i])) {
            printf("  再次插入: %s\n", people[i].name);
        } else {
            printf("  再次插入失败: %s\n", people[i].name);
        }
    }
    printf("  第二轮链表大小: %d\n", (int)zerolist_size(&list));
#if LIST_ENABLE_FAST_ALLOC
    printf("  当前 free_top=%d (期望=%d)\n", (int)list.free_top,
           (int)(list.max_nodes - zerolist_size(&list)));
#endif

    zerolist_clear(&list);
}
#endif

// ===========================================
// 示例 4: 静态模式 + 动态扩容
// ===========================================

#if !LIST_USE_MALLOC && ZEROLIST_STATIC_DYNAMIC_EXPAND
void example_dynamic_expand(void)
{
    printf("\n========== 示例 4: 静态模式 + 动态扩容 ==========\n");

    // 定义动态扩容链表（初始 4 个节点）
    ZEROLIST_DEFINE(list, 4);
    if (!ZEROLIST_INIT(list)) {
        printf("初始化失败！\n");
        return;
    }

    Person people[20];
    for (int i = 0; i < 20; i++) {
        people[i].id = i + 1;
        sprintf(people[i].name, "Expand_%d", i + 1);
    }

    printf("\n插入 20 个节点（初始缓冲区 4 个，会自动扩容）:\n");
    for (int i = 0; i < 20; i++) {
        zerolist_push_back(&list, &people[i]);
        if (i == 3 || i == 7 || i == 15) {
            printf("  插入第 %d 个节点后，缓冲区大小: %d\n", i + 1, (int)list.max_nodes);
        }
    }

    printf("\n最终缓冲区大小: %d\n", (int)list.max_nodes);
    printf("链表大小: %d\n", (int)zerolist_size(&list));
    printf("链表内容（前 10 个）:\n");

    // 只打印前 10 个
    int count = 0;
    ZEROLIST_FOR_EACH(&list, node)
    {
        if (count++ >= 10) break;
        print_person(node->data);
    }

    // 销毁链表（释放动态分配的缓冲区）
    zerolist_destroy(&list);
    printf("\n链表已销毁，内存已释放\n");
}
#endif

// ===========================================
// 示例 5: 遍历宏的使用
// ===========================================

void example_traversal_macros(void)
{
    printf("\n========== 示例 5: 遍历宏的使用 ==========\n");

    ZEROLIST_DEFINE(list, 20);
    ZEROLIST_INIT(list);

    Person people[8];
    for (int i = 0; i < 8; i++) {
        people[i].id = i + 1;
        sprintf(people[i].name, "Traverse_%d", i + 1);
        zerolist_push_back(&list, &people[i]);
    }

    // 使用 LIST_FOR_EACH（不安全，不能删除节点）
    printf("\n1. ZEROLIST_FOR_EACH 遍历:\n");
    ZEROLIST_FOR_EACH(&list, node)
    {
        Person* p = (Person*)node->data;
        printf("  %s\n", p->name);
    }

    // 使用 LIST_FOR_EACH_SAFE（安全，可以删除节点）
    printf("\n2. LIST_FOR_EACH_SAFE 遍历并删除 ID>5 的节点:\n");
    ZEROLIST_FOR_EACH_SAFE(&list, node, tmp)
    {
        Person* p = (Person*)node->data;
        if (p && p->id > 5) {
            printf("  删除: %s\n", p->name);
            zerolist_remove_ptr(&list, p);
        }
    }

    printf("\n删除后剩余节点:\n");
    ZEROLIST_FOR_EACH(&list, node)
    {
        Person* p = (Person*)node->data;
        printf("  %s\n", p->name);
    }

    zerolist_clear(&list);
}

// ===========================================
// 示例 6: 性能评估
// ===========================================

void example_performance_suite(void)
{
    printf("\n========== 示例 6: 性能评估 ==========\n");

    ZEROLIST_DEFINE(list, PERF_TEST_NODE_COUNT);
    ZEROLIST_INIT(list);

    Person* dataset = (Person*)malloc(sizeof(Person) * PERF_TEST_NODE_COUNT);
    if (!dataset) {
        printf("  无法分配测试数据，跳过此示例\n");
        return;
    }

    for (int i = 0; i < PERF_TEST_NODE_COUNT; ++i) {
        fill_person(&dataset[i], i + 1, "Perf");
    }

    double total_insert_ms   = 0.0;
    double total_traverse_ms = 0.0;
    double total_delete_ms   = 0.0;

    for (int round = 0; round < PERF_TEST_ROUNDS; ++round) {
        zerolist_clear(&list);
        // 插入
        double start = now_ms();
        for (int i = 0; i < PERF_TEST_NODE_COUNT; ++i) {
            if (!zerolist_push_back(&list, &dataset[i])) {
                printf("  Round %d: 插入失败于节点 %d\n", round + 1, i + 1);
                break;
            }
        }
        double insert_ms = now_ms() - start;
        total_insert_ms += insert_ms;

        // 遍历
        start = now_ms();
        ZEROLIST_FOR_EACH(&list, node)
        {
            volatile int sink = ((Person*)node->data)->id;
            (void)sink;
        }
        double traverse_ms = now_ms() - start;
        total_traverse_ms += traverse_ms;

        // 删除
        start = now_ms();
        for (int i = 0; i < PERF_TEST_NODE_COUNT; ++i) {
            if (!zerolist_remove_at(&list, 0)) {
                break;
            }
        }
        double delete_ms = now_ms() - start;
        total_delete_ms += delete_ms;

        printf("  Round %d: 插入 %.3f ms, 遍历 %.3f ms, 删除 %.3f ms\n", round + 1, insert_ms,
               traverse_ms, delete_ms);
    }

    zerolist_clear(&list);

    printf("  平均插入耗时: %.3f ms\n", total_insert_ms / PERF_TEST_ROUNDS);
    printf("  平均遍历耗时: %.3f ms\n", total_traverse_ms / PERF_TEST_ROUNDS);
    printf("  平均删除耗时: %.3f ms\n", total_delete_ms / PERF_TEST_ROUNDS);

    free(dataset);
}

// ===========================================
// 示例 7: 鲁棒性与边界验证
// ===========================================

void example_robustness_suite(void)
{
    printf("\n========== 示例 7: 鲁棒性与边界验证 ==========\n");

    ZEROLIST_DEFINE(list, 4);
    ZEROLIST_INIT(list);
    Person data[4];
    for (int i = 0; i < 4; ++i) {
        fill_person(&data[i], i + 1, "Safe");
    }

    printf("  1) 填满缓冲区:\n");
    for (int i = 0; i < 4; ++i) {
        bool ok = zerolist_push_back(&list, &data[i]);
        printf("     插入 %s -> %s\n", data[i].name, ok ? "PASS" : "FAIL");
    }

    bool overflow = zerolist_push_back(&list, &data[0]);
    printf("  2) 缓冲区满后继续插入（期望失败）: %s\n", overflow ? "FAIL" : "PASS");

    bool invalid_index = zerolist_remove_at(&list, 10);
    printf("  3) 删除越界索引（期望失败）: %s\n", invalid_index ? "FAIL" : "PASS");

    bool remove_existing = zerolist_remove_ptr(&list, &data[0]);
    bool remove_twice    = zerolist_remove_ptr(&list, &data[0]);
    printf("  4) 删除存在节点: %s, 重复删除: %s\n", remove_existing ? "PASS" : "FAIL",
           remove_twice ? "FAIL" : "PASS");

    void* result = zerolist_at(&list, 5);
    printf("  5) 访问越界索引返回 NULL: %s\n", result ? "FAIL" : "PASS");

    zerolist_clear(&list);
    zerolist_clear(&list);
    printf("  6) 多次清空链表: PASS\n");
}

// ===========================================
// 示例 8: 空指针与误操作验证
// ===========================================

void example_null_and_misuse_suite(void)
{
    printf("\n========== 示例 8: 空指针与误操作验证 ==========\n");

    printf("  1) NULL 链表指针处理:\n");
    printf("     zerolist_push_back(NULL, NULL): %s\n",
           zerolist_push_back(NULL, NULL) ? "FAIL" : "PASS");
    printf("     zerolist_remove_ptr(NULL, NULL): %s\n",
           zerolist_remove_ptr(NULL, NULL) ? "FAIL" : "PASS");
    zerolist_clear(NULL);
    zerolist_free_node(NULL, NULL);
    printf("     zerolist_clear/zerolist_free_node(NULL): PASS (未崩溃)\n");

    printf("  2) 未初始化链表:\n");
    Zerolist dummy = { 0 };
    Person   tmp;
    fill_person(&tmp, 1, "Dummy");
    printf("     未初始化 zerolist_push_back: %s\n",
           zerolist_push_back(&dummy, &tmp) ? "FAIL" : "PASS");

    printf("  3) 正常链表上的误操作:\n");
    ZEROLIST_DEFINE(list, 3);
    ZEROLIST_INIT(list);
    Person people[3];
    for (int i = 0; i < 3; ++i) {
        fill_person(&people[i], i + 1, "Err");
        zerolist_push_back(&list, &people[i]);
    }
    bool remove_null = zerolist_remove_ptr(&list, NULL);
    printf("     zerolist_remove_ptr(&list, NULL): %s\n", remove_null ? "FAIL" : "PASS");

    bool del_ok      = zerolist_remove_at(&list, 1);
    bool del_invalid = zerolist_remove_at(&list, 10);
    printf("     删除有效索引: %s, 删除无效索引: %s\n", del_ok ? "PASS" : "FAIL",
           del_invalid ? "FAIL" : "PASS");

    bool second_clear = false;
    zerolist_clear(&list);
    second_clear = true;
    zerolist_clear(&list);
    printf("     重复清空链表: %s\n", second_clear ? "PASS" : "FAIL");
}

// ===========================================
// 示例 9: 随机操作压测（插入 / 查找 / 删除）
// ===========================================
void example_random_ops_suite(void)
{
    printf("\n========== 示例 9: 随机操作压测 ==========\n");

    ZEROLIST_DEFINE(list, RANDOM_OP_NODE_COUNT);
    ZEROLIST_INIT(list);

    Person* pool = (Person*)malloc(sizeof(Person) * RANDOM_OP_NODE_COUNT);
    if (!pool) {
        printf("  无法分配测试数据，跳过此示例\n");
        return;
    }

    // 记录数据初始化时间
    double init_start = now_ms();
    for (int i = 0; i < RANDOM_OP_NODE_COUNT; ++i) {
        fill_person(&pool[i], i + 1, "Rnd");
    }
    double init_time = now_ms() - init_start;

    srand((unsigned int)time(NULL));
    size_t current_size = 0;

    size_t insert_ops = 0, find_ops = 0, delete_ops = 0;
    size_t find_hits   = 0;
    double insert_time = 0.0, find_time = 0.0, delete_time = 0.0;

    // 记录总操作时间
    double total_start = now_ms();
    for (int op = 0; op < RANDOM_OP_ROUNDS; ++op) {
        int action = rand() % 3;  // 0 insert, 1 find, 2 delete

        if (action == 0) {
            if (current_size >= RANDOM_OP_NODE_COUNT) {
                continue;
            }
            ZEROLIST_TYPE idx   = (ZEROLIST_TYPE)(rand() % RANDOM_OP_NODE_COUNT);
            double        start = now_ms();
            bool          ok    = zerolist_push_back(&list, &pool[idx]);
            insert_time += now_ms() - start;
            insert_ops++;
            if (ok) {
                current_size++;
            }
        } else if (action == 1) {
            if (current_size == 0) continue;
            ZEROLIST_TYPE    idx   = (ZEROLIST_TYPE)(rand() % RANDOM_OP_NODE_COUNT);
            double           start = now_ms();
            zerolist_node_t* node  = zerolist_at(&list, idx);
            find_time += now_ms() - start;
            find_ops++;
            if (node) {
                find_hits++;
            }
        } else {
            if (current_size == 0) continue;
            ZEROLIST_TYPE idx   = (ZEROLIST_TYPE)(rand() % current_size);
            double        start = now_ms();
            bool          ok    = zerolist_remove_at(&list, idx);
            delete_time += now_ms() - start;
            delete_ops++;
            if (ok) {
                current_size--;
            }
        }
    }
    double total_op_time = now_ms() - total_start;

    // 记录清理时间
    double cleanup_start = now_ms();
    zerolist_clear(&list);
    free(pool);
    double cleanup_time = now_ms() - cleanup_start;

    printf("  最终链表大小: %d\n", (int)zerolist_size(&list));
    printf("  运行随机操作总数: %d\n", RANDOM_OP_ROUNDS);
    printf("  数据初始化时间: %.3f ms\n", init_time);
    printf("  总操作时间: %.3f ms\n", total_op_time);
    printf("  清理时间: %.3f ms\n", cleanup_time);
    printf("  插入: %zu 次, 总耗时 %.3f ms, 平均 %.3f us\n", insert_ops, insert_time,
           insert_ops ? (insert_time * 1000.0 / insert_ops) : 0.0);
    printf("  查找: %zu 次, 命中 %zu, 总耗时 %.3f ms, 平均 %.3f us\n", find_ops, find_hits,
           find_time, find_ops ? (find_time * 1000.0 / find_ops) : 0.0);
    printf("  删除: %zu 次, 总耗时 %.3f ms, 平均 %.3f us\n", delete_ops, delete_time,
           delete_ops ? (delete_time * 1000.0 / delete_ops) : 0.0);

    // 计算各操作时间占比
    double total_actual_op_time = insert_time + find_time + delete_time;
    if (total_actual_op_time > 0) {
        printf("  插入时间占比: %.2f%%\n", (insert_time / total_actual_op_time) * 100.0);
        printf("  查找时间占比: %.2f%%\n", (find_time / total_actual_op_time) * 100.0);
        printf("  删除时间占比: %.2f%%\n", (delete_time / total_actual_op_time) * 100.0);
    }
}
// ===========================================
// 示例 10: 多次 pop_at 压力测试
// ===========================================

void example_pop_at_stress_test(void)
{
    printf("\n========== 示例 10: 多次 pop_at 压力测试 ==========\n");

#define MAX_NODES 200
    const int ROUNDS = 50;

    ZEROLIST_DEFINE(list, MAX_NODES);
    ZEROLIST_INIT(list);

    Person* pool = (Person*)malloc(sizeof(Person) * MAX_NODES);
    if (!pool) {
        printf("  内存分配失败，跳过此测试\n");
        return;
    }

    // 初始化数据池
    for (int i = 0; i < MAX_NODES; ++i) {
        fill_person(&pool[i], i + 1, "PopAt");
    }

    srand((unsigned int)time(NULL));

    for (int round = 0; round < ROUNDS; ++round) {
        // 1. 清空并重新填充
        zerolist_clear(&list);
        int current_size = 10 + rand() % (MAX_NODES - 10);  // 10~199
        for (int i = 0; i < current_size; ++i) {
            if (!zerolist_push_back(&list, &pool[i])) {
                printf("  Round %d: 插入失败 at %d\n", round, i);
                goto cleanup;
            }
        }

        // 2. 随机弹出直到为空
        while (zerolist_size(&list) > 0) {
            ZEROLIST_TYPE size = zerolist_size(&list);
            // 随机选择策略：0=front, 1=back, 2=random
            int           strategy = rand() % 3;
            ZEROLIST_TYPE idx;

            if (strategy == 0) {
                idx = 0;
            } else if (strategy == 1) {
                idx = size - 1;
            } else {
                idx = (ZEROLIST_TYPE)(rand() % size);
            }

            void* data = zerolist_pop_at(&list, idx);
            if (!data) {
                printf("  ? Round %d: pop_at(%u) 返回 NULL，但 size=%u\n", round, idx, size);
                goto cleanup;
            }

            // 可选：验证 data 是否在预期范围内（这里略）
        }

        // 3. 验证空链表状态
        if (zerolist_size(&list) != 0 || list.head != NULL) {
            printf("  ? Round %d: 清空后链表非空\n", round);
            goto cleanup;
        }
    }

    printf("  ? %d 轮 pop_at 压力测试通过！\n", ROUNDS);

cleanup:
    zerolist_clear(&list);
    free(pool);
}
// ===========================================
// 主函数
// ===========================================

int main(void)
{
    printf("========================================\n");
    printf("  双向循环链表库使用示例\n");
    printf("========================================\n");

    // 示例 1: 静态模式（总是可用）
    example_static_mode();

#if LIST_USE_MALLOC
    // 示例 2: 动态模式
    example_dynamic_mode();
#endif

#if !LIST_USE_MALLOC && ZEROLIST_STATIC_FALLBACK_MALLOC
    // 示例 3: 静态模式 + malloc 回退
    example_static_with_fallback();
#endif

#if !LIST_USE_MALLOC && ZEROLIST_STATIC_DYNAMIC_EXPAND
    // 示例 4: 静态模式 + 动态扩容
    example_dynamic_expand();
#endif

    // 示例 5: 遍历宏的使用
    example_traversal_macros();

    // 示例 6: 性能评估
    example_performance_suite();

    // 示例 7: 鲁棒性与边界验证
    example_robustness_suite();

    // 示例 8: 空指针与误操作验证
    example_null_and_misuse_suite();

    // 示例 9: 随机操作压测
    example_random_ops_suite();
    example_pop_at_stress_test();
    printf("\n========================================\n");
    printf("  所有示例执行完成！\n");
    printf("========================================\n");

    return 0;
}
