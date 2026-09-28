# libDLT645_2007

`DL/T 645—2007《多功能电能表通信协议》` 的跨平台 C 语言实现。

- **纯 C99**，**不使用动态内存**，适合裸机 MCU、RTOS 与 PC/网关。
- **主站（客户端）与从站（电表）双角色**。
- **非阻塞状态机** 为核心，附带 **阻塞式便捷封装**。
- **函数指针 HAL 端口** 解耦物理层，核心库不依赖任何操作系统。附带 Windows/POSIX 串口参考实现。
- 内置由标准 **附录 A 自动生成** 的数据标识（DI）目录，支持费率/结算日通配匹配。

---

## 目录结构

```
include/dlt645/      公共头文件
  dlt645.h           总入口（umbrella）
  dlt645_types.h     类型、常量、状态码
  dlt645_codec.h     BCD / 地址 / 时间 / 数据项编解码
  dlt645_frame.h     组帧、解帧、流式接收状态机
  dlt645_port.h      HAL 端口定义
  dlt645_master.h    主站（客户端）
  dlt645_slave.h     从站（电表）
  dlt645_di.h        数据标识目录
  dlt645_serial.h    可选串口适配器
src/                 实现
port/                串口参考实现（Windows / POSIX）
tools/gen_di_table.py 从标准 PDF 生成 DI 目录
tools/dlt645_di_catalog.json  生成的完整 DI 目录
examples/            主站 / 从站示例
tests/               单元测试与回环集成测试
docs/                标准 PDF
```

---

## 构建

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

选项：

| 选项 | 默认 | 说明 |
|---|---|---|
| `DLT645_BUILD_TESTS` | ON | 构建单元测试 |
| `DLT645_BUILD_EXAMPLES` | ON | 构建示例 |
| `DLT645_BUILD_SERIAL` | ON | 构建串口适配器 |

也可直接把 `src/*.c` 与 `include/dlt645` 加入你自己的工程（核心库无需任何外部依赖）。

---

## 作为依赖使用

安装后同时提供 **CMake package** (`find_package(dlt645)`) 与 **pkg-config**
(`dlt645.pc`)，两种包含风格都可用：`#include <dlt645/dlt645.h>`（推荐）或
`#include <dlt645.h>`。安装产物：

```
<prefix>/include/dlt645/*.h
<prefix>/lib/libdlt645.a            (+ libdlt645_serial.a)
<prefix>/lib/cmake/dlt645/dlt645Config.cmake
<prefix>/lib/cmake/dlt645/dlt645Targets.cmake    # 目标 dlt645::dlt645 / dlt645::dlt645_serial
<prefix>/lib/pkgconfig/dlt645.pc
```

### 1. 安装到系统 / 自定义前缀

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DDLT645_BUILD_TESTS=OFF -DDLT645_BUILD_EXAMPLES=OFF
cmake --build build
cmake --install build            # 或 cmake --install build --prefix <dir>
```

### 2. CMake `find_package`（推荐）

```cmake
find_package(dlt645 0.1 CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE dlt645::dlt645)
# 需要串口适配器时：
target_link_libraries(app PRIVATE dlt645::dlt645_serial)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
```

### 3. `add_subdirectory` / `FetchContent`（不安装）

```cmake
# 方式 A：源码子目录
add_subdirectory(third_party/libDLT645_2007)
target_link_libraries(app PRIVATE dlt645::dlt645)

# 方式 B：拉取
include(FetchContent)
FetchContent_Declare(dlt645
    GIT_REPOSITORY https://github.com/wqs-base/libDLT645.git
    GIT_TAG        master)
