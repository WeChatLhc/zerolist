/**
 * @file zerolist.h
 * @brief 高性能双向循环链表库（支持静态/动态内存管理）
 *
 * 本库提供一个高效、类型安全、可配置的双向循环链表实现，适用于嵌入式系统和通用环境。
 * 支持静态分配、动态分配以及动态扩容模式，并提供内存保护与安全遍历机制。
 *
 * @version 2.0
 * @date 2025-11-06
 * @author liuhc
 *
 * @copyright Copyright (c) 2025
 *
 * @par 主要特性
 * - **安全可靠**：
 *   - 安全遍历宏：支持在遍历过程中删除节点
 *   - 内存保护：支持自定义内存池
 *   - 类型安全：编译时配置检查
 * - **开箱即用的示例**：参阅 README.md 与 example/example.c 可快速了解常见场景
 */

/**
 * @defgroup config 配置选项
 *
 * 在包含头文件前定义以下宏来配置库的行为：
 *
 * | 宏定义                        | 默认值 | 说明                          |
 * |------------------------------|--------|-------------------------------|
 * | `ZEROLIST_USE_MALLOC`            | 0      | 1=动态模式, 0=静态模式        |
 * | `ZEROLIST_FAST_ALLOC`     | 0      | 1=启用快速分配（O(1)）        |
 * | `ZEROLIST_SIZE_ENABLE`                | 1      | 1=启用节点计数器 | |
 * `ZEROLIST_STATIC_DYNAMIC_EXPAND` | 0      | 1=启用动态扩容                | |
 * `ZEROLIST_STATIC_FALLBACK_MALLOC`| 0      | 1=启用 malloc 回退            | |
 * `ZEROLIST_TYPE`                  | uint8_t|
 * 节点索引类型（uint8_t/uint16_t/uint32_t） |
 * @{
 */

#ifndef __ZEROLIST_H__
#define __ZEROLIST_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
// ===========================================
//  配置区 - 所有配置选项集中在此处
// ===========================================
//
// 使用说明：
//   在包含此头文件之前定义以下宏来修改配置
//   如果未定义，将使用下方注释中的默认值
//
//   示例：
//     #define ZEROLIST_USE_MALLOC 1
//     #define ZEROLIST_FAST_ALLOC 1
//     #include "zerolist.h"
//
// ===========================================
// 【模式选择】基础配置
// ===========================================

/// @brief 内存分配模式选择
/// @note 0 = 静态模式（使用预分配缓冲区，适合 MCU，默认）
/// @note 1 = 动态模式（使用 malloc/free，适合通用环境）
#ifndef ZEROLIST_USE_MALLOC
#define ZEROLIST_USE_MALLOC 0
#endif

/// @brief 快速分配优化（仅静态模式有效）
/// @note 0 = 普通遍历查找空闲节点（默认）
/// @note 1 = 使用空闲索引栈快速分配（推荐高频插入删除）
#ifndef ZEROLIST_FAST_ALLOC
#define ZEROLIST_FAST_ALLOC 1
#endif

/// @brief 节点数量计数器
/// @note 0 = 禁用（节省内存，获取大小需遍历）
/// @note 1 = 启用（推荐，快速获取链表大小，默认）
#ifndef ZEROLIST_SIZE_ENABLE
#define ZEROLIST_SIZE_ENABLE 1
#endif

// ===========================================
// 【静态模式扩展】高级配置（仅当 ZEROLIST_USE_MALLOC=0 时有效）
// ===========================================

/// @brief 静态模式下的 malloc 回退机制
/// @note 0 = 禁用（缓冲区满时插入失败，默认，适合严格静态环境）
/// @note 1 = 启用（缓冲区满时自动使用 malloc，混合模式）
/// @warning 启用后，链表可能同时包含静态和动态分配的节点
#ifndef ZEROLIST_STATIC_FALLBACK_MALLOC
#define ZEROLIST_STATIC_FALLBACK_MALLOC 0
#endif

