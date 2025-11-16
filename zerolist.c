/**
 * @file zerolist.c
 * @author lhc (liuhc_lhc@163.com)
 * @brief 双向循环链表库的实现（支持静态/动态内存管理）
 * @version 2.0
 * @date 2025-11-14
 *
 * @copyright Copyright (c) 2025
 *
 * @note 使用说明与完整示例请参阅 README.md 与 example/example.c
 ****/

#include "zerolist.h"
#include <string.h>

// ===========================================
// 内部宏定义（局部使用，不对外暴露）
// ===========================================

// 节点大小常量（编译时计算，用于性能优化）
#define _ZEROLIST_NODE_SIZE (sizeof(zerolist_node_t))
// 检查节点是否在使用
#define _ZEROLIST_NODE_IS_IN_USE(node) ((node) && (node)->flags.in_use)

// 获取节点存储的下标（仅静态模式有效，已废弃但保留用于兼容）
#define _ZEROLIST_NODE_GET_INDEX(node) ((node) ? (node)->flags.index : 0)

// 设置节点为使用状态并存储下标（静态模式）
#define _ZEROLIST_NODE_SET_IN_USE(node, idx)             \
    do {                                                 \
        if (node) {                                      \
            (node)->flags.in_use = 1;                    \
            (node)->flags.index  = (ZEROLIST_TYPE)(idx); \
        }                                                \
    } while (0)

// 设置节点为使用状态（动态模式，只设置in_use位，不存储下标）
#define _ZERO_ZEROLIST_NODE_SET_IN_USE_SIMPLE(node) \
    do {                                            \
        if (node) {                                 \
            (node)->flags.in_use = 1;               \
            (node)->flags.index  = 0;               \
        }                                           \
    } while (0)

// 设置节点为空闲状态
#define _ZEROLIST_NODE_SET_FREE(node) \
    do {                              \
        if (node) {                   \
            (node)->flags.in_use = 0; \
            (node)->flags.index  = 0; \
        }                             \
    } while (0)

// ===========================================
// 前向声明
// ===========================================

#if ZEROLIST_STATIC_DYNAMIC_EXPAND
static bool _zerolist_expand_buffer(Zerolist* list, ZEROLIST_TYPE new_size);
#endif

// ===========================================
// 内部节点管理函数
// ===========================================

/**
 * @brief Determine if the node is statically allocated
 * @param list linked list pointer
 * @param node node pointer
 * @return true Node is in the static buffer
 * @return false Node is dynamically allocated
 */
static inline bool _zerolist_is_static_node(Zerolist* list, zerolist_node_t* node)
{
#if !ZEROLIST_USE_MALLOC
    if (!list || !list->node_buf || !node) return false;
    uintptr_t base = (uintptr_t)list->node_buf;
    uintptr_t addr = (uintptr_t)node;
    uintptr_t end  = base + (uintptr_t)list->max_nodes * (uintptr_t)_ZEROLIST_NODE_SIZE;
    return (addr >= base && addr < end);
#else
    (void)list;
    (void)node;
    return false;
#endif
}

#if !ZEROLIST_USE_MALLOC

/*
 * 计算节点在零列表中的索引位置
 *
 * 参数:
 *   list - 指向零列表结构的指针
 *   node - 指向要计算索引的节点的指针
 *
 * 返回值:
 *   节点在零列表中的索引位置，如果参数无效或节点不在列表中则返回0
 */
static inline ZEROLIST_TYPE _zerolist_calc_node_index(Zerolist* list, zerolist_node_t* node)
{
    if (!list || !node || !list->node_buf) return 0;
    uintptr_t base = (uintptr_t)list->node_buf;
    uintptr_t addr = (uintptr_t)node;
    if (addr < base) return 0;
    uintptr_t offset = addr - base;
    return (ZEROLIST_TYPE)(offset / (uintptr_t)_ZEROLIST_NODE_SIZE);
}

// ===========================================
// 内部宏：简化节点分配和释放的重复代码
// ===========================================

// 从静态缓冲区中查找空闲节点（快速分配模式）
#define _ZEROLIST_ALLOC_FROM_STACK(list, node, idx)          \
    do {                                                     \
        if ((list)->free_top > 0) {                          \
            (idx)  = (list)->free_stack[--(list)->free_top]; \
            (node) = &(list)->node_buf[(idx)];               \
        }                                                    \
    } while (0)

// 从静态缓冲区中查找空闲节点（普通分配模式）
#define _ZEROLIST_ALLOC_FROM_SEARCH(list, node, idx)               \
    do {                                                           \
        for (ZEROLIST_TYPE i = 0; i < (list)->max_nodes; i++) {    \
            if (!_ZEROLIST_NODE_IS_IN_USE(&(list)->node_buf[i])) { \
                (node) = &(list)->node_buf[i];                     \
                (idx)  = i;                                        \
                break;                                             \
            }                                                      \
        }                                                          \
    } while (0)