FetchContent_MakeAvailable(dlt645)
target_link_libraries(app PRIVATE dlt645::dlt645)
```

两种方式都可加 `-DDLT645_BUILD_TESTS=OFF -DDLT645_BUILD_EXAMPLES=OFF` 避免构建
本仓库的测试与示例。

### 4. pkg-config

```sh
cc app.c $(pkg-config --cflags --libs dlt645) -o app
```

### 5. 无 CMake 的裸工程

把 `include/dlt645/` 加入头文件搜索路径，编译并链接 `src/*.c`
（**不要**编译 `src/dlt645_di_table.inc`，它是被 `#include` 的生成文件）：

```sh
cc -std=c99 -Iinclude/dlt645 app.c src/*.c -o app
```

---

## 快速开始（主站，阻塞式）

```c
#include "dlt645.h"
#include "dlt645_serial.h"

static dlt645_serial_t serial;
static dlt645_port_t   port;
static dlt645_master_t master;
static dlt645_addr_t   meter;

dlt645_serial_open(&serial, "COM3", 2400);   /* 8E1 */
dlt645_serial_port(&serial, &port);

dlt645_master_init(&master, &port, NULL);     /* 默认：重试 3 次，超时 500ms */
dlt645_addr_parse("123456789012", &meter);

dlt645_master_set_callbacks(&master, on_frame, on_done, NULL);
dlt645_master_build_read(&master, &meter, 0x00010000u); /* 正向有功总电能 */
dlt645_status_t st = dlt645_master_run(&master, 3000);  /* 阻塞至完成 */
```

`on_frame` 回调收到每一帧应答（多帧续读会多次回调），用
`dlt645_frame_data()` 取出明文数据域，用 `dlt645_di_*` 解析含义：

```c
static void on_frame(dlt645_master_t *m, const dlt645_frame_t *f, void *u)
{
    uint8_t plain[DLT645_MAX_DATA]; size_t n;
    char name[128], value[32];
    if (dlt645_frame_data(f, plain, sizeof plain, &n) != DLT645_OK) return;

    uint32_t di = dlt645_di_read(plain);
    const dlt645_di_info_t *info = dlt645_di_lookup(di);
    dlt645_di_format_name(di, name, sizeof name);
    dlt645_bcd_format(&plain[4], (uint8_t)(n - 4), info->decimals,
                      value, sizeof value);
    printf("%s = %s %s\n", name, value, info->unit);
}
```

## 快速开始（主站，非阻塞）

在裸机/RTOS 中，把收到的字节喂给 `dlt645_master_poll()` 即可，无需阻塞：

```c
/* UART RX 中断里把字节缓存起来，主循环里： */
dlt645_master_poll(&master, dlt645_port_now_ms());
/* 或使用自己的毫秒计数 */
```

`dlt645_master_build_*()` 启动一笔事务，`dlt645_master_busy()` 判断是否进行中，
`dlt645_master_get_result()` 取结果。**读数据**若数据超过 200 字节，主站会自动发送
`12H` 读后续命令并在回调中交回全部数据。

## 快速开始（从站 / 电表侧）

```c
static dlt645_slave_t slave;

static dlt645_status_t my_read(void *u, uint32_t di,
                               const uint8_t **data, size_t *len) {
    if (di == 0x00010000u) { static uint8_t e[4]={0x78,0x56,0x34,0x12};
                             *data = e; *len = 4; return DLT645_OK; }
    return DLT645_ERR_STATE;      /* 会应答“无请求数据” */
}
static dlt645_status_t my_auth(void *u, uint8_t pa,
                               const uint8_t pwd[3], const uint8_t op[4]) {
    return (pa == 0 && pwd[0]==0 && pwd[1]==0 && pwd[2]==0)
           ? DLT645_OK : DLT645_ERR_STATE;
}

dlt645_slave_model_t model = { .on_read = my_read, .on_auth = my_auth };

