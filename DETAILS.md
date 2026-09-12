# potato-launcher 技术说明

> 本文件归档 README 之外的其余说明（与 HMCL 的对照、结构、构建、验证记录等）。
> 返回 [README.md](README.md)。

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

**下载 / 补全（download）**

| HMCL | 本项目 | 说明 |
|------|--------|------|
| `GameLibrariesTask` | `launcher.cpp::ensure_libraries` | 启动前检查/补全缺失的库 |
| `LibraryDownloadTask` | `download.cpp::download_file` | 单库下载（SHA-1 校验、原子替换） |
| `DownloadInfo` / `LibraryDownloadInfo` | `manifest.h::DownloadInfo` | `path/url/sha1/size` 元数据 |
| `Library.getPath` / `getDownload` | `manifest.cpp::Library::download_path/download_url/download_sha1` | 解析库的落盘路径与下载源 |
| `BMCLAPIDownloadProvider.injectURL` | `download.cpp::mirror_url` | Mojang/Forge/Maven → BMCLAPI 镜像映射 |
| `FileDownloadTask` 的临时文件 + 校验 | `download.cpp::download_file` | `.part-*` 临时文件，校验通过后 `rename` |
| `AuthlibInjectorExtractor` | `download.cpp::download_authlib_injector` | 取 latest.json（镜像/官方）并下载 jar（SHA-256） |

**配置（config）**

| HMCL | 本项目 | 说明 |
|------|--------|------|
| `Settings` / `GameSettings` | `config.h::CliOptions` | 全部启动设置的聚合 |
| 启动器设置文件 | `config.cpp::global_config_path` | 全局 JSON（XDG） |
| 实例设置 | `config.cpp::instance_config_path` | 每实例 `<id>/potato.json` 模板 |
| 设置项校验 | `main.cpp::finalize` + `config.cpp::apply_config_json` | 枚举/必填校验 |
| `--config` / `--no-config` / `--init-config` | `main.cpp::parse_args` + `config.cpp` | 手动指定、忽略自动层、生成默认配置 |

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
- 解析库下载元数据：`url`、`downloads.artifact`、`downloads.classifiers`、`checksums`；落盘路径优先采用 manifest 的 `path`
- 启动前补全缺失库（含 native 分类器）：存在性检查（`--verify-files` 时校验 SHA-1），
  镜像优先/官方优先，候选源逐个尝试；下载走 `.part` 临时文件，校验后原子替换
- 离线皮肤所需 authlib-injector 缺失时自动补全：读取 `latest.json`（镜像
  `.../mirrors/authlib-injector` 或官方 `authlib-injector.yushi.moe`），校验 SHA-256，
  默认落到 `<game-dir>/authlib-injector.jar`；`--no-download` 可禁用
- 生成 JVM 参数：`-Xmx/-Xms`、`-XX:MetaspaceSize`、文件编码、log4j2 安全加固、G1GC 调优、`-Dminecraft.client.jar`、`-Duser.home`、代理
- 规则与 features：`has_custom_resolution`（`--width/--height`）等
- 原生库解压到 `versions/<id>/<id>-natives-<platform>/`，支持 `extract.exclude`、跳过 `.sha1/.git`、跳过符号链接、同尺寸文件跳过
- 进程管理：fork/exec、stdout/stderr 泵线程、退出分类（崩溃报告 → APPLICATION_ERROR、137 → KILLED、JVM 初始化失败 → JVM_ERROR）
- 环境变量注入：`INST_NAME` / `INST_ID` / `INST_DIR` / `INST_MC_DIR` / `INST_JAVA`
- pre-launch / post-exit 命令、wrapper、`nice` 进程优先级、代理
- `--launch-script` 生成可执行 bash 脚本；`--print-command` 打印命令
- 分层 JSON 配置：默认 < 全局（XDG）< 实例（`versions/<id>/potato.json`）<
  `--config`（可重复）< 命令行；`--no-config` 关闭自动层；`--init-config` 生成默认模板；
  敏感项（password/accessToken/userType/proxy password）只从命令行读取
- 登录：yggdrasil（账号+密码）与 offline（离线）两种方式，支持账户持久化与 token 自动续期

## 目录结构

