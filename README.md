# potato-launcher

一个用 C++17 编写的极简 Minecraft 启动器。它从 HMCL 的启动器实现中学习而来，
重新实现了「身份验证 → 读取版本清单 → 组装 java 命令 → 解压原生库 → 启动游戏 →
监控输出与退出码」的完整链路。目前支持 Linux/macOS（Windows 进程路径尚未实现），
登录支持 yggdrasil 与 offline，并可为离线账户提供自定义皮肤。

技术细节（与 HMCL 的逐项对照、目录结构、构建方式、验证记录等）归档在本地文件
`DETAILS.md` 中（该文件未纳入版本管理）。

## 启动样例

`--game-dir`（`.minecraft` 目录）与 `--version`（版本 ID）是必填参数。启动前需确认
已安装对应版本（`versions/<id>/<id>.json` 与 `<id>.jar`），并按版本要求选择 Java
（如 1.20.4 需 Java 17，26.2 需 Java 25），可用 `--java` 指定；不指定时按清单要求
自动查找。

### 离线游玩

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login offline --username Player --max-mem 2048
```

UUID 由玩家名按 Java 规则推导（`OfflinePlayer:` + 名字的 name-based MD5），也可用
`--uuid` 覆盖。

### yggdrasil 登录（Mojang 账号）

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login yggdrasil --username you@example.com --password ... --max-mem 2048
```

`--auth-server` / `--session-server` 可指向第三方 yggdrasil 兼容服务器
（如 LittleSkin 等皮肤站）。

### 使用已保存账户

登录时加 `--save-account` 会把账户写入 `potato-accounts.json`（默认在游戏目录下），
之后直接按账户 ID 启动，启动器会自动 validate，过期则 refresh：

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --account account:xxxx --max-mem 2048
```

### 离线 + 自定义皮肤

需要下载 [authlib-injector](https://authlib-injector.yushi.moe/)：

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login offline --username Player --skin skin.png --skin-model slim \
    --authlib-injector ~/authlib-injector.jar --max-mem 2048
```

启动器会在本地起一个 yggdrasil 服务器，通过 `-javaagent` 注入 authlib-injector，
把离线玩家皮肤签名后提供给游戏。

### 直接指定凭据（绕过账户管理）

也兼容手动指定 AuthInfo 的旧写法：

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --username Player --uuid 069a79f4-44e9-4726-a5be-fca90e38aaf5 \
    --access-token your-token --user-type mojang --max-mem 4096
```

## 软件参数说明

### 必填参数

| 参数 | 说明 |
|------|------|
| `--game-dir DIR` | `.minecraft` 目录 |
| `--version ID` | 版本 ID（读取 `versions/<id>/<id>.json`） |

### 登录与账户

| 参数 | 说明 |
|------|------|
| `--login METHOD` | 登录方式：`offline` 或 `yggdrasil` |
| `--username NAME` | 玩家名 / 登录邮箱 |
| `--password PASS` | yggdrasil 密码（`--login yggdrasil` 时必填） |
| `--uuid UUID` | offline：显式指定玩家 UUID（默认由玩家名推导） |
| `--account ID` | 使用已保存账户（如 `account:xxxx`），自动 validate / refresh |
| `--account-store PATH` | 账户存储路径（默认 `<game-dir>/potato-accounts.json`） |
| `--save-account` | 登录成功后持久化账户 |
| `--auth-server URL` | yggdrasil 认证地址（默认 `https://authserver.mojang.com`） |
| `--session-server URL` | yggdrasil session 地址（默认 `https://sessionserver.mojang.com`） |

> 不填 `--login` / `--account` 时，`--username`/`--uuid`/`--access-token`/
> `--user-type` 作为手动 AuthInfo 使用（无账户管理）。

### 离线皮肤

| 参数 | 说明 |
|------|------|
| `--skin FILE` | 要提供的 png 皮肤（需要 `--authlib-injector`） |
| `--skin-model MODEL` | 皮肤模型：`wide`（默认）或 `slim` |
| `--authlib-injector PATH` | `authlib-injector.jar` 的路径 |

### 内存与 JVM

| 参数 | 说明 |
|------|------|
| `--max-mem MB` | `-Xmx` 最大堆内存 |
| `--min-mem MB` | `-Xms` 初始堆内存（不超过 max-mem） |
| `--metaspace MB` | `-XX:MetaspaceSize`（Java 8+；旧版为 `-XX:PermSize`） |
| `--java PATH` | java 可执行文件（默认按清单自动查找匹配版本） |
| `--java-arg ARG` | 追加 JVM 参数（可重复） |
| `--override-java-arg ARG` | 替换启动器生成的同名参数（可重复） |
| `--priority LEVEL` | 进程优先级：`high` / `abovenormal` / `normal` / `belownormal` / `low` |
| `--no-generated-jvm-args` | 禁用启动器生成的 JVM 默认参数 |
| `--no-optimizing-jvm-args` | 禁用 G1GC/JIT 等调优参数 |

### 窗口

| 参数 | 说明 |
|------|------|
| `--width W --height H` | 自定义分辨率（会启用 `has_custom_resolution`） |
| `--fullscreen` | 全屏启动 |

### 游戏

| 参数 | 说明 |
|------|------|
| `--game-arg ARG` | 追加游戏参数（可重复） |
| `--server HOST[:PORT]` | 启动后直连服务器（1.20.5+ 自动改用 quickPlay，或加 `--quick-play`） |
| `--quick-play` | 使用 `--quickPlayMultiplayer` 参数（1.20.5+） |

### 原生库

| 参数 | 说明 |
|------|------|
| `--natives-dir DIR` | 覆盖原生库解压目录 |
| `--use-custom-natives` | 跳过原生库解压（使用已有目录） |

### 进程与运行环境

| 参数 | 说明 |
|------|------|
| `--env VAR=VAL` | 注入环境变量（可重复） |
| `--wrapper CMD` | 包装 java 命令（如 `gamemoderun`） |
| `--pre-launch-command CMD` | 启动前执行的命令（非零退出则中止启动） |
| `--post-exit-command CMD` | 游戏退出后执行的命令 |

### 代理

| 参数 | 说明 |
|------|------|
| `--proxy-host HOST --proxy-port PORT` | HTTP 代理 |
| `--proxy-user U --proxy-pass P` | 代理认证（可选） |

### 运行模式

| 参数 | 说明 |
|------|------|
| `--launch-script PATH` | 生成可执行的 bash 启动脚本并退出（同样会先解压原生库） |
| `--print-command` | 打印组装好的命令行并退出 |
| `--help` | 显示帮助 |

> 系统依赖：HTTP 使用系统 `curl`（可用 `POTATO_CURL` 指定），离线皮肤签名使用
> `openssl`（可用 `POTATO_OPENSSL` 指定），游戏需已安装对应 Java。
