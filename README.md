# SUSFS4LKM

为 KSU LKM 适配 SUSFS，让 LKM 也能使用无敌环境

SUSFS4LKM 是一个 KernelSU 模块：它在设备上加载 SUSFS 的可加载内核模块（LKM），并给管理器提供配置界面与统一控制接口，用于隐藏路径、挂载、KSTAT、open redirect 等痕迹。

## 模块功能

- **LKM 后端**：以 `.ko` 动态加载 SUSFS，不需要替换内核或刷入镜像
- **后端优先级**：先探测内核已有后端（native 或已加载的 LKM），探测到就直接复用；只有确认“无后端”时才加载 LKM，避免二次加载
- **8 个 GKI KMI 变体**：android12-5.10 / android13-5.10 / android13-5.15 / android14-5.15 / android14-6.1 / android15-6.6 / android16-6.12 / android17-6.18，模块按内核版本自动选择
- **统一 JSON 控制入口**：`susfsctl --json '<单个 JSON 参数>'`，请求结构 `{operation, target, values}`，响应 `{ok, code, backend, version, capabilities, data, error}`，参数严格 POSIX 引用，helper 只操作白名单接口
- **固定退出码**：`0` 成功 / `2` 参数错误 / `3` 无后端 / `4` 版本或布局不支持 / `5` 权限错误 / `6` 协议错误 / `7` 内核业务失败，失败时 stdout 为空、诊断在 stderr
- **接口边界（实测）**：JSON 路径必须是以 `/` 开头的无空白字符串（含空格/换行/反斜杠会被判 `2`），拒绝 `..` 穿越与内嵌 NUL，单次请求受系统单个参数长度上限（约 128 KiB）限制，重复 key 按第一个取值
- **WebUI**：状态页显示集成模块版本与后端版本；高级页在后端不是 LKM 时会置灰入口并在页面内禁用全部控件（path / kstat / redirect 的删除清空、full-clone、模块名与挂载前缀列表、日志与 AVC、只读诊断）
- **保存与立即应用分离**：保存启动配置不会立刻改动运行态，立即应用不会写回配置
- **默认策略**：后端确认 ready 后才执行原模块默认策略与属性调整，单项失败独立记录，核心失败停止后续操作
- **远程更新**：模块内置 `updateJson` 指向本仓库 `update.json`，之后发布新版本可直接从仓库拉取
- **可停用可回滚**：`disable` 文件 + 重启即可停用；配置与状态都在独立目录
- **不干扰其他模块**：不修改 KernelSU、启动镜像与其他模块；不自动停用、卸载或覆盖其他模块，也不会重复加载或卸载已存在的 SUSFS LKM（外部模块自己管理的规则不属于本模块职责）

## 支持范围

| KMI 变体 | 内核模块来源 | 验证状态 |
| --- | --- | --- |
| android14-6.1 | 本仓库源码构建（含 6.1 专用补丁） | 已在 OPD2404 / kernel 6.1.141-android14-11 真机验收 |
| android12-5.10 / android13-5.10 / android13-5.15 / android14-5.15 / android15-6.6 / android16-6.12 / android17-6.18 | 本仓库源码构建（锁定 commit `85ef920c` + 每个变体固定 DDK 镜像 digest） | 仅构建、ELF/vermagic 与包结构校验，未在对应设备启动验证 |

> 下表以外的设备（非 GKI、预 5.10 内核、其他架构）不在本项目支持范围。相同 KMI 不等于相同内核 ABI，若加载失败请停用模块。

## 安装教程

### 前置条件

1. 已安装并能正常使用 KernelSU（管理器与 `ksud` 可用）
2. 设备为 arm64、GKI 内核，KMI 在上表支持范围内
3. 能访问 GitHub；受限网络请先准备代理或镜像下载方式

### 一、下载

从 Release 页面下载最新的 `SUSFS4LKM-arm64.zip`：

```
https://github.com/Xiaogugtfgdfhgrh/SUSFS4LKM/releases/latest/download/SUSFS4LKM-arm64.zip
```

建议同时下载 `SHA256SUMS` 校验：

```sh
sha256sum -c SHA256SUMS
```

### 二、安装

方式 A（管理器）：KernelSU 管理器 → 模块 → 从本地安装 → 选择 `SUSFS4LKM-arm64.zip`

方式 B（命令行）：

```sh
su -c 'ksud module install /sdcard/Download/SUSFS4LKM-arm64.zip'
```

安装脚本会把配置复制到 `/data/adb/susfs4ksu_unified`，之后重启生效。

### 三、重启并验证