// 根据配置选择分配方式
#if ZEROLIST_FAST_ALLOC
#define _ZEROLIST_TRY_ALLOC_STATIC(list, node, idx) _ZEROLIST_ALLOC_FROM_STACK(list, node, idx)
#else
#define _ZEROLIST_TRY_ALLOC_STATIC(list, node, idx) _ZEROLIST_ALLOC_FROM_SEARCH(list, node, idx)
#endif

// 释放节点到静态缓冲区（快速分配模式）
#define _ZEROLIST_FREE_TO_STACK(list, node, idx)                                    \
    do {                                                                            \
        if ((list) && (list)->free_stack && (list)->free_top < (list)->max_nodes) { \
            if ((idx) < (list)->max_nodes) {                                        \
                (list)->free_stack[(list)->free_top++] = (idx);                     \
            }                                                                       \
        }                                                                           \
    } while (0)

// 释放节点到静态缓冲区（统一接口，自动选择模式）
#if ZEROLIST_FAST_ALLOC
#define _ZEROLIST_FREE_STATIC_NODE(list, node, idx) \
    do {                                            \
        _ZEROLIST_NODE_SET_FREE(node);              \
        (node)->data = NULL;                        \
        (node)->prev = (node)->next = (node);       \
        _ZEROLIST_FREE_TO_STACK(list, node, idx);   \
    } while (0)
#else
#define _ZEROLIST_FREE_STATIC_NODE(list, node, idx) \
    do {                                            \
        _ZEROLIST_NODE_SET_FREE(node);              \
        (node)->data = NULL;                        \
        (node)->prev = (node)->next = (node);       \
    } while (0)
#endif

#endif
ZEROLIST_TYPE zerolist_get_max_nodes(Zerolist* list)
{
    if (!list) return 0;

#if ZEROLIST_USE_MALLOC
    // 动态分配模式直接返回配置的最大节点数
    return 0;
#else
    // 静态缓冲区模式
    if (!list->node_buf) return 0;

#if ZEROLIST_STATIC_DYNAMIC_EXPAND
    if (list->max_nodes == 0 || list->max_nodes == (ZEROLIST_TYPE)-1) {
        if (list->max_nodes == 0) {
            return (ZEROLIST_TYPE)((uintptr_t)(list->node_buf + list->max_nodes)
                                   - (uintptr_t)list->node_buf)
                   / _ZEROLIST_NODE_SIZE;
        }
        return 0;  // 无效状态返回0
    }
#endif

    return list->max_nodes;
#endif
}

/**
 *Allocate linked list nodes
 *
 *Supports multiple allocation strategies based on compilation configuration:
 *-ZEROLIST_USE_MALLOC: Use dynamic memory allocation
 *-ZEROLIST_FAST_ALLOC: Use fast allocation stack
 *-ZEROLIST_STATIC_DYNAMIC_EXPAND: Dynamically expand the static buffer when it is
 *full -ZEROLIST_STATIC_FALLBACK_MALLOC: Fall back to malloc when the static buffer
 *is full
 *
 * @param list linked list pointer, containing node buffer and allocation status
 *information
 * @return Returns the allocated node pointer on success, NULL on failure
 */
static inline zerolist_node_t* _zerolist_alloc_node(Zerolist* list)
{
#if ZEROLIST_USE_MALLOC
    // 动态模式：直接使用 malloc 分配
    zerolist_node_t* node = (zerolist_node_t*)ZEROLIST_MALLOC(_ZEROLIST_NODE_SIZE);
    if (!node) return NULL;
    node->next = node->prev = node;
    node->data              = NULL;
    return node;
#else
    // 静态模式：从缓冲区分配
    zerolist_node_t* node = NULL;
    ZEROLIST_TYPE    idx  = 0;
    if (!list || !list->node_buf || list->max_nodes == 0) {
        return NULL;
    }

    // 尝试从缓冲区分配节点
    _ZEROLIST_TRY_ALLOC_STATIC(list, node, idx);

#if ZEROLIST_STATIC_DYNAMIC_EXPAND
    // 动态扩容模式：如果分配失败，尝试扩容
    if (!node) {
        ZEROLIST_TYPE new_size = list->max_nodes << 1;
        if (new_size <= list->max_nodes) {
            if (list->max_nodes == ((ZEROLIST_TYPE)-1)) {
                return NULL;
            }
            new_size = (ZEROLIST_TYPE)(-1);
        }
        if (!_zerolist_expand_buffer(list, new_size)) return NULL;
        // 扩容后再次尝试分配
        _ZEROLIST_TRY_ALLOC_STATIC(list, node, idx);
        if (!node) return NULL;
    }
#elif ZEROLIST_STATIC_FALLBACK_MALLOC
    // 回退到 malloc 模式：如果分配失败，使用 malloc
    if (!node) {
        node = (zerolist_node_t*)ZEROLIST_MALLOC(_ZEROLIST_NODE_SIZE);
        if (!node) return NULL;
        node->next = node->prev = node;
        _ZERO_ZEROLIST_NODE_SET_IN_USE_SIMPLE(node);
        node->data = NULL;
        return node;
    }
#else
    // 纯静态模式：分配失败直接返回
    if (!node) return NULL;
#endif

    // 初始化节点
    node->prev = node->next = node;
    _ZEROLIST_NODE_SET_IN_USE(node, idx);
    node->data = NULL;
    return node;
#endif
}

