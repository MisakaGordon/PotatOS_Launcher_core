# potato-launcher

一个用 C++ 编写的极简 Minecraft 启动器。它从 HMCL
(`hmcl_basement/launch/`、`hmcl_basement/auth/`) 的启动器实现中学习而来，用 C++17
重新实现了 "身份验证 → 读取版本清单 → 组装 java 命令 → 解压原生库 → 启动游戏 →
监控输出与退出码" 的完整链路。

## 功能

对应 HMCL `org.jackhuang.hmcl.launch` 包中的核心概念：

| HMCL | 本项目 | 说明 |
|------|--------|------|
| `DefaultLauncher.generateCommandLine` | `launcher.cpp:generate_command_line` | 组装完整 java 命令行 |
| `LaunchOptions` | `launcher.h::LaunchOptions` | 内存/分辨率/优先级/代理等启动选项 |
| `AuthInfo` | `launcher.h::AuthInfo` | 玩家名 / uuid / access token |
| `Arguments.parseArguments` | `command.cpp` | `${...}` 占位符替换 |
| `Rule` / `Rules.isAllowed` | `manifest.cpp::check_rules` | os / features 规则判定 |
| `GameVersionManifest` | `manifest.h::VersionManifest` | version.json 解析 |
| `Unzipper` + `decompressNatives` | `zip.cpp` + `launcher.cpp::decompress_natives` | 原生库解压（含 extract.exclude） |
| `StreamPump` / `ExitWaiter` | `process.cpp` | stdout/stderr 泵与退出分类 |
| `ProcessListener.ExitType` | `process.h::ExitType` | NORMAL / APPLICATION_ERROR / JVM_ERROR / KILLED |
| `makeLaunchScript` | `launcher.cpp::make_launch_script` | 生成 bash 启动脚本 |
| `CommandBuilder.addDefault` | `command.h::CommandBuilder` | 默认参数不覆盖用户参数 |

具体能力：

- 解析 `version.json`：libraries、rules、jvm/game arguments、`minecraftArguments`（旧版）、assetIndex、javaVersion
- 生成 JVM 参数：`-Xmx/-Xms`、`-XX:MetaspaceSize`、文件编码、log4j2 安全加固、G1GC 调优、`-Dminecraft.client.jar`、`-Duser.home`、代理
- 规则与 features：`has_custom_resolution`（`--width/--height`）等
- 原生库解压到 `versions/<id>/<id>-natives-<platform>/`，支持 `extract.exclude`、跳过 `.sha1/.git`、跳过符号链接、同尺寸文件跳过
- 进程管理：fork/exec、stdout/stderr 泵线程、退出分类（崩溃报告 → APPLICATION_ERROR、137 → KILLED、JVM 初始化失败 → JVM_ERROR）
- 环境变量注入：`INST_NAME` / `INST_ID` / `INST_DIR` / `INST_MC_DIR` / `INST_JAVA`
- pre-launch / post-exit 命令、wrapper、`nice` 进程优先级、代理
- `--launch-script` 生成可执行 bash 脚本；`--print-command` 打印命令

## 目录结构

```
src/
  platform.h/.cpp   操作系统 / 路径 / 环境变量 / shell 转义
  manifest.h/.cpp   version.json 数据模型与解析、规则判定
  command.h/.cpp    命令行组装（CommandBuilder）、占位符替换
  process.h/.cpp    进程 spawn、StreamPump、ExitWaiter、退出分类
  zip.h/.cpp        miniz 封装：原生库 zip 解压
  launcher.h/.cpp   LaunchOptions / AuthInfo / DefaultLauncher
  main.cpp          CLI 入口
vendor/
  nlohmann/json.hpp JSON 解析（v3.11.3）
  miniz/            zip/inflate（miniz master）
hmcl_basement/      参考的 HMCL 源码（未修改）
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

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --username Player --uuid 069a79f4-44e9-4726-a5be-fca90e38aaf5 \
    --access-token your-token --max-mem 4096 --java /usr/lib/jvm/java-17/bin/java
```

离线游玩：

```sh
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --username Player --uuid 00000000-0000-0000-0000-000000000000 \
    --access-token 0 --user-type offline --max-mem 2048
```

常用选项见 `--help`。

## 登录（auth 模块）

对应 HMCL `org.jackhuang.hmcl.auth` 包：

| HMCL | 本项目 | 说明 |
|------|--------|------|
| `YggdrasilService` | `auth/yggdrasil.{h,cpp}` | authenticate / refresh / validate / invalidate |
| `YggdrasilSession` | `auth/yggdrasil.h::YggdrasilSession` | accessToken / clientToken / selectedProfile / userProperties |
| `YggdrasilAccount.logIn` | `auth/yggdrasil.cpp::YggdrasilAccount::log_in` | validate → refresh → CredentialExpiredException |
| `RemoteAuthenticationException` | `auth/auth.h` | `{error, errorMessage, cause}` 结构化错误 |
| `OfflineAccountFactory.getUUIDFromUserName` | `auth/offline.cpp::offline_uuid_for` | `UUID.nameUUIDFromBytes("OfflinePlayer:"+name)` 精确一致 |
| 离线皮肤 `YggdrasilServer` | `auth/authserver.{h,cpp}` | 本地 yggdrasil API + SHA1withRSA 签名 |
| `accounts.json` | `auth/accountstore.{h,cpp}` | 账户持久化与复用 |

两种登录方式：

- **yggdrasil**：`--login yggdrasil --username X --password Y`
  （`--auth-server` / `--session-server` 可指向第三方验证服务器）
- **offline**：`--login offline --username X`（UUID 由名字推导，可 `--uuid` 指定）

依赖：HTTP 使用系统 `curl` 二进制；离线皮肤的 RSA 签名使用 `openssl` 命令行；
哈希（MD5/SHA-1/SHA-256）与 base64 为 C++ 自带实现。

```sh
# 离线
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login offline --username Player --max-mem 2048

# yggdrasil（Mojang 账号）
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login yggdrasil --username you@example.com --password ... --max-mem 2048

# 使用已保存账户（自动 validate/refresh）
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --account account:xxxx --max-mem 2048

# 离线 + 皮肤（需要 authlib-injector.jar）
./potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
    --login offline --username Player --skin skin.png --skin-model slim \
    --authlib-injector ~/authlib-injector.jar --max-mem 2048
```

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
