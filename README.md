# potato-launcher

一个用 C++ 编写的极简 Minecraft 启动器。它从 HMCL
(`hmcl_basement/launch/`、`hmcl_basement/auth/`) 的启动器实现中学习而来，用 C++17
重新实现了 "身份验证 → 读取版本清单 → 组装 java 命令 → 解压原生库 → 启动游戏 →
监控输出与退出码" 的完整链路。

## 功能

对应 HMCL 的核心概念：

**启动（launch）**

| HMCL | 本项目 | 说明 |
|------|--------|------|
| `DefaultLauncher.generateCommandLine` | `launcher.cpp:generate_command_line` | 组装完整 java 命令行 |
| `LaunchOptions` | `launcher.h::LaunchOptions` | 内存/分辨率/优先级/代理等启动选项 |
| `AuthInfo` | `auth/auth.h::AuthInfo` | 玩家名 / uuid / access token |
| `Arguments.parseArguments` | `command.cpp` | `${...}` 占位符替换 |
| `Rule` / `Rules.isAllowed` | `manifest.cpp::check_rules` | os / features 规则判定 |
| `GameVersionManifest` | `manifest.h::VersionManifest` | version.json 解析 |
| `Unzipper` + `decompressNatives` | `zip.cpp` + `launcher.cpp::decompress_natives` | 原生库解压（含 extract.exclude） |
| `StreamPump` / `ExitWaiter` | `process.cpp` | stdout/stderr 泵与退出分类 |
| `ProcessListener.ExitType` | `process.h::ExitType` | NORMAL / APPLICATION_ERROR / JVM_ERROR / KILLED |
| `makeLaunchScript` | `launcher.cpp::make_launch_script` | 生成 bash 启动脚本 |
| `CommandBuilder.addDefault` | `command.h::CommandBuilder` | 默认参数不覆盖用户参数 |

**登录（auth）**

| HMCL | 本项目 | 说明 |
|------|--------|------|
| `YggdrasilService` | `auth/yggdrasil.{h,cpp}` | authenticate / refresh / validate / invalidate |
| `YggdrasilSession` | `auth/yggdrasil.h::YggdrasilSession` | accessToken / clientToken / selectedProfile / userProperties |
| `YggdrasilAccount.logIn` | `auth/yggdrasil.cpp::YggdrasilAccount::log_in` | validate → refresh → CredentialExpiredException |
| `RemoteAuthenticationException` | `auth/auth.h` | `{error, errorMessage, cause}` 结构化错误 |
| `OfflineAccountFactory.getUUIDFromUserName` | `auth/offline.cpp::offline_uuid_for` | `UUID.nameUUIDFromBytes("OfflinePlayer:"+name)` 精确一致 |
| 离线皮肤 `YggdrasilServer` | `auth/authserver.{h,cpp}` | 本地 yggdrasil API + SHA1withRSA 签名 |
| `accounts.json` | `auth/accountstore.{h,cpp}` | 账户持久化与复用 |

具体能力：

- 解析 `version.json`：libraries、rules、jvm/game arguments、`minecraftArguments`（旧版）、assetIndex、javaVersion
- 生成 JVM 参数：`-Xmx/-Xms`、`-XX:MetaspaceSize`、文件编码、log4j2 安全加固、G1GC 调优、`-Dminecraft.client.jar`、`-Duser.home`、代理
- 规则与 features：`has_custom_resolution`（`--width/--height`）等
- 原生库解压到 `versions/<id>/<id>-natives-<platform>/`，支持 `extract.exclude`、跳过 `.sha1/.git`、跳过符号链接、同尺寸文件跳过
- 进程管理：fork/exec、stdout/stderr 泵线程、退出分类（崩溃报告 → APPLICATION_ERROR、137 → KILLED、JVM 初始化失败 → JVM_ERROR）
- 环境变量注入：`INST_NAME` / `INST_ID` / `INST_DIR` / `INST_MC_DIR` / `INST_JAVA`
- pre-launch / post-exit 命令、wrapper、`nice` 进程优先级、代理
- `--launch-script` 生成可执行 bash 脚本；`--print-command` 打印命令
- 登录：yggdrasil（账号+密码）与 offline（离线）两种方式，支持账户持久化与 token 自动续期

## 目录结构

```
src/
  platform.h/.cpp   操作系统 / 路径 / 环境变量 / shell 转义
  manifest.h/.cpp   version.json 数据模型与解析、规则判定
  command.h/.cpp    命令行组装（CommandBuilder）、占位符替换
  process.h/.cpp    进程 spawn、StreamPump、ExitWaiter、退出分类
  zip.h/.cpp        miniz 封装：原生库 zip 解压
  launcher.h/.cpp   LaunchOptions / DefaultLauncher
  auth/
    auth.h           AuthInfo、异常层级、Account 接口
    yggdrasil.*      yggdrasil 登录（authenticate/refresh/validate）
    offline.*        离线登录（离线 UUID 推导）
    accountstore.*   账户持久化（potato-accounts.json）
    authserver.*     离线皮肤的本地 yggdrasil 服务器
    crypto.*         MD5/SHA-1/SHA-256、base64、UUID
    http.*           基于 curl 二进制的 HTTP 客户端
  main.cpp          CLI 入口
vendor/
  nlohmann/json.hpp JSON 解析（v3.11.3）
  miniz/            zip/inflate（miniz master）
hmcl_basement/      参考的 HMCL 源码（未修改，不随仓库提交）
```