/**
 * @brief 释放节点（统一接口，对外提供的公共接口）
 * @param list 链表指针
 * @param node 要释放的节点指针
 */
void zerolist_free_node(Zerolist* list, zerolist_node_t* node)
{
    if (!node) return;

#if ZEROLIST_USE_MALLOC
    // 动态模式：直接释放内存
    ZEROLIST_FREE(node);
#else
    // 静态模式：释放到缓冲区
#if ZEROLIST_STATIC_FALLBACK_MALLOC
    // 回退模式：需要判断是静态节点还是动态节点
    if (_zerolist_is_static_node(list, node)) {
        ZEROLIST_TYPE idx = _zerolist_calc_node_index(list, node);
        _ZEROLIST_FREE_STATIC_NODE(list, node, idx);
    } else {
        // 动态分配的节点，直接释放
        ZEROLIST_FREE(node);
    }
#else
    // 纯静态模式：直接释放到缓冲区
    ZEROLIST_TYPE idx = _zerolist_calc_node_index(list, node);
    _ZEROLIST_FREE_STATIC_NODE(list, node, idx);
#endif
#endif
}

// ===========================================
//  初始化
// ===========================================

#if ZEROLIST_USE_MALLOC

/**
 * @brief 初始化动态链表（malloc 模式）
 */
bool list_init_dynamic(Zerolist* list)
{
    if (!list) return false;
    memset(list, 0, sizeof(Zerolist));
#if ZEROLIST_SIZE_ENABLE
    list->size = 0;
#endif
    return true;
}

#endif  // ZEROLIST_USE_MALLOC

// ===========================================
//  统一销毁接口（适用于所有模式）
// ===========================================

void zerolist_destroy(Zerolist* list)
{
    if (!list) return;
    zerolist_clear(list);

#if ZEROLIST_USE_MALLOC

#elif ZEROLIST_STATIC_DYNAMIC_EXPAND
    if (list->node_buf) {
        ZEROLIST_FREE(list->node_buf);
        list->node_buf = NULL;
    }
#if ZEROLIST_FAST_ALLOC
    if (list->free_stack) {
        ZEROLIST_FREE(list->free_stack);
        list->free_stack = NULL;
    }
#endif
    list->max_nodes = 0;
    list->head      = NULL;

#if ZEROLIST_SIZE_ENABLE
    list->size = 0;
#endif
#else
    // 纯静态模式：缓冲区由用户管理，不需要释放内存
    // max_nodes 保持不变，以便 zerolist_reinit 可以重新使用
    // 注意：head、tail、size 已在 zerolist_clear 中重置
#if ZEROLIST_FAST_ALLOC
    if (list->free_stack) {
        list->free_top = 0;
    }
#endif
#endif
}

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
 * @param initial_size 初始大小（仅动态扩容模式需要，其他模式忽略）
 * @return true 初始化成功
 * @return false 初始化失败（参数无效或内存分配失败）
 *
 * @note 此函数必须在 zerolist_destroy() 之后调用
 * @note 对于纯静态模式，此函数会重新初始化状态，使用原有的缓冲区
 * @note 对于动态扩容模式，需要提供 initial_size 参数
 */
bool zerolist_reinit(Zerolist* list, ZEROLIST_TYPE initial_size)
{
    if (!list) return false;

#if ZEROLIST_USE_MALLOC
    // 动态模式：重新初始化
    return list_init_dynamic(list);
#elif ZEROLIST_STATIC_DYNAMIC_EXPAND
    // 动态扩容模式：重新分配缓冲区并初始化
    if (initial_size == 0) return false;
    return list_init_dynamic_expand(list, initial_size);
#else
    // 纯静态模式：使用原有缓冲区重新初始化
    // max_nodes 在 destroy 时不会被设置为 0，所以这里可以正常使用
    if (!list->node_buf || list->max_nodes == 0) return false;

    list->head = NULL;
    list->tail = NULL;
#if ZEROLIST_SIZE_ENABLE
    list->size = 0;
#endif

#if ZEROLIST_FAST_ALLOC
    if (list->free_stack) {
        list->free_top = 0;
        for (ZEROLIST_TYPE i = 0; i < list->max_nodes; i++) {
            list->node_buf[i].flags.in_use     = 0;
            list->node_buf[i].flags.index      = i;
            list->free_stack[list->free_top++] = i;
        }
    }
#else
    for (ZEROLIST_TYPE i = 0; i < list->max_nodes; i++) {
        list->node_buf[i].flags.in_use = 0;
        list->node_buf[i].flags.index  = i;
    }
#endif
    return true;
#endif
}

#if !ZEROLIST_USE_MALLOC
// ===================== 静态模式 =====================