dlt645_slave_init(&slave, &port, &addr, &model);
dlt645_slave_run(&slave, 0);       /* 阻塞服务；或周期调用 slave_poll() */
```

从站自动处理：地址匹配与 `AA` 缩位寻址、广播（不应答）、帧校验、
各控制码的功能与异常应答、密码/操作者代码鉴权、读写 DI、多帧续读切分。

---

## HAL 端口

核心库只通过这些回调与外界交互：

```c
typedef struct dlt645_port {
    void *user;
    int      (*send)(void *, const uint8_t *, size_t, uint32_t /*timeout_ms*/);
    int      (*recv)(void *, uint8_t *, size_t, uint32_t /*timeout_ms*/); /* 非阻塞 */
    uint32_t (*now_ms)(void *);
    void     (*delay_ms)(void *, uint32_t);
} dlt645_port_t;
```

- `send`：发送全部字节，返回发送字节数，失败返回负值。
- `recv`：**非阻塞**读取，返回本次读到的字节数（无数据返回 0）。
- `now_ms`：单调毫秒计数（非阻塞引擎需要）。
- `delay_ms`：阻塞延时（阻塞封装需要）。

`port/dlt645_serial.c` 提供了 Windows 与 POSIX 的串口实现（8E1），可直接使用。

---

## 协议实现覆盖

| 功能 | 请求控制码 | 主站构造 | 从站处理 |
|---|---|---|---|
| 读数据 | 11H | ✅ | ✅ |
| 读后续数据 | 12H | ✅（自动） | ✅ |
| 读通信地址 | 13H | ✅ | ✅ |
| 写数据/编程 | 14H | ✅ | ✅（鉴权） |
| 写通信地址 | 15H | ✅ | ✅ |
| 广播校时 | 08H | ✅ | ✅ |
| 冻结 | 16H | ✅ | ✅ |
| 更改通信速率 | 17H | ✅ | ✅ |
| 修改密码 | 18H | ✅ | ✅ |
| 最大需量清零 | 19H | ✅ | ✅ |
| 电表清零 | 1AH | ✅ | ✅ |
| 事件清零 | 1BH | ✅ | ✅ |
| 异常应答 / 错误信息字 | D1H..DBH | ✅ | ✅ |

链路层细节：`68H` 起始、6 字节 BCD 地址（低字节在前）、`+0x33` 数据变换、
模 256 纵向校验、`16H` 结束、`FEH` 前导唤醒、缩位寻址 `AA`、广播地址
`999999999999H`。

---

## 数据标识目录

`tools/gen_di_table.py` 从标准 PDF 的附录 A 生成：

```sh
python tools/gen_di_table.py docs/多功能电能表通信协议.pdf
```

输出 `tools/dlt645_di_catalog.json`（完整目录）与 `src/dlt645_di_table.inc`
（编译进库的 C 表）。范围型标识（费率 1~63、结算日、数据块）以**通配掩码**
表示；`dlt645_di_lookup()` 做最具体匹配，`dlt645_di_format_name()` 会自动补上
`(费率N)` / `(上N结算日)` / `(数据块)` 后缀。附录 A 仅示例性列出部分结算日，
查询失败时会逐级放宽（忽略费率/结算日）以保证可解析。

也可自行注册/覆盖：调用 `dlt645_di_set_user_table()` 注册一个**应用自有的静态
数组**（不拷贝、不分配内存），自定义或厂商扩展的标识即可被 `dlt645_di_lookup()`
与 `dlt645_di_format_name()` 识别；**同等具体度下用户条目优先**，因此也能覆盖内置
条目（名称/长度/单位等）。

```c
static const dlt645_di_info_t my_di[] = {
    /* 新增厂商自定义标识 */
    DLT645_DI_DEFINE(0x0A000001u, "厂商自定义电能", "kWh",
                     "XXXXXX.XX", 4, 2, DLT645_DI_READ, DLT645_DI_CAT_OTHER),
    /* 覆盖内置条目 */
    DLT645_DI_DEFINE(0x00010000u, "正向有功总电能(厂家重定义)", "kWh",
                     "XXXXXX.XX", 4, 2,
                     DLT645_DI_READ | DLT645_DI_WRITE, DLT645_DI_CAT_ENERGY),
    /* 带通配掩码：DI1=费率可变，掩码中为 0 的位表示“任意值” */
    DLT645_DI_ENTRY(0x0B000000u, 0xFFFF00FFu, "厂商自定义总电能", "kWh",
                    "XXXXXX.XX", 4, 2,
                    DLT645_DI_READ | DLT645_DI_WILD_RATE, DLT645_DI_CAT_ENERGY),
};