/// @brief 静态模式下的动态扩容机制
/// @note 0 = 禁用（使用静态预分配缓冲区，默认）
/// @note 1 = 启用（初始化和扩容都使用 malloc/realloc）
/// @warning 启用后，node_buf 和 free_stack 由库管理，需要调用 zerolist_destroy
/// 释放
/// @warning 与 ZEROLIST_STATIC_FALLBACK_MALLOC 互斥，优先使用动态扩容
#ifndef ZEROLIST_STATIC_DYNAMIC_EXPAND
#define ZEROLIST_STATIC_DYNAMIC_EXPAND 1
#endif

// ===========================================
// 【类型配置】
// ===========================================

/// @brief 节点索引和大小计数的数据类型
/// @note 可选：uint8_t, uint16_t, uint32_t, size_t 等
/// @note 默认：uint16_t（最大支持 65535 个节点）
/// @note 根据实际需求选择合适的类型以平衡内存和容量
#ifndef ZEROLIST_TYPE
#define ZEROLIST_TYPE uint16_t
#endif

// ===========================================
// 【自定义内存池】可选配置
// ===========================================

/// @brief 自定义内存分配函数
/// @note 如果未定义，使用标准 malloc
/// @example #define ZEROLIST_MALLOC(size) my_pool_alloc(size)
#ifndef ZEROLIST_MALLOC
#define ZEROLIST_MALLOC(size) (malloc(size))
#endif

/// @brief 自定义内存释放函数
/// @note 如果未定义，使用标准 free
/// @example #define ZEROLIST_FREE(ptr) my_pool_free(ptr)
#ifndef ZEROLIST_FREE
#define ZEROLIST_FREE(ptr) (free(ptr))
#endif

/// @brief 自定义内存重分配函数
/// @note 如果未定义，使用标准 realloc
/// @example #define ZEROLIST_REALLOC(ptr, size) my_pool_realloc(ptr, size)
#ifndef ZEROLIST_REALLOC
#define ZEROLIST_REALLOC(ptr, size) (realloc(ptr, size))
#endif

// ===========================================
// 模式互斥检查
// ===========================================
#if (ZEROLIST_USE_MALLOC && ZEROLIST_FAST_ALLOC)
#error "[zerolist error] Invalid config: ZEROLIST_FAST_ALLOC requires static mode."
#endif

#if (ZEROLIST_USE_MALLOC && ZEROLIST_STATIC_FALLBACK_MALLOC)
#error "[zerolist error] Invalid config: ZEROLIST_STATIC_FALLBACK_MALLOC requires static mode."
#endif

#if (ZEROLIST_USE_MALLOC && ZEROLIST_STATIC_DYNAMIC_EXPAND)
#error "[zerolist error] Invalid config: ZEROLIST_STATIC_DYNAMIC_EXPAND requires static mode."
#endif

#if (ZEROLIST_STATIC_DYNAMIC_EXPAND && ZEROLIST_STATIC_FALLBACK_MALLOC)
#error "[zerolist error] Invalid config: ZEROLIST_STATIC_DYNAMIC_EXPAND and "                  \
    "ZEROLIST_STATIC_FALLBACK_MALLOC are mutually exclusive."
#endif

// ===========================================
// 数据结构定义
// ===========================================

/**
 * @struct zerolist_node
 * @brief 链表节点结构体
 *
 * 双向循环链表的基本节点单元，包含数据指针和前后节点指针
 */
typedef struct zerolist_node
{
    void*                 data;  ///< 节点数据指针，指向用户数据
    struct zerolist_node* prev;  ///< 前驱节点指针
    struct zerolist_node* next;  ///< 后继节点指针
#if !ZEROLIST_USE_MALLOC
    struct
    {
        uint16_t in_use : 1;  ///< 节点使用标记，1表示正在使用
        uint16_t index
            : ((sizeof(ZEROLIST_TYPE) << 3) - 1);  ///< 节点在缓冲区中的下标（仅静态模式有效）
    } flags;
#endif
} zerolist_node_t;

/**
 * @struct Zerolist
 * @brief 链表结构体
 *
 * 双向循环链表的主结构，包含链表头尾指针和节点管理信息
 */