/**
 * @brief 初始化静态链表（静态缓冲区模式）
 *
 * @param list       链表指针
 * @param buf        节点缓冲区指针
 * @param free_stack 空闲索引栈指针（可为 NULL）
 * @param max_nodes  最大节点数量
 */
void zerolist_init_expand(Zerolist* list, zerolist_node_t* buf,
#if ZEROLIST_FAST_ALLOC
                          ZEROLIST_TYPE* free_stack,
#endif
                          ZEROLIST_TYPE max_nodes)
{
    if (!list || !buf || max_nodes == 0) return;

    list->head = NULL;

    list->node_buf  = buf;
    list->max_nodes = max_nodes;
#if ZEROLIST_SIZE_ENABLE
    list->size = 0;
#endif

#if ZEROLIST_FAST_ALLOC
    list->free_stack = free_stack;
    list->free_top   = max_nodes;
    for (ZEROLIST_TYPE i = 0; i < max_nodes; i++) {
        free_stack[i]       = (ZEROLIST_TYPE)(max_nodes - 1 - i);
        buf[i].flags.in_use = 0;
        buf[i].flags.index  = i;
    }
#else
    for (ZEROLIST_TYPE i = 0; i < max_nodes; i++) {
        // 初始化时存储下标，但不设置 in_use 位（空闲状态）
        buf[i].flags.in_use = 0;
        buf[i].flags.index  = i;
    }
#endif
}

#if ZEROLIST_STATIC_DYNAMIC_EXPAND

// ===========================================
// 内部辅助函数（提取公共逻辑）
// ===========================================

/*
 * 更新(Zerolist)中的节点指针，当缓冲区发生重新分配时使用
 *
 * 该函数处理当节点缓冲区从old_buf移动到new_buf时，需要更新所有节点中的prev/next指针
 * 以及列表的头尾指针。主要处理步骤包括：
 * 1. 保存原头尾节点在old_buf中的相对索引
 * 2. 更新列表的节点缓冲区指针
 * 3. 根据保存的索引恢复头尾指针
 * 4. 遍历所有节点，更新每个节点的prev/next指针和索引标志
 *
 * @param list      指向零列表的指针
 * @param old_buf   旧的节点缓冲区指针
 * @param new_buf   新的节点缓冲区指针
 * @param old_size  旧的缓冲区大小
 * @param new_size  新的缓冲区大小
 */
static void _zerolist_update_node_pointers(Zerolist* list, zerolist_node_t* old_buf,
                                           zerolist_node_t* new_buf, ZEROLIST_TYPE old_size,
                                           ZEROLIST_TYPE new_size)
{
    // 更新缓冲区指针（无论内存位置是否变化）
    list->node_buf = new_buf;

    // 如果内存位置未变化或链表为空，无需更新节点指针
    if (new_buf == old_buf || !list->head) {
        return;
    }

    // 保存关键索引（在old_buf被覆盖前）
    ZEROLIST_TYPE head_idx = (ZEROLIST_TYPE)(list->head - old_buf);
    // ZEROLIST_TYPE tail_idx = (ZEROLIST_TYPE)(list->tail - old_buf);

    // 更新头尾指针
    list->head = &new_buf[head_idx];
    // list->tail = &new_buf[tail_idx];

    // 遍历更新所有节点指针
    zerolist_node_t* cur = list->head;
    do {
        // 通过指针计算当前节点在new_buf中的索引（不再使用flags.index）
        ZEROLIST_TYPE cur_idx = (ZEROLIST_TYPE)(cur - new_buf);

        // 计算prev/next节点的索引（相对于old_buf）
        ZEROLIST_TYPE prev_idx = (ZEROLIST_TYPE)(cur->prev - old_buf);
        ZEROLIST_TYPE next_idx = (ZEROLIST_TYPE)(cur->next - old_buf);

        // 确保索引在有效范围内（缩容时需要）
        if (prev_idx >= new_size) {
            prev_idx = cur_idx;
        }
        if (next_idx >= new_size) {
            next_idx = cur_idx;
        }

        // 更新指针指向new_buf中的节点
        cur->prev = &new_buf[prev_idx];
        cur->next = &new_buf[next_idx];
        // 保留字段但不使用：cur->flags.index = cur_idx;

        cur = cur->next;
    } while (cur != list->head);
}

/*
 * 回滚零列表的内存重新分配操作
 *
 * 当内存重新分配失败时，将列表缓冲区回滚到旧大小。如果分配成功但需要更新指针，
 * 则调用指针更新函数处理节点指针。
 *
 * @param list      指向零列表的指针
 * @param new_buf   尝试分配的新缓冲区指针
 * @param old_buf   旧的缓冲区指针
 * @param old_size  旧缓冲区的大小（节点数量）
 * @param new_size  新缓冲区的大小（节点数量，用于指针更新）
 *
 * @return 成功返回true，失败返回false
 */
