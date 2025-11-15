# zerolist

`zerolist` 是一套专门面向 MCU 与其他受限平台的双向循环链表库。核心设计是**完全可选的 malloc 依赖**：在默认配置下，所有节点来自编译期/链接期预留的静态内存池，满足确定性延迟与可控的内存占用；需要时再通过编译开关切换到混合或纯动态模式，以兼顾 PC/服务器上的灵活度。

## 设计初衷

- **MCU 友好**：默认禁止 `malloc/free`，通过 `ZEROLIST_DEFINE` 直接定义定长链表，初始化后即可 O(1) 插入、删除。
- **自定义内存池**：可将 `ZEROLIST_MALLOC/ZEROLIST_FREE/ZEROLIST_REALLOC` 重定向到用户的 pool/region，方便与 RTOS、伙伴系统或 DMA buffer 集成。
- **统一 API**：`zerolist_push_back`、`zerolist_delete`、`zerolist_foreach` 等接口在任何模式下保持一致，业务层无需关心底层存储差异。
- **安全可靠**：节点状态位、索引校验、遍历安全宏以及空指针防护，阻断常见的误 delete、重复 free 等问题。

## 多种数据存储策略

| 模式 | 适用宏组合 | 特点 |
| --- | --- | --- |
| 纯静态缓冲 | `ZEROLIST_USE_MALLOC=0`, `ZEROLIST_STATIC_FALLBACK_MALLOC=0`, `ZEROLIST_STATIC_DYNAMIC_EXPAND=0` | 预分配 `node_buf`，节点索引写入位域，最可控、最适合 MCU。 |
| 静态 + 空闲栈 | 额外开启 `ZEROLIST_FAST_ALLOC=1`（默认） | 维护 `free_stack`，每次分配/释放都是 O(1)，适合频繁插入删除。 |
| 静态 + malloc 回退 | `ZEROLIST_STATIC_FALLBACK_MALLOC=1` | 静态池耗尽时自动向自定义内存池申请节点，用完再回收回静态池，可在“刚性容量 + 弹性需求”场景使用。 |
| 静态 + 动态扩容 | `ZEROLIST_STATIC_DYNAMIC_EXPAND=1` | 起始依旧静态接口，但会以 `malloc/realloc` 的方式扩展 `node_buf` 与 `free_stack`，链表销毁时由库统一释放。 |
| 纯动态 | `ZEROLIST_USE_MALLOC=1` | 直接走标准堆或用户内存池，API 仍一致，方便在主机环境复用逻辑与测试。 |

### 节点与数据的布置方式

1. **节点结构 (`zerolist_node `)**  
   - 保存 `data` 指针、前后指针以及一个位域 `flags`。  
   - 在静态模式下，`flags.index` 记录节点在 `node_buf` 中的下标，便于调试与回写。  

2. **静态缓冲 (`node_buf`)**  
   - `ZEROLIST_DEFINE(list, N)` 会在 `.bss` 或 `.data` 中生成 `zerolist_node  list_buf[N]`，以及（若开启）同等数量的 `free_stack` 索引。  
   - 初始化 (`ZEROLIST_INIT`) 时会一次性把空闲索引推入栈，插入节点时直接从栈顶弹出，释放时再推回，保证常数级耗时。  

3. **混合节点**  
   - 当 `ZEROLIST_STATIC_FALLBACK_MALLOC=1` 时，静态池满会调用 `ZEROLIST_MALLOC` 取一个额外节点，并在 `flags.in_use` 中标记。  
   - 节点释放后，如果来自静态池就归还索引，如果来自回退分配则走 `ZEROLIST_FREE`。  

4. **动态扩容池**  
   - 开启 `ZEROLIST_STATIC_DYNAMIC_EXPAND` 后，`list_init_dynamic_expand` 会使用 `ZEROLIST_MALLOC` 分配初始 `node_buf` 与 `free_stack`，当 `max_nodes` 用尽时自动 `ZEROLIST_REALLOC` 扩容。  
   - 适合“默认静态语义 + 运行期按需变长”的半动态场景。  