```
src/
  platform.h/.cpp   操作系统 / 路径 / 环境变量 / shell 转义
  manifest.h/.cpp   version.json 数据模型与解析、规则判定
  command.h/.cpp    命令行组装（CommandBuilder）、占位符替换
  process.h/.cpp    进程 spawn、StreamPump、ExitWaiter、退出分类
  zip.h/.cpp        miniz 封装：原生库 zip 解压
  download.h/.cpp   缺失库下载：镜像映射、curl 取文件、SHA-1 校验
  config.h/.cpp     分层 JSON 配置：CliOptions、路径、apply/to_json
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

## 依赖

- HTTP 使用系统 `curl` 二进制（可用环境变量 `POTATO_CURL` 指定路径）
- 库下载同样使用系统 `curl`（`-fL`，失败回退候选源）
- 离线皮肤的 RSA 签名使用 `openssl` 命令行（可用 `POTATO_OPENSSL` 指定路径）
- 哈希（MD5/SHA-1/SHA-256）与 base64 为 C++ 自带实现
- Java 自动检测：不传 `--java` 时按 manifest 所需版本自动查找
  （先查 PATH 的 `java`、`JAVA_HOME`，再扫 `/usr/lib/jvm`、macOS
  `JavaVirtualMachines`、Windows `Program Files` 等常见位置）

## 在其他机器上运行

启动器本身不绑定某台机器，只需满足以下运行时依赖：

| 依赖 | 用途 | 缺失时的处理 |
|------|------|--------------|
| `curl` | yggdrasil 登录与库下载的 HTTP 请求 | 报错提示安装，或 `POTATO_CURL=/path/to/curl` |
| `openssl` | 离线皮肤的签名 | 报错提示安装，或 `POTATO_OPENSSL=/path/to/openssl` |
| Java | 运行游戏 | 不传 `--java` 时自动检测匹配版本 |

- 临时文件使用系统临时目录（`TMPDIR`/`TEMP`/`TMP`），不写死 `/tmp`
- 随机数用 `std::random_device`，不依赖 `/dev/urandom`
- 离线皮肤依赖外部 `authlib-injector.jar`，各机器需自行准备

## 尚未实现

- Windows 进程管理（fork/exec → CreateProcess）仍为 TODO，目前只在
  Linux/macOS 上运行；Windows 需要补充进程分支与 `.bat`/`.ps1` 脚本生成。
- 资源补全仅覆盖 libraries；客户端主 jar（`downloads.client`）、资源索引与
  assets 对象（`assets/indexes`、`assets/objects`）仍不自动下载。
- 下载为串行执行（未做 HMCL 的多线程并发下载）。

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

缺失库补全功能在以下实例上验证：

- **Minecraft 26.2**（`/home/misaka/.minecraft/`）：删除 `at.yawk.lz4:lz4-java`
  后，默认镜像优先与 `--download-source mojang` 均成功补全，SHA-1 与 manifest 一致；
  截断文件 + `--verify-files` 正确识别 checksum mismatch 并重下；`--no-download`
  不联网；`--print-command` 不触发下载
- **MikotoFarming**（PCL2 生成的 1.20.1 Fabric 扁平清单，
  `/home/misaka/PCL2/NewStart20230818/.minecraft/`）：删除
  `com.mojang:blocklist:1.0.10` 后自动补全并还原；用 Java 21 离线启动，
  游戏经 Fabric Loader 0.14.22 → Datafixer → 资源加载 → OpenAL → 纹理图集，
  完整进入主界面（离线 401 为预期）

分层 JSON 配置功能验证：

- `--init-config PATH` 生成含全部非敏感键的默认配置，重复执行拒绝覆盖
- 全局（`XDG_CONFIG_HOME`）与实例 `versions/26.2/potato.json` 自动加载；
  优先级实测：全局 1111 < 实例 2222 < `--config` 3333 < 命令行 4444
- `--no-config` 忽略全局/实例但仍应用显式 `--config`
- 非法枚举（如 `priority: "urgent"`）与缺失的显式配置文件均报清晰错误
- 旧命令行用法回归通过（显式 `--game-dir/--version/--login/--username/--max-mem` 等）

authlib-injector 自动补全验证（离线皮肤、26.2 实例）：

- 未传 `--authlib-injector` 时，镜像优先与 `--download-source mojang` 均成功下载
  `1.2.8`，SHA-256 `9c7f4343…` 与官方一致，命令行注入
  `-javaagent:<path>=http://127.0.0.1:<port>`
- `--authlib-injector /tmp/my-injector.jar` 会下载到指定路径
- 文件已存在时不重复下载；`--no-download` 且缺失时给出明确错误

## 与 HMCL 的差异（简化点）

- 未实现微软（MSA/OAuth）登录——按任务要求跳过
- 未实现版本列表/版本安装、客户端主 jar 与 assets 的下载，以及模组解析
  （Forge/Fabric 组件检测）；仅实现缺失 libraries 的补全
- 镜像映射只覆盖库下载相关主机（libraries/forge/maven），非 HMCL 的完整替换表；
  下载串行，未做并发与缓存仓库
- 只实现 POSIX（Linux/macOS）进程路径；Windows 需要补充 CreateProcess 分支
- 未实现 `-DignoreList` 的 BootstrapLauncher 重写、显卡/渲染器环境变量、Mesa/Vulkan 驱动注入
- log4j2.xml 为内嵌的最小可用版本，而非 HMCL 资源文件
- 账户存储简化：元数据与凭据合并为单个 JSON 数组（HMCL 分 accounts.json + 私有数据）
- 配置无 GUI 设置界面与按实例的版本隔离策略，仅提供分层 JSON 文件；敏感项不落盘到 JSON
- 离线皮肤依赖本地 yggdrasil 服务器；authlib-injector 缺失时自动从官方/镜像下载
  （HMCL 为内置资源，本项目改为在线获取）

## 许可

本项目代码参考了 HMCL（GPL-3.0），学习用途。