static bool _zerolist_rollback_realloc(Zerolist* list, zerolist_node_t* new_buf,
                                       zerolist_node_t* old_buf, ZEROLIST_TYPE old_size,
                                       ZEROLIST_TYPE new_size)
{
    zerolist_node_t* rollback_buf =
        (zerolist_node_t*)ZEROLIST_REALLOC(new_buf, old_size * _ZEROLIST_NODE_SIZE);

    if (!rollback_buf) {
        return false;
    }

    // 如果回滚时内存位置发生变化，需要更新指针
    if (rollback_buf != new_buf && list->head) {
        _zerolist_update_node_pointers(list, new_buf, rollback_buf, new_size, old_size);
    }

    list->node_buf = rollback_buf;
    return true;
}

/**
 * @brief Expand dynamic buffer (internal function)
 * @param list linked list pointer
 * @param new_size new buffer size
 * @return true Expand successful
 * @return false Expand failed
 */
static bool _zerolist_expand_buffer(Zerolist* list, ZEROLIST_TYPE new_size)
{
    if (new_size <= list->max_nodes) return false;

    zerolist_node_t* old_buf  = list->node_buf;
    ZEROLIST_TYPE    old_size = list->max_nodes;

    zerolist_node_t* new_buf =
        (zerolist_node_t*)ZEROLIST_REALLOC(list->node_buf, new_size * _ZEROLIST_NODE_SIZE);
    if (!new_buf) {
        return false;
    }

    // 如果 realloc 移动了内存位置，需要更新所有节点的指针
    _zerolist_update_node_pointers(list, old_buf, new_buf, old_size, new_size);

#if ZEROLIST_FAST_ALLOC
    ZEROLIST_TYPE* new_stack =
        (ZEROLIST_TYPE*)ZEROLIST_REALLOC(list->free_stack, new_size * sizeof(ZEROLIST_TYPE));
    if (!new_stack) {
        // free_stack分配失败，回滚node_buf
        if (!_zerolist_rollback_realloc(list, new_buf, old_buf, old_size, new_size)) {
            // 回滚失败，尝试恢复原缓冲区
            list->node_buf = old_buf;
        }
        return false;
    }
    list->free_stack = new_stack;
#endif

#if ZEROLIST_FAST_ALLOC
    for (ZEROLIST_TYPE i = old_size; i < new_size; i++) {
        list->node_buf[i].flags.in_use     = 0;
        list->node_buf[i].flags.index      = i;
        list->free_stack[list->free_top++] = i;
    }
#else
    for (ZEROLIST_TYPE i = old_size; i < new_size; i++) {
        list->node_buf[i].flags.in_use = 0;
        list->node_buf[i].flags.index  = i;
    }
#endif

    list->max_nodes = new_size;
    return true;
}
bool zerolist_shrink_buffer(Zerolist* list, ZEROLIST_TYPE new_size)
{
    size_t used_nodes = zerolist_size(list);
    if (new_size <= used_nodes) {
        new_size = used_nodes * 2;
    }

    if (new_size >= list->max_nodes) {
        return true;
    }

    zerolist_node_t* old_buf  = list->node_buf;
    ZEROLIST_TYPE    old_size = list->max_nodes;

    zerolist_node_t* new_buf =
        (zerolist_node_t*)ZEROLIST_REALLOC(list->node_buf, new_size * _ZEROLIST_NODE_SIZE);
    if (!new_buf) {
        return false;
    }

    _zerolist_update_node_pointers(list, old_buf, new_buf, old_size, new_size);

#if ZEROLIST_FAST_ALLOC
    ZEROLIST_TYPE* new_stack =
        (ZEROLIST_TYPE*)ZEROLIST_REALLOC(list->free_stack, new_size * sizeof(ZEROLIST_TYPE));

    if (!new_stack) {
        if (!_zerolist_rollback_realloc(list, new_buf, old_buf, old_size, new_size)) {
            list->node_buf = old_buf;
        }
        return false;
    }
    list->free_stack = new_stack;

    list->free_top = 0;
    for (ZEROLIST_TYPE i = 0; i < new_size; i++) {
        if (!list->node_buf[i].flags.in_use) {
            list->free_stack[list->free_top++] = i;
        }
    }
#endif

    list->max_nodes = new_size;
    return true;
}

bool list_init_dynamic_expand(Zerolist* list, ZEROLIST_TYPE initial_size)
{
    if (!list || initial_size == 0) return false;

    size_t           buf_size = (size_t)initial_size * _ZEROLIST_NODE_SIZE;
    zerolist_node_t* buf      = (zerolist_node_t*)ZEROLIST_MALLOC(buf_size);
    if (!buf) return false;

#if ZEROLIST_FAST_ALLOC
    size_t         stack_size = (size_t)initial_size * sizeof(ZEROLIST_TYPE);
    ZEROLIST_TYPE* free_stack = (ZEROLIST_TYPE*)ZEROLIST_MALLOC(stack_size);
    if (!free_stack) {
        ZEROLIST_FREE(buf);
        return false;
    }
#else
    ZEROLIST_TYPE* free_stack = NULL;
#endif

    list->head = NULL;

    list->node_buf  = buf;
    list->max_nodes = initial_size;
#if ZEROLIST_SIZE_ENABLE
    list->size = 0;
#endif

#if ZEROLIST_FAST_ALLOC
    list->free_stack = free_stack;
    list->free_top   = initial_size;
    for (ZEROLIST_TYPE i = 0; i < initial_size; i++) {
        free_stack[i]       = (ZEROLIST_TYPE)(initial_size - 1 - i);
        buf[i].flags.in_use = 0;
        buf[i].flags.index  = i;
    }
#else
    for (ZEROLIST_TYPE i = 0; i < initial_size; i++) {
        buf[i].flags.in_use = 0;
        buf[i].flags.index  = i;
    }
#endif

    return true;
}
#endif  // ZEROLIST_STATIC_DYNAMIC_EXPAND