```sh
su -c '/data/adb/modules/susfs4ksu_unified/bin/susfsctl probe'
```

期望输出：

```
lkm v2.3.0
```

查看后端状态与能力：

```sh
su -c '/data/adb/modules/susfs4ksu_unified/bin/susfsctl --json "{\"operation\":\"status\"}"'
```

### 四、打开 WebUI

在 KernelSU 管理器中打开本模块的 WebUI。首页显示集成模块版本与后端版本；只有 LKM 后端会启用高级页。

## 升级

- 管理器内更新：版本比较依据 `update.json`（当前版本基线 `versionCode=100`）
- 手动升级：直接在管理器里安装新版 ZIP，`/data/adb/susfs4ksu_unified` 下已有配置会保留

## 停用与卸载

停用（保留配置，重启后不再加载）：

```sh
su -c 'touch /data/adb/modules/susfs4ksu_unified/disable'
su -c 'reboot'
```

重新启用：

```sh
su -c 'rm -f /data/adb/modules/susfs4ksu_unified/disable'
su -c 'reboot'
```

卸载：在管理器中删除模块，或

```sh
su -c 'ksud module uninstall susfs4ksu_unified'
su -c 'reboot'
```

停用或卸载后重启即回到模块加载前的状态；本项目不提供热卸载。

## 故障排查

| 现象 | 含义 | 处理 |
| --- | --- | --- |
| `susfsctl probe` 输出 `susfsctl: none` 且退出码 3 | 没有检测到后端 | 确认模块已启用、重启过、KMI 匹配 |
| 退出码 4 | 版本或布局不支持 / 该操作不被当前后端支持 | 查看 stderr 里的 JSON `error` 字段 |
| 退出码 5 | 权限错误 | 用 root（`su`）执行 |
| 退出码 6 | 协议错误 | 内核返回异常，查看 stderr 诊断 |
| 退出码 7 | 内核业务失败 | 目标路径/规则本身不满足条件 |
| 高级页灰置 | 当前不是 LKM 后端 | native SUSFS 后端不提供高级页 |
| 无法下载更新包 | 网络访问 GitHub 受限 | 用代理/镜像下载 ZIP，并按 `SHA256SUMS` 校验后再安装 |

## 从源码构建

Windows（本机维护流程，需要 WSL2 + Docker + Android NDK + Node.js ≥ 20 + Python 3.12）：

```powershell
powershell -File tools/build.ps1
```

产物在构建目录的 `release/`：`SUSFS4LKM-arm64.zip`、`provenance`、`SHA256SUMS`、`ROLLBACK.sh`。

Linux / CI 使用同一批脚本：`tools/fetch-sources.py` → `tools/source_lock.py` → `tools/build-lkm.sh` → `tools/fetch-ko.py` → `tools/package.py`，完整顺序见 `.github/workflows/release.yml`。

CI 发布的 ZIP 就是这套流程的产物，与本机 Windows 构建的 ZIP 对比：91 个成员名称、顺序与内容一致，只有宿主编译的两个 helper（`bin/susfsctl`、`bin/susfs_insmod`）因宿主 clang 工具链不同而字节不同；8 个内核模块与 WebUI 产物逐字节一致。发布包以 CI 产物为准。

注意：上游源码不随仓库提交，由 `tools/fetch-sources.py` 按 `manifest.yaml` 中固定的 commit 抓取，并逐个文件核对 `SOURCE_LOCK.json` 里的 SHA256；**任何改动 `overlay/`、`src/`、`tools/`、`tests/` 或 `manifest.yaml` 之后都必须重新生成锁文件**：

```sh
python3 tools/source_lock.py . --write
```

## 目录结构

```
overlay/module/    模块脚本与 module.prop（安装进设备的文件）
overlay/webui/     WebUI 覆盖层（与上游 webui 合并后构建）
src/susfsctl/      统一控制入口的 C 源码
tools/             取源、构建、打包、版本与校验脚本
tests/             本地与设备端测试脚本
update.json        远程更新元数据
SOURCE_LOCK.json   构建输入锁定（含上游文件 SHA256）
```

## 开源协议

- 本仓库自有代码（模块脚本、工具、WebUI 覆盖层、用户态 helper）：AGPL-3.0，见 `LICENSE`
- 上游组件许可：LKM 为 GPL-2.0，cJSON 为 MIT，模块与 WebUI 上游为 AGPL-3.0，见 `LICENSES/`

## 免责声明

本模块会修改运行中的内核行为（加载 LKM、隐藏路径与挂载等），请自行评估风险并在可回滚的设备上使用。仅在上表支持范围内、且已备份数据的设备上安装。