typedef struct Zerolist
{
#if ZEROLIST_SIZE_ENABLE
    ZEROLIST_TYPE size;  ///< 当前链表中的节点数量
#endif
    zerolist_node_t* head;  ///< 链表头节点指针
    zerolist_node_t* tail;  ///< 链表尾节点指针
#if !ZEROLIST_USE_MALLOC
    zerolist_node_t* node_buf;   ///< 节点缓冲区指针（静态模式）
    ZEROLIST_TYPE    max_nodes;  ///< 最大节点数量限制
#if ZEROLIST_FAST_ALLOC
    ZEROLIST_TYPE  free_top;    ///< 空闲节点栈的栈顶索引
    ZEROLIST_TYPE* free_stack;  ///< 空闲节点索引栈，用于快速分配
#endif
#endif
} Zerolist;

// ===========================================
// 宏定义（声明与初始化）
// ===========================================

#if !ZEROLIST_USE_MALLOC  // ---------- 静态模式 ----------
#if ZEROLIST_STATIC_DYNAMIC_EXPAND
/**
 * @def ZEROLIST_DEFINE(name, _max_nodes)
 * @brief 定义动态扩容链表（使用 malloc/realloc）
 *
 * 此宏会创建一个链表对象，缓冲区通过 malloc 动态分配。
 * 适用于需要动态扩展内存池的场景。
 *
 * @param name 链表变量名
 * @param _max_nodes 初始最大节点数量
 *
 * @note 使用此宏后需要调用 ZEROLIST_INIT(name) 进行初始化
 * @note 缓冲区满时会自动扩容，无需手动管理
 * @note 使用完毕后需要调用 zerolist_destroy(name) 释放内存
 */
#define ZEROLIST_DEFINE(name, _max_nodes)                                       \
    static Zerolist name = {                                                    \
        .head = NULL, .tail = NULL, .node_buf = NULL, .max_nodes = (_max_nodes) \
    }
// 在 .h 中声明（extern）
#define ZEROLIST_DECLARE(name) extern Zerolist name;
/**
 * @def ZEROLIST_INIT(name)
 * @brief 初始化动态扩容链表
 */
#define ZEROLIST_INIT(name) list_init_dynamic_expand(&(name), (name).max_nodes)

#elif ZEROLIST_FAST_ALLOC
/**
 * @def ZEROLIST_DEFINE(name, _max_nodes)
 * @brief 定义并初始化静态链表（快速分配模式）
 *
 * 此宏会创建一个静态链表对象，包括节点缓冲区、空闲栈等所需资源。
 * 适用于静态内存分配模式，使用空闲栈进行快速节点分配。
 *
 * @param name 链表变量名
 * @param _max_nodes 最大节点数量
 *
 * @note 使用此宏后需要调用 ZEROLIST_INIT(name) 进行初始化
 */