#endif  // ZEROLIST_USE_MALLOC

// ===========================================
//  插入操作
// ===========================================

static inline bool _zerolist_insert_internal(Zerolist* list, zerolist_node_t* pos, void* data,
                                             bool before)
{
#if ZEROLIST_STATIC_DYNAMIC_EXPAND && !ZEROLIST_USE_MALLOC
    ZEROLIST_TYPE pos_idx       = 0;
    bool          pos_idx_valid = false;
    if (pos && _zerolist_is_static_node(list, pos)) {
        // 不再使用flags.index，直接通过指针计算索引
        pos_idx       = (ZEROLIST_TYPE)(pos - list->node_buf);
        pos_idx_valid = true;
    }
#endif

    zerolist_node_t* node = _zerolist_alloc_node(list);
    if (!node) return false;
    node->data = data;

#if ZEROLIST_STATIC_DYNAMIC_EXPAND && !ZEROLIST_USE_MALLOC
    if (pos && pos_idx_valid && !_zerolist_is_static_node(list, pos)) {
        pos = &list->node_buf[pos_idx];
    }
#endif

    if (!list->head) {
        list->head = node;
        node->next = node->prev = node;
#if ZEROLIST_SIZE_ENABLE
        list->size = 1;
#endif
        return true;
    }

    if (!pos) pos = before ? list->head : list->head->prev;

    if (before) {
        node->prev      = pos->prev;
        node->next      = pos;
        pos->prev->next = node;
        pos->prev       = node;
        if (pos == list->head) {
            list->head = node;  // 只需这一行！
        }
    } else {
        node->next      = pos->next;
        node->prev      = pos;
        pos->next->prev = node;
        pos->next       = node;
        // if (pos == list->tail) {
        //     list->tail       = node;
        //     list->head->prev = list->tail;
        //     list->tail->next = list->head;
        // }
    }
#if ZEROLIST_SIZE_ENABLE
    list->size++;
#endif
    return true;
}

bool zerolist_push_front(Zerolist* list, void* data)
{
    if (!list) return false;
    return _zerolist_insert_internal(list, list->head, data, true);
}

bool zerolist_push_back(Zerolist* list, void* data)
{
    if (!list) return false;
    return _zerolist_insert_internal(list, NULL, data, false);
}

bool zerolist_insert_before(Zerolist* list, void* target_data, void* new_data)
{
    if (!list || !list->head) return false;
    zerolist_node_t* cur = list->head;
    do {
        if (cur->data == target_data) return _zerolist_insert_internal(list, cur, new_data, true);
        cur = cur->next;
    } while (cur != list->head);
    return false;
}

// ===========================================
//  删除操作
// ===========================================

/*
 *Detach the specified node from the linked list
 *
 *This function handles three situations:
 *1. The separated nodes are the head and tail nodes (the linked list becomes empty)
 *2. The separated node is the head node (update the head pointer)
 *3. The separated node is the tail node (update the tail pointer)
 *
 *@param list pointer to zero-length linked list
 *@param cur The node pointer to be separated
 */
static inline void _zerolist_detach_node(Zerolist* list, zerolist_node_t* cur)
{
    if (!list || !cur) return;

    // 唯一节点：摘除后链表为空
    if (cur->next == cur) {
        list->head = NULL;
        return;
    }

    // 安全检查（防御性编程）
    if (!cur->prev || !cur->next) {
        if (cur == list->head) {
            list->head = NULL;
        }
        return;
    }

    // 标准双向链表摘除
    cur->prev->next = cur->next;
    cur->next->prev = cur->prev;

    // 如果摘除的是头节点，更新 head
    if (cur == list->head) {
        list->head = cur->next;
    }

    // 注意：不再处理 tail，尾部由 head->prev 隐式表示
}

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
void* zerolist_pop_front(Zerolist* list)
{
    if (!list || !list->head) return NULL;

    zerolist_node_t* node = list->head;
    void*            data = node->data;

#if ZEROLIST_SIZE_ENABLE
    --list->size;
#endif

    _zerolist_detach_node(list, node);  // ← 全权处理结构更新
    zerolist_free_node(list, node);

    return data;
}

/*
 * 从零列表尾部弹出节点数据
 *
 * 该函数会移除列表尾部的节点，并返回该节点中存储的数据。
 * 如果列表为空或不存在尾部节点，则返回NULL。
 *
 * @param list 指向零列表的指针，不能为NULL
 * @return 返回被弹出节点中存储的数据指针，如果操作失败则返回NULL
 */