## 构建

需要 `g++`/`clang++`（C++17）和 `make` 或 `cmake`。

```sh
# 方式一：make
make

# 方式二：cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 用法

`--game-dir`（`.minecraft` 目录）和 `--version`（版本 ID）是必填参数。启动游戏前需
确认已安装对应版本（`versions/<id>/<id>.json` 与 `<id>.jar`），并按版本要求选择
Java（如 1.20.4 需 Java 17，26.2 需 Java 25），可用 `--java` 指定。

### 离线游玩

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login offline --username Player --max-mem 2048
```

UUID 由玩家名按 Java 规则推导（`OfflinePlayer:` + 名字的 name-based MD5），
也可用 `--uuid` 覆盖。

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

### 依赖

- HTTP 使用系统 `curl` 二进制（可用环境变量 `POTATO_CURL` 指定路径）
- 离线皮肤的 RSA 签名使用 `openssl` 命令行（可用 `POTATO_OPENSSL` 指定路径）
- 哈希（MD5/SHA-1/SHA-256）与 base64 为 C++ 自带实现
- Java 自动检测：不传 `--java` 时按 manifest 所需版本自动查找
  （先查 PATH 的 `java`、`JAVA_HOME`，再扫 `/usr/lib/jvm`、macOS
  `JavaVirtualMachines`、Windows `Program Files` 等常见位置）

其余选项（分辨率、服务器直连、代理、优先级、启动脚本等）见 `--help`。

## 在其他机器上运行

启动器本身不绑定某台机器，只需满足以下运行时依赖：

| 依赖 | 用途 | 缺失时的处理 |
|------|------|--------------|
| `curl` | yggdrasil 登录的 HTTP 请求 | 报错提示安装，或 `POTATO_CURL=/path/to/curl` |
| `openssl` | 离线皮肤的签名 | 报错提示安装，或 `POTATO_OPENSSL=/path/to/openssl` |
| Java | 运行游戏 | 不传 `--java` 时自动检测匹配版本 |

- 临时文件使用系统临时目录（`TMPDIR`/`TEMP`/`TMP`），不写死 `/tmp`
- 随机数用 `std::random_device`，不依赖 `/dev/urandom`
- 离线皮肤依赖外部 `authlib-injector.jar`，各机器需自行准备

**尚未实现**：Windows 进程管理（fork/exec → CreateProcess）仍为 TODO，目前只在
Linux/macOS 上运行；Windows 需要补充进程分支与 `.bat`/`.ps1` 脚本生成。

## 真实环境验证

已在 `/home/misaka/.minecraft/` 的 **Minecraft 26.2**（Java 25，LWJGL 3.4.1）
副本上实测通过：

- 正确解析 26.2 新格式 manifest（natives 分类器内嵌在 `name` 中、由 LWJGL 运行时自解压，
  全部 68 个 Linux 适用库进入 classpath）
- 游戏完整启动至主界面：Datafixer → LWJGL/OpenGL (Mesa Iris Xe) → 资源加载 → OpenAL 音效 → 纹理图集
- LWJGL/JNA/Netty 在 `${natives_directory}/lwjgl|jna|netty` 运行时自解压成功
- 生成 `-Dlog4j.configurationFile` 并写出 `log4j2.xml`（此前的 bug：误用 assetIndex.id
  判断版本号，已改为使用游戏版本号）
- JVM 参数错误（如缺 `-XX:+UnlockExperimentalVMOptions`）被正确归类为 `JVM_ERROR` 并输出诊断
- 离线登录（含自定义皮肤）正常进入主界面，皮肤签名通过 Java `SHA1withRSA` 验证，
  authlib-injector 注入成功且无签名校验警告
- yggdrasil 流程经 mock 服务器验证：正确登录、错误密码报
  `ForbiddenOperationException`、过期 token 自动 refresh

## 与 HMCL 的差异（简化点）

- 未实现微软（MSA/OAuth）登录——按任务要求跳过
- 未实现版本下载、资源文件管理、模组解析（Forge/Fabric 组件检测）
- 只实现 POSIX（Linux/macOS）进程路径；Windows 需要补充 CreateProcess 分支
- 未实现 `-DignoreList` 的 BootstrapLauncher 重写、显卡/渲染器环境变量、Mesa/Vulkan 驱动注入
- log4j2.xml 为内嵌的最小可用版本，而非 HMCL 资源文件
- 账户存储简化：元数据与凭据合并为单个 JSON 数组（HMCL 分 accounts.json + 私有数据）
- 离线皮肤依赖外部 `authlib-injector.jar`（需自行下载）与本地 yggdrasil 服务器

## 许可

本项目代码参考了 HMCL（GPL-3.0），学习用途。
