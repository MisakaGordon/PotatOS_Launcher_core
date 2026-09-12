# potato-launcher

一个用 C++17 编写的极简 Minecraft 启动器。它从 HMCL 的启动器实现中学习而来，
重新实现了「身份验证 → 读取版本清单 → 补全缺失的库 → 组装 java 命令 → 解压原生库 →
启动游戏 → 监控输出与退出码」的完整链路。目前支持 Linux/macOS（Windows 进程路径尚未实现），
登录支持 yggdrasil 与 offline，并可为离线账户提供自定义皮肤。

技术细节（与 HMCL 的逐项对照、目录结构、构建方式、验证记录等）见
[`DETAILS.md`](DETAILS.md)。

## 启动样例

`--game-dir`（`.minecraft` 目录）与 `--version`（版本 ID）是必填参数。启动前需确认
已安装对应版本（`versions/<id>/<id>.json` 与 `<id>.jar`），并按版本要求选择 Java
（如 1.20.4 需 Java 17，26.2 需 Java 25），可用 `--java` 指定；不指定时按清单要求
自动查找。清单中缺失的库文件会在启动前自动补全（默认镜像优先，见下）。

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

authlib-injector 会在缺失时自动下载（默认镜像优先，官方源
[authlib-injector.yushi.moe](https://authlib-injector.yushi.moe/)，镜像
`https://bmclapi2.bangbang93.com/mirrors/authlib-injector`），无需手动准备：

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login offline --username Player --skin skin.png --skin-model slim \
    --max-mem 2048
```

默认下载到 `<game-dir>/authlib-injector.jar`；也可用 `--authlib-injector PATH`
指定其它位置（缺失时同样会下载到该路径）。

启动器会在本地起一个 yggdrasil 服务器，通过 `-javaagent` 注入 authlib-injector，
把离线玩家皮肤签名后提供给游戏。

### 直接指定凭据（绕过账户管理）

也兼容手动指定 AuthInfo 的旧写法：

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --username Player --uuid 069a79f4-44e9-4726-a5be-fca90e38aaf5 \
    --access-token your-token --user-type mojang --max-mem 4096
```

## 配置文件

除了手打参数，也可以把参数写进 JSON。参数按优先级从低到高叠加：

```
内置默认 < 全局配置 < 实例配置 < --config 文件 < 命令行参数
```

命令行里显式给出的参数始终覆盖 JSON 中的同名项。

| 层 | 路径 | 说明 |
|----|------|------|
| 全局 | `$XDG_CONFIG_HOME/potato-launcher/config.json`（未设置则 `~/.config/potato-launcher/config.json`） | 适用于大部分实例的通用参数 |
| 实例 | `<game-dir>/versions/<id>/potato.json` | 某个实例的特殊参数（模板） |
| 指定 | `--config FILE` | 手动指定，可重复，按顺序叠加 |

```jsonc
{
  "gameDir": "~/.minecraft",
  "version": "1.20.4",
  "launch": {
    "java": "java",
    "maxMemory": 4096,
    "minMemory": 1024,
    "width": 1280, "height": 720,
    "priority": "normal",
    "downloadSource": "mirror",
    "javaArgs": ["-Dfoo=bar"],
    "env": { "MESA_LOADER_DRIVER_OVERRIDE": "iris" }
  },
  "auth": {
    "login": "offline",
    "username": "Player",
    "account": "",
    "authServer": "https://authserver.mojang.com"
  },
  "skin": { "file": "", "model": "wide", "authlibInjector": "" },
  "proxy": { "host": "", "port": 0, "username": "" },
  "mode": { "launchScript": "", "printCommand": false }
}
```

- 生成模板：`--init-config [PATH]` 写出包含全部键的默认配置（默认写到全局路径，已存在则不覆盖）。
- 关闭自动加载：`--no-config` 忽略全局与实例配置（显式 `--config` 仍然生效）。
- 数组（如 `javaArgs`/`gameArgs`）按整段替换；`env` 为对象，逐键合并。
- 出于安全考虑，密码、access token、proxy 密码等敏感项不会写入也不会从 JSON 读取，
  仍通过 `--password` / `--access-token` / `--proxy-pass` 或 `potato-accounts.json` 提供。

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
| `--skin FILE` | 要提供的 png 皮肤 |
| `--skin-model MODEL` | 皮肤模型：`wide`（默认）或 `slim` |
| `--authlib-injector PATH` | `authlib-injector.jar` 路径（默认 `<game-dir>/authlib-injector.jar`，缺失时自动下载） |

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

### 缺失库自动补全

启动前会按 `version.json` 的 `downloads` / `url` 元数据检查每个适用于当前系统的库
（含 `liblwjgl`、原生分类器等），缺失的自动下载到 `.minecraft/libraries`；下载经过
`.part` 临时文件并在校验 SHA-1 后原子替换，不会破坏已有文件。默认**镜像优先**
（BMCLAPI），失败再回退 Mojang 源。

| 参数 | 说明 |
|------|------|
| `--no-download` | 关闭自动补全（不联网，缺失库照旧跳过） |
| `--verify-files` | 对已存在的库校验 SHA-1，不一致则重新下载 |
| `--download-source SRC` | `mirror`（默认，镜像优先）或 `mojang`（官方源优先） |
| `--download-server URL` | 覆盖镜像根地址（默认 BMCLAPI） |

> 补全发生在 `--launch-script` 与真正启动之前；`--print-command` 不触发下载。
> 下载同样使用系统 `curl`，并遵循 `--proxy-*` 代理设置。

使用离线皮肤时，缺失的 `authlib-injector.jar` 也会按同样的镜像/官方源策略自动下载
（校验 SHA-256），存放于 `--authlib-injector` 指定路径或 `<game-dir>/authlib-injector.jar`；
`--no-download` 会一并禁止该下载。

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
| `--launch-script PATH` | 生成可执行的 bash 启动脚本并退出（同样会先补全库、解压原生库） |
| `--print-command` | 打印组装好的命令行并退出（不下载、不解压） |
| `--help` | 显示帮助 |

### 配置

| 参数 | 说明 |
|------|------|
| `--config FILE` | 从 JSON 文件加载参数（可重复，按顺序叠加） |
| `--no-config` | 忽略全局与实例 JSON（显式 `--config` 仍生效） |
| `--init-config [PATH]` | 写出默认配置并退出（默认写到全局路径） |

> 系统依赖：HTTP 与库下载使用系统 `curl`（可用 `POTATO_CURL` 指定），离线皮肤签名使用
> `openssl`（可用 `POTATO_OPENSSL` 指定），游戏需已安装对应 Java。