dlt645_di_set_user_table(my_di, sizeof(my_di) / sizeof(my_di[0]));
/* ... 之后 lookup/format_name 即会命中自定义条目 ... */
dlt645_di_clear_user_table();   /* 需要时恢复内置行为 */
```

要点：
- 传入的数组必须在整个使用期间保持有效（用 `static` 数组最稳妥）。
- `mask` 中为 `1` 的位必须与 `di` 相等，为 `0` 的位表示该字节任意（用于费率/
  结算日/数据块）；`DLT645_DI_DEFINE` 等同于 `mask = 0xFFFFFFFF`。
- 仅主站侧的**解释/命名**用到目录；从站数据由 `dlt645_slave_model_t` 的
  `on_read` 回调提供，无需登记 DI。
- 非线程安全：请在启动阶段注册，不要与并发查询同时进行。

---

## 测试

```
test_frame   编解码、地址、时间、组帧/解帧、流式接收
test_link    主站↔从站内存回环：多帧续读(300B)、异常应答、读通信地址
test_di      DI 目录查询与命名
```

---

## 设计取舍

以下决策在实现中反复权衡，了解它们能避免误用与重复讨论。

| 取舍 | 理由 | 代价 / 影响 |
|---|---|---|
| **主站与从站在同一库、共用链路段** | 组帧/解帧/BCD/地址只有一份实现，回环可自测 | 二进制略大；单角色项目可只链接需要的 `src` 文件 |
| **以非阻塞状态机为核心，阻塞 API 只是轮询封装** | 核心可在 UART 中断/裸机主循环里跑，无阻塞、无线程 | 阻塞 API 依赖 `port.now_ms`/`delay_ms`；核心本身不 sleep |
| **HAL 用函数指针，串口只做可选参考实现** | 核心零 OS 依赖、易移植；平台差异隔离在 `port/` | 使用者需自行提供 4 个回调；`send`/`recv` 语义必须严格遵守 |
| **C99 + 无动态内存 + 固定缓冲** | 嵌入式友好、内存可预测、无碎片 | 上限编译期确定（`DLT645_MAX_DATA=200`）；超长数据靠多帧续读，而非放大缓冲 |
| **单事务模型（无内部队列）** | 状态机简单、RAM 占用固定 | 同一实例同时只能有一笔事务；并发/排队由调用方负责 |
| **DI 目录用“掩码通配”而非全展开** | 费率×结算日×象限全展开会有数万条表 | 范围项名称是“基项名 + `(费率N)/(上N结算日)/(数据块)` 后缀”，非逐值精确；附录 A 只示例性列出部分结算日，查询失败时逐级放宽（忽略费率/结算日） |
| **地址字符串按十进制数值解释（`b[0]` = 最低两位）** | 与标准「地址低字节在前」一致，匹配电表面板显示 | 若上游给的是“线路字节序”字符串，需直接填 `dlt645_addr_t.b[]` |
| **从站数据模型为 model-owned 的 `指针 + 长度`** | 库不复制、不拥有数据，省去大缓冲 | 回调返回的指针必须在下一次调用前保持有效 |
| **校验只用标准定义的偶校验 + 模 256 和，不额外加 CRC** | 保证与现网电表互操作 | 检错能力弱于 CRC，链路可靠性依赖重试与超时 |
| **自动续帧仅限读数据** | `12H` 续读是读路径的规范行为；写/控制类命令无此语义 | 读请求可能触发多次 `on_frame` 回调，调用方需按序累加（见示例） |
| **错误统一用 `dlt645_status_t` 返回码** | 无全局状态、可重入、易测试 | 调用方需检查返回值；不使用 `errno` |

明确的非目标：线程安全（请自行加锁）、内置重传之外的流控、多主机仲裁、
非标准厂家扩展标识的语义解析。

---

## 许可证

MIT，见 [LICENSE](LICENSE)。