void* zerolist_pop_back(Zerolist* list)
{
    if (!list || !list->head) return NULL;

    zerolist_node_t* node = list->head->prev;
    void*            data = node->data;

#if ZEROLIST_SIZE_ENABLE
    --list->size;
#endif

    _zerolist_detach_node(list, node);  // ← 同样调用 detach
    zerolist_free_node(list, node);

    return data;
}

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
void* zerolist_pop_at(Zerolist* list, ZEROLIST_TYPE index)
{
    if (!list || !list->head) return NULL;

#if ZEROLIST_SIZE_ENABLE
    if (index >= list->size) return NULL;
#endif

    zerolist_node_t* cur = list->head;
    for (ZEROLIST_TYPE i = 0; i < index; ++i) {
        cur = cur->next;
        if (cur == list->head) return NULL;
    }

    void* data = cur->data;

    _zerolist_detach_node(list, cur);

#if ZEROLIST_SIZE_ENABLE
    --list->size;
#endif

    zerolist_free_node(list, cur);
    return data;
}

bool zerolist_remove_ptr(Zerolist* list, void* data)
{
    if (!list || !data) return false;

#if !ZEROLIST_USE_MALLOC && !ZEROLIST_STATIC_FALLBACK_MALLOC
    for (ZEROLIST_TYPE i = 0; i < list->max_nodes; ++i) {
        zerolist_node_t* node = &list->node_buf[i];
        if (_ZEROLIST_NODE_IS_IN_USE(node) && node->data == data) {
            _zerolist_detach_node(list, node);
            zerolist_free_node(list, node);
#if ZEROLIST_SIZE_ENABLE
            list->size--;
#endif
            return true;
        }
    }
    return false;

#else
    if (!list->head) return false;
    zerolist_node_t* start = list->head;
    zerolist_node_t* cur   = start;
    do {
        if (cur->data == data) {
            _zerolist_detach_node(list, cur);
            zerolist_free_node(list, cur);
#if ZEROLIST_SIZE_ENABLE
            list->size--;
#endif
            return true;
        }
        cur = cur->next;
        if (!cur) break;
    } while (cur != start);
    return false;
#endif
}

bool zerolist_remove_if(Zerolist* list, void* data, bool (*cmp_func)(const void*, const void*))
{
    if (!list || !cmp_func) return false;

#if !ZEROLIST_USE_MALLOC && !ZEROLIST_STATIC_FALLBACK_MALLOC
    for (ZEROLIST_TYPE i = 0; i < list->max_nodes; ++i) {
        zerolist_node_t* node = &list->node_buf[i];
        if (_ZEROLIST_NODE_IS_IN_USE(node) && cmp_func(node->data, data)) {
            _zerolist_detach_node(list, node);
            zerolist_free_node(list, node);
#if ZEROLIST_SIZE_ENABLE
            list->size--;
#endif
            return true;
        }
    }
    return false;

#else
    if (!list->head) return false;
    zerolist_node_t* start = list->head;
    zerolist_node_t* cur   = start;
    do {
        if (cmp_func(cur->data, data)) {
            _zerolist_detach_node(list, cur);
            zerolist_free_node(list, cur);
#if ZEROLIST_SIZE_ENABLE
            list->size--;
#endif
            return true;
        }
        cur = cur->next;
        if (!cur) break;
    } while (cur != start);
    return false;
#endif
}

bool zerolist_remove_at(Zerolist* list, ZEROLIST_TYPE index)
{
    if (!list || !list->head) return false;
#if ZEROLIST_SIZE_ENABLE
    if (index >= list->size) return false;
#endif
    zerolist_node_t* cur = list->head;
    for (ZEROLIST_TYPE i = 0; i < index; ++i) {
        cur = cur->next;
        if (cur == list->head) return false;
    }
    _zerolist_detach_node(list, cur);
    zerolist_free_node(list, cur);
#if ZEROLIST_SIZE_ENABLE
    list->size--;
#endif
    return true;
}

// ===========================================
//  查询 / 遍历
// ===========================================

void* zerolist_at(Zerolist* list, ZEROLIST_TYPE index)
{
    if (!list || !list->head) return NULL;
#if ZEROLIST_SIZE_ENABLE
    if (index >= list->size) return NULL;
    zerolist_node_t* cur = list->head;
    for (ZEROLIST_TYPE i = 0; i < index; ++i) {
        cur = cur->next;
        if (!cur) return NULL;
    }
    return cur->data;
#else
    zerolist_node_t* cur = list->head;
    for (ZEROLIST_TYPE i = 0; i < index; ++i) {
        cur = cur->next;
        if (!cur) return NULL;
    }
    return cur->data;
#endif
}