#define ZEROLIST_DEFINE(name, _max_nodes)                     \
    static zerolist_node_t name##_buf[(_max_nodes)];          \
    static ZEROLIST_TYPE   name##_free_stack[(_max_nodes)];   \
    static Zerolist        name = { .head       = NULL,       \
                                    .tail       = NULL,       \
                                    .node_buf   = name##_buf, \
                                    .max_nodes  = _max_nodes, \
                                    .free_top   = _max_nodes, \
                                    .free_stack = name##_free_stack }
#define ZEROLIST_DECLARE(name) extern Zerolist name;
/**
 * @def ZEROLIST_INIT(name)
 * @brief 初始化静态链表（快速分配模式）
 *
 * 初始化由 ZEROLIST_DEFINE 定义的静态链表。
 */
#define ZEROLIST_INIT(name) \
    zerolist_init_expand(&(name), name.node_buf, name.free_stack, name.max_nodes)

#else  // ---------- 静态普通分配模式（无快速栈） ----------
/**
 * @def ZEROLIST_DEFINE(name, _max_nodes)
 * @brief 定义并初始化静态链表（普通分配模式）
 *
 * 此宏会创建一个静态链表对象，包括节点缓冲区。
 * 适用于静态内存分配模式，使用遍历方式查找空闲节点。
 *
 * @param name 链表变量名
 * @param _max_nodes 最大节点数量
 *
 * @note 使用此宏后需要调用 ZEROLIST_INIT(name) 进行初始化
 */
#define ZEROLIST_DEFINE(name, _max_nodes)                                                    \
    static zerolist_node_t name##_buf[(_max_nodes)];                                         \
    static Zerolist        name = {                                                          \
               .head = NULL, .tail = NULL, .node_buf = name##_buf, .max_nodes = (_max_nodes) \
    }
#define ZEROLIST_DECLARE(name) extern Zerolist name;
/**
 * @def ZEROLIST_INIT(name)
 * @brief 初始化静态链表（普通模式）
 */
#define ZEROLIST_INIT(name)    zerolist_init_expand(&(name), name.node_buf, name.max_nodes)
#endif  // ZEROLIST_FAST_ALLOC

#else  // ---------- 动态模式（malloc/free） ----------
/**
 * @def ZEROLIST_DEFINE(name, _max_nodes)
 * @brief 定义动态链表（malloc模式）
 *
 * 此宏仅声明一个链表变量，实际内存通过malloc动态分配。
 *
 * @param name 链表变量名
 * @param _max_nodes 最大节点数量（动态模式下此参数无效）
 */
#define ZEROLIST_DEFINE(name, _max_nodes) static Zerolist name
#define ZEROLIST_DECLARE(name)            extern Zerolist name;
/**
 * @def ZEROLIST_INIT(name)
 * @brief 初始化动态链表
 */
#define ZEROLIST_INIT(name)               list_init_dynamic(&(name))
#endif  // ZEROLIST_USE_MALLOC

// ===========================================
// 遍历宏（统一接口 - 适用于所有模式）
// ===========================================

/**
 * @def ZEROLIST_FOR_EACH(list_ptr, node_var)
 * @brief 链表遍历宏（不安全版本，统一接口）
 *
 * 用于遍历链表中的所有节点，循环体内可以访问每个节点的数据。
 * 此宏适用于所有模式（静态/动态/混合）。
 *
 * @param list_ptr 指向LinkedList结构体的指针
 * @param node_var 循环变量名，类型为ListNode*
 *
 * @warning 不要在遍历过程中修改链表结构（删除节点），否则可能导致未定义行为
 *
 * @example
 * @code
 * ZEROLIST_FOR_EACH(&my_list, node) {
 *     if (node->data) {
 *         printf("Data: %p\n", node->data);
 *     }
 * }
 * @endcode
 */
#define ZEROLIST_FOR_EACH(list_ptr, node_var)                                           \
    if ((list_ptr)->head != NULL)                                                       \
        for (zerolist_node_t* node_var = (list_ptr)->head, *__first = (list_ptr)->head; \
             node_var != NULL; node_var = (node_var->next == __first ? NULL : node_var->next))

/**
 * @def ZEROLIST_FOR_EACH_SAFE(list_ptr, node_var, tmp_var)
 * @brief 链表遍历宏（安全版本，统一接口）
 *
 * 用于遍历链表中的所有节点，循环体内可以安全地删除当前节点。
 * 使用临时变量保存下一个节点，避免在删除节点时出现问题。
 * 此宏适用于所有模式（静态/动态/混合）。
 *
 * @param list_ptr 指向LinkedList结构体的指针
 * @param node_var 循环变量名，类型为ListNode*
 * @param tmp_var 临时变量名，用于保存下一个节点指针
 *
 * @note 推荐在需要删除节点的场景中使用此宏
 *
 * @example
 * @code
 * ZEROLIST_FOR_EACH_SAFE(&my_list, node, tmp) {
 *     if (should_remove(node->data)) {
 *         zerolist_remove_ptr(&my_list, node->data);
 *     }
 * }
 * @endcode
 */
#define ZEROLIST_FOR_EACH_SAFE(list_ptr, node_var, tmp_var)                           \
    if ((list_ptr)->head != NULL)                                                     \
        for (zerolist_node_t* node_var = (list_ptr)->head, *tmp_var = node_var->next, \
                              *__first  = (list_ptr)->head;                           \
             node_var != NULL; node_var = (tmp_var == __first ? NULL : tmp_var),      \
                              tmp_var   = (node_var ? node_var->next : NULL))

// ===========================================
// 函数声明
// ===========================================

#if ZEROLIST_USE_MALLOC
/**
 * @brief 初始化动态链表（使用malloc分配内存）
 *
 * 为链表分配动态内存，初始化链表结构。
 * 适用于通用Linux环境，使用malloc/free管理内存。
 *
 * @param list 指向LinkedList结构体的指针
 * @return true 初始化成功
 * @return false 初始化失败（内存分配失败）
 *
 * @note 使用动态模式时，需要在使用完毕后调用 zerolist_free_node() 或
 * zerolist_clear() 释放节点内存
 */
bool list_init_dynamic(Zerolist* list);
#else
/**
 * @brief 初始化静态链表（使用预分配缓冲区）
 *
 * 使用预分配的缓冲区初始化链表结构。
 * 适用于MCU嵌入式环境，避免动态内存分配。
 *
 * @param list 指向LinkedList结构体的指针
 * @param buf 节点缓冲区指针
#if ZEROLIST_FAST_ALLOC
 * @param free_stack 空闲节点索引栈指针（快速分配模式需要）
#endif
 * @param max_nodes 最大节点数量
 *
 * @note 静态模式下，内存由用户管理，无需释放
 * @note 当 ZEROLIST_FAST_ALLOC=1 时，需要提供 free_stack 参数
 * @note 当 ZEROLIST_FAST_ALLOC=0 时，不需要 free_stack 参数
 */
void zerolist_init_expand(Zerolist* list, zerolist_node_t* buf,
#if ZEROLIST_FAST_ALLOC
                          ZEROLIST_TYPE* free_stack,
#endif
                          ZEROLIST_TYPE max_nodes);

#if ZEROLIST_STATIC_DYNAMIC_EXPAND
/**
 * @brief 初始化动态扩容链表（使用 malloc 分配初始缓冲区）
 *
 * 使用 malloc 分配初始缓冲区，支持后续通过 realloc 扩容。
 * 适用于需要动态扩展内存池的场景。
 *
 * @param list 指向LinkedList结构体的指针
 * @param initial_size 初始缓冲区大小（节点数量）
 * @return true 初始化成功
 * @return false 初始化失败（内存分配失败）
 *
 * @note 使用完毕后需要调用 zerolist_destroy() 释放内存
 */
bool list_init_dynamic_expand(Zerolist* list, ZEROLIST_TYPE initial_size);

#endif
#endif

// ===========================================
// 基本操作（统一接口 - 适用于所有模式）
// ===========================================

/**
 * @brief 在链表头部插入节点（统一接口）
 *
 * 在链表的头部位置插入一个新节点，节点数据为data。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param data 要插入的数据指针
 * @return true 插入成功
 * @return false 插入失败（内存不足或参数无效）
 *
 * @note 插入后，新节点成为链表的头节点
 */
bool zerolist_push_front(Zerolist* list, void* data);

/**
 * @brief 在链表尾部插入节点（统一接口）
 *
 * 在链表的尾部位置插入一个新节点，节点数据为data。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param data 要插入的数据指针
 * @return true 插入成功
 * @return false 插入失败（内存不足或参数无效）
 *
 * @note 插入后，新节点成为链表的尾节点
 */
bool zerolist_push_back(Zerolist* list, void* data);

/**
 * @brief 在指定数据节点之前插入新节点（统一接口）
 *
 * 在链表中查找包含target_data的节点，然后在该节点之前插入包含new_data的新节点。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param target_data 目标节点的数据指针
 * @param new_data 要插入的新节点数据指针
 * @return true 插入成功
 * @return false 插入失败（未找到目标节点或内存不足）
 *
 * @note 如果链表中存在多个相同的target_data，只在第一个匹配的节点前插入
 */
bool zerolist_insert_before(Zerolist* list, void* target_data, void* new_data);

// ===========================================
// 删除操作（统一接口 - 适用于所有模式）
// ===========================================

/**
 * @brief 删除包含指定数据的节点（统一接口）
 *
 * 在链表中查找并删除包含data的节点。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param data 要删除的数据指针
 * @return true 删除成功
 * @return false 删除失败（未找到数据或参数无效）
 *
 * @note 如果链表中存在多个相同的data，只删除第一个匹配的节点
 */
bool zerolist_remove_ptr(Zerolist* list, void* data);

/**
 * @brief 使用自定义比较函数删除节点（统一接口）
 *
 * 使用用户提供的比较函数查找并删除匹配的节点。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param data 要匹配的数据指针
 * @param cmp_func 比较函数指针，返回true表示匹配
 * @return true 删除成功
 * @return false 删除失败（未找到匹配节点或参数无效）
 *
 * @note 比较函数原型：bool cmp_func(const void* list_data, const void*
 * target_data)
 */
bool zerolist_remove_if(Zerolist* list, void* data, bool (*cmp_func)(const void*, const void*));

/*
 * 从指定索引位置弹出节点数据
 *
 * 参数:
 *   list - 指向零列表结构的指针
 *   index - 要弹出的节点索引位置
 *
 * 返回值:
 *   成功时返回被弹出节点的数据指针，失败时返回NULL
 *   失败情况包括：
 *   - 列表为空或头节点为空
 *   - 索引超出范围（当启用ZEROLIST_SIZE_ENABLE时）
 *   - 遍历到循环列表的头部（表示索引无效）
 */
void* zerolist_pop_at(Zerolist* list, ZEROLIST_TYPE index);

/*
 * 从零列表尾部弹出节点数据
 *
 * 该函数会移除列表尾部的节点，并返回该节点中存储的数据。
 * 如果列表为空或不存在尾部节点，则返回NULL。
 *
 * @param list 指向零列表的指针，不能为NULL
 * @return 返回被弹出节点中存储的数据指针，如果操作失败则返回NULL
 */
void* zerolist_pop_back(Zerolist* list);

/*
 * 从循环双向链表头部移除节点并返回其数据
 *
 * @param list 指向zerolist链表的指针，不能为NULL
 * @return 返回被移除节点中存储的数据指针，如果链表为空则返回NULL
 *
 * 注意事项：
 * 1. 函数会处理链表为空或只有一个节点的情况
 * 2. 在ZEROLIST_SIZE_ENABLE定义时会自动更新链表大小
 * 3. 节点内存释放由zerolist_free_node处理
 */
void* zerolist_pop_front(Zerolist* list);

/**
 * @brief 根据索引删除节点（统一接口）
 *
 * 删除链表中指定索引位置的节点。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param index 节点索引（从0开始）
 * @return true 删除成功
 * @return false 删除失败（索引越界或参数无效）
 *
 * @note 索引从0开始，0表示第一个节点
 */
bool zerolist_remove_at(Zerolist* list, ZEROLIST_TYPE index);

// ===========================================
// 访问 / 查找（统一接口 - 适用于所有模式）
// ===========================================

/**
 * @brief 根据索引获取节点数据（统一接口）
 *
 * 获取链表中指定索引位置的节点数据。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param index 节点索引（从0开始）
 * @return void* 节点数据指针，失败返回NULL
 *
 * @note 索引从0开始，0表示第一个节点
 */
void* zerolist_at(Zerolist* list, ZEROLIST_TYPE index);

/**
 * @brief 根据数据指针查找节点（统一接口）
 *
 * 在链表中查找包含target_addr数据的节点。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param target_addr 目标数据指针
 * @return zerolist_node * 找到的节点指针，未找到返回NULL
 *
 * @note 此函数通过指针比较查找节点，不比较数据内容
 */
zerolist_node_t* zerolist_find(Zerolist* list, const void* target_addr);

/**
 * @brief 使用自定义比较函数查找节点（统一接口）
 *
 * 在链表中使用比较函数查找匹配的节点。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param target_data 目标数据指针
 * @param cmp_func 比较函数指针
 * @return zerolist_node * 找到的节点指针，未找到返回NULL
 */
zerolist_node_t* zerolist_search(Zerolist* list, const void* target_data,
                                 bool (*cmp_func)(const void*, const void*));

/**
 * @brief 遍历链表并对每个节点执行回调函数（统一接口）
 *
 * 遍历链表中的所有节点，对每个节点的数据调用回调函数。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param callback 回调函数指针，接收void*类型的节点数据
 *
 * @note 回调函数原型：void callback(void* data)
 */
void zerolist_foreach(Zerolist* list, void (*callback)(void* data));

// ===========================================
// 工具函数（统一接口 - 适用于所有模式）
// ===========================================

/**
 * @brief 反转链表（统一接口）
 *
 * 将链表中的所有节点顺序反转，原头节点变为尾节点，原尾节点变为头节点。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 *
 * @note 反转操作会修改链表的内部结构
 */
void zerolist_reverse(Zerolist* list);

/**
 * @brief 清空链表（统一接口）
 *
 * 删除链表中的所有节点，但不释放链表结构本身。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 *
 * @note 清空后链表为空，可以继续使用
 * @note 对于动态扩容模式，建议使用 zerolist_destroy() 来同时释放缓冲区
 */
void zerolist_clear(Zerolist* list);

/**
 * @brief 销毁链表（统一接口）
 *
 * 统一的链表销毁接口，适用于所有模式：
 * - 动态模式（ZEROLIST_USE_MALLOC）：清空所有节点（每个节点单独分配）
 * - 动态扩容模式（LIST_STATIC_DYNAMIC_EXPAND）：清空节点 + 释放缓冲区
 * - 纯静态模式：清空节点 + 重置状态（缓冲区由用户管理，可重新使用）
 *
 * @param list 指向LinkedList结构体的指针
 *
 * @note 调用此函数后，应使用 zerolist_reinit() 重新初始化链表
 * @note 对于动态扩容模式，此函数会释放通过 malloc/realloc 分配的缓冲区
 * @note 对于纯静态模式，此函数会重置所有节点状态和空闲栈，使其可以重新使用
 */
void zerolist_destroy(Zerolist* list);

/**
 * @brief 重新初始化链表（统一接口）
 *
 * 在调用 zerolist_destroy() 后，使用此函数重新初始化链表。
 * 此函数会根据模式自动处理：
 * - 动态模式：重新初始化链表结构
 * - 动态扩容模式：重新分配缓冲区并初始化
 * - 纯静态模式：重新初始化链表状态（使用原有缓冲区）
 *
 * @param list 指向LinkedList结构体的指针
 * @param initial_size 初始大小（仅动态扩容模式需要，其他模式忽略此参数）
 * @return true 初始化成功
 * @return false 初始化失败（参数无效或内存分配失败）
 *
 * @note 此函数必须在 zerolist_destroy() 之后调用
 * @note 对于纯静态模式，此函数会重新初始化状态，使用原有的缓冲区
 * @note 对于动态扩容模式，必须提供有效的 initial_size 参数
 *
 * @example
 * @code
 * @endcode
 */
bool zerolist_reinit(Zerolist* list, ZEROLIST_TYPE initial_size);

/**
 * @brief 获取链表节点数量（统一接口）
 *
 * 返回当前链表中节点的数量。
 * 此接口适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @return ZEROLIST_TYPE 节点数量，如果ZEROLIST_SIZE_ENABLE为0则返回0
 *
 * @note 如果ZEROLIST_SIZE_ENABLE为0，此函数可能返回不准确的值
 */
ZEROLIST_TYPE zerolist_size(Zerolist* list);

/**
 * @brief 释放单个节点（统一接口）
 *
 * 释放链表中的单个节点。这是统一的节点释放接口，适用于所有模式（静态/动态/混合）。
 *
 * @param list 指向LinkedList结构体的指针
 * @param node 要释放的节点指针
 *
 * @note 这是对外提供的公共接口，统一用于释放节点
 * @note 静态模式下，节点会被标记为空闲并加入空闲栈
 * @note 动态模式下，节点内存会被释放
 *
 * @example
 * @code
 * zerolist_node * node = zerolist_find(&my_list, target_data);
 * if (node) {
 *     zerolist_free_node(&my_list, node);
 * }
 * @endcode
 */
void zerolist_free_node(Zerolist* list, zerolist_node_t* node);
#ifdef __cplusplus
}
#endif
#endif  // __ZEROLIST_H__