5. **用户数据存储**  
   - 节点只保存 `void *data`，用户可以放栈对象、结构体、DMA 缓冲等任意指针。  
   - 需要托管内存时，可结合对象池或静态数组，将 `data` 指向池中的元素，从而实现“节点池 + 数据池”双静态管理。  

## 配置宏一览

| 宏 | 默认值 | 说明 |
| --- | --- | --- |
| `ZEROLIST_USE_MALLOC` | 0 | 1=全动态模式，0=走静态池/混合模式。 |
| `ZEROLIST_FAST_ALLOC` | 1 | 静态模式下启用空闲栈，O(1) 分配释放。 |
| `ZEROLIST_STATIC_FALLBACK_MALLOC` | 1 | 静态池满后是否向内存池申请额外节点（与动态扩容互斥）。 |
| `ZEROLIST_STATIC_DYNAMIC_EXPAND` | 0 | 是否对静态池做自动扩容。 |
| `ZEROLIST_SIZE_ENABLE` | 1 | 维护 `zerolist_size` 字段获取 O(1) 长度。 |
| `ZEROLIST_TYPE` | `uint8_t` | 节点索引/大小类型（可切换为 `uint16_t/uint32_t`）。 |
| `ZEROLIST_MALLOC/ZEROLIST_FREE/ZEROLIST_REALLOC` | 标准库版本 | 可替换为用户内存池接口。 |

> **配置示例：启用静态扩容并提升索引范围**
> ```cmake
> target_compile_definitions(example_fallback PRIVATE
>     ZEROLIST_STATIC_FALLBACK_MALLOC=0
>     ZEROLIST_STATIC_DYNAMIC_EXPAND=1
>     ZEROLIST_TYPE=uint16_t)
> ```

## 构建与运行

```powershell
cmake -S . -B build
cmake --build build
./build/example_fallback      # Windows 上为 .\build\example_fallback.exe
```

`CMakeLists.txt` 默认编译 `example_fallback`，其中串联了所有演示场景。若需要在自己的工程中使用，可直接把 `zerolist.c/h` 加入目标并在 `target_compile_definitions` 中设置对应宏。

## 示例概览

`example/example.c` 将库的行为拆为 9 组用例，覆盖链表的完整生命周期：

1. **纯静态 MCU 模式**：插入/遍历/删除/反转的最小示例。  
2. **纯动态模式**：在 PC 上验证行为一致性。  
3. **静态 + malloc 回退**：演示静态节点与回退节点的混合使用和回收。  
4. **静态 + 动态扩容**：向 20+ 节点写入数据的同时自动扩容。  
5. **遍历宏**：`ZEROLIST_FOR_EACH` 与 `ZEROLIST_FOR_EACH_SAFE
` 的典型写法。  
6. **性能计数**：多轮插入/遍历/删除，输出平均耗时。  
7. **鲁棒性用例**：溢出、越界、重复删除、重复清空等防护。  
8. **空指针/误操作**：未初始化、NULL 参数、非法节点的处理。  
9. **随机压测**：100~200 节点规模下的随机混合操作。  

运行示例即可看到各阶段打印，帮助核对当前配置是否满足实际项目需求。

## 与主库文件的关系

- `zerolist.h`：公开 API、宏与节点/链表结构体，集中罗列所有配置点。  
- `zerolist.c`：实现所有模式下的插入、删除、扩容、索引回写与安全遍历逻辑。  
- `example/example.c`：集合测试/演示入口，可作为移植或回归的模板。  

## 下一步建议

- MCU 场景：保持 `ZEROLIST_USE_MALLOC=0`，根据容量调整 `ZEROLIST_TYPE` 与 `ZEROLIST_DEFINE` 的大小；如需硬实时，可关闭回退机制。  
- 桌面/仿真：开启动态模式或动态扩容，快速验证复杂业务逻辑。  
- 自定义监控：参考示例里的 `fprintf`，在库内加入钩子收集统计或将日志重定向到串口/JTAG。  

欢迎根据业务需求扩展示例或提交改进建议。 如果你也在做自定义内存池或需要更细粒度的诊断，欢迎提出 Issue 一起迭代。祝开发顺利！