#define _ZEROLIST_FOREACH_NODE_STATIC(list, node_var, body)        \
    do {                                                           \
        for (ZEROLIST_TYPE _i = 0; _i < (list)->max_nodes; ++_i) { \
            zerolist_node_t* node_var = &(list)->node_buf[_i];     \
            if (_ZEROLIST_NODE_IS_IN_USE(node_var)) {              \
                body                                               \
            }                                                      \
        }                                                          \
    } while (0)

#define _ZEROLIST_FOREACH_NODE_DYNAMIC(list, node_var, body) \
    do {                                                     \
        if ((list)->head) {                                  \
            zerolist_node_t* _start   = (list)->head;        \
            zerolist_node_t* node_var = _start;              \
            do {                                             \
                body node_var = node_var->next;              \
                if (!node_var) break;                        \
            } while (node_var != _start);                    \
        }                                                    \
    } while (0)

zerolist_node_t* zerolist_find(Zerolist* list, const void* target_addr)
{
    if (!list) return NULL;

#if !ZEROLIST_USE_MALLOC && !ZEROLIST_STATIC_FALLBACK_MALLOC
    _ZEROLIST_FOREACH_NODE_STATIC(list, node, {
        if (node->data == target_addr) return node;
    });
#else
    _ZEROLIST_FOREACH_NODE_DYNAMIC(list, node, {
        if (node->data == target_addr) return node;
    });
#endif
    return NULL;
}

zerolist_node_t* zerolist_search(Zerolist* list, const void* target_data,
                                 bool (*cmp_func)(const void*, const void*))
{
    if (!list || !cmp_func) return NULL;

#if !ZEROLIST_USE_MALLOC && !ZEROLIST_STATIC_FALLBACK_MALLOC
    for (ZEROLIST_TYPE i = 0; i < list->max_nodes; ++i) {
        zerolist_node_t* node = &list->node_buf[i];
        if (_ZEROLIST_NODE_IS_IN_USE(node) && cmp_func(node->data, target_data)) {
            return node;
        }
    }
    return NULL;

#else
    if (!list->head) return NULL;
#if ZEROLIST_SIZE_ENABLE
    zerolist_node_t* cur       = list->head;
    ZEROLIST_TYPE    remaining = list->size;
    while (remaining--) {
        if (cmp_func(cur->data, target_data)) return cur;
        cur = cur->next;
        if (!cur) return NULL;
    }
#else
    zerolist_node_t* start = list->head;
    zerolist_node_t* cur   = start;
    do {
        if (cmp_func(cur->data, target_data)) return cur;
        cur = cur->next;
        if (!cur) return NULL;
    } while (cur != start);
#endif
    return NULL;
#endif
}
void zerolist_foreach(Zerolist* list, void (*callback)(void* data))
{
    if (!list || !callback) return;

#if !ZEROLIST_USE_MALLOC && !ZEROLIST_STATIC_FALLBACK_MALLOC
    for (ZEROLIST_TYPE i = 0; i < list->max_nodes; ++i) {
        zerolist_node_t* node = &list->node_buf[i];
        if (_ZEROLIST_NODE_IS_IN_USE(node)) {
            callback(node->data);
        }
    }
#else
    if (!list->head) return;
    zerolist_node_t* start = list->head;
    zerolist_node_t* cur   = start;
    do {
        callback(cur->data);
        cur = cur->next;
        if (!cur) return;
    } while (cur != start);
#endif
}

// ===========================================
// 工具函数
// ===========================================

void zerolist_reverse(Zerolist* list)
{
    if (!list || !list->head) return;

    // 单节点无需反转
    if (list->head->next == list->head) return;

    zerolist_node_t* cur      = list->head;
    zerolist_node_t* old_tail = list->head->prev;  // 原尾节点将成为新头

    do {
        zerolist_node_t* tmp = cur->next;
        cur->next            = cur->prev;
        cur->prev            = tmp;
        cur                  = tmp;
    } while (cur != list->head);

    list->head = old_tail;
}
void zerolist_clear(Zerolist* list)
{
    if (!list) return;

#if !ZEROLIST_USE_MALLOC && !ZEROLIST_STATIC_FALLBACK_MALLOC

    for (ZEROLIST_TYPE i = 0; i < list->max_nodes; ++i) {
        zerolist_node_t* node = &list->node_buf[i];
        _ZEROLIST_NODE_SET_FREE(node);
        node->next = node->prev = node;
        node->data              = NULL;
    }
#else
    zerolist_node_t* cur = list->head;
    while (cur) {
        zerolist_node_t* next = cur->next;
        zerolist_free_node(list, cur);
        cur = next == list->head ? NULL : next;
    }
#endif

    list->head = NULL;
#if ZEROLIST_SIZE_ENABLE
    list->size = 0;
#endif
}

ZEROLIST_TYPE zerolist_size(Zerolist* list)
{
#if ZEROLIST_SIZE_ENABLE
    return list ? list->size : 0;
#else
    ZEROLIST_TYPE cnt = 0;
    if (!list || !list->head) return 0;
    zerolist_node_t* cur = list->head;
    do {
        cnt++;
        cur = cur->next;
    } while (cur != list->head);
    return cnt;
#endif
}
