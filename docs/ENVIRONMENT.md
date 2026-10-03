# 环境与插件版本记录

检测日期：2026-10-03。依据是 UE 安装信息、`JGD2026.uproject`、项目日志及 Git 索引。工程已在本机 UE 5.8.2 中打开，日志确认 Paper2D 加载；145 个 UE 资产由仓库属性配置为 Git LFS。

## 1. 检测结果与团队约束

| 项目 | 实测结果 | 团队使用规则 |
| --- | --- | --- |
| Launcher 安装 | UE 5.8.2，`5.8.2-56702186+++UE5+Release-5.8-Windows` | 固定此正式发行构建 |
| `Engine/Build/Build.version` | Major 5 / Minor 8 / Patch 2；CL **56702186**；CompatibleCL **55116800**；Branch **`++UE5+Release-5.8`** | UE 版本与 CL 必须一致 |
| Git | **2.50.0.windows.1** | 最低 2.50.0，更新的稳定版可用 |
| Git LFS | **3.6.1** | 最低 3.6.1，初始化每个克隆的 LFS hook 与锁校验 |
| 项目模板 | 本机引擎安装中无专门的 Paper2D/2D 项目模板 | 统一从 **Games → Blank → C++** 新建；若团队共同决定全用蓝图，可改为 Blank Blueprint |
| Paper2D | **1.0 / Version 1**；引擎内置、默认启用、非 Beta | 新建后在插件设置中确认启用并重启；作为项目的 2D 引擎插件记录 |
| Visual Studio | **Community 2026，18.5.1**，内部安装版本 18.5.11716.220 | 推荐 VS 2026；IDE 补丁号不硬锁 |
| MSVC | `cl.exe` ProductVersion **14.50.35729.0**；FileVersion **19.50.35729.0** | C++ 基线为 UBT 工具链 **14.50.35729** |
| Windows SDK | 安装有 **10.0.22621.0** 和 **10.0.26100.0** | C++ 基线固定 **10.0.22621.0** |
| 历史 UBT 日志 | 选择过 MSVC **14.50.35729**、SDK **10.0.22621.0** | 这是其他编译记录，不能代替新工程编译验收 |
| UE 项目 | `JGD2026.uproject`；C++ 模块 `JGD2026`；主机日志确认 UE **5.8.2 / CL 56702186** | 全员用同一发行版本打开 |
| Paper2D | `JGD2026.log` 显示 Paper2D Runtime 与 Editor 模块均加载 | 项目文件显式启用，确保协作者使用相同依赖 |
| 工作目录 | 工程与 Git 根目录：`Games/JGD2026`；规范副本已置于仓库内 | 原 `Games/JGDgamejam` 文件夹保留作本地备份 |
| Git | Git **2.50.0**、LFS **3.6.1**；`main` 已初始化，`origin` 和 LFS 锁校验已配置 | UE 二进制资源由 LFS 管理，生成文件由 `.gitignore` 排除 |
| GitHub | 目标仓库公开、默认分支名 `main` | 由 `origin` 远端关联 |

注册表还有 UE 5.4 的旧安装条目，但对应 `Build.version` 未找到，不能据此认为 5.4 仍可使用。本次版本采用已核实的 5.8.2。

MSVC 的安装目录名是 **14.50.35717**，实际编译器产品版本是 **14.50.35729**，二者不要混淆。本机 UE 的 `Engine/Config/Windows/Windows_SDK.json` 列出的 MSVC 禁用区间包括 `14.50.0–14.50.35722`；实际二进制 14.50.35729 不在该区间。新成员以 `cl.exe` 产品版本和 UBT 输出核对，不仅看文件夹名。

选 10.0.22621.0 是为了与本机已观察到的 UBT 选择一致；它也在该引擎本地 SDK 配置中列为 MainVersion。Epic 当前文档推荐更新的 SDK，但本团队先使用这个已存在的基线。首次建立 C++ 工程时，负责人确认 UBT 实际选中的编译器与 SDK；如果默认选中不同版本，先统一选择策略及版本记录，再发布工程，避免每人自行切换。兼容范围可参考 [Epic 官方 VS 环境文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine)。

## 2. 检测到的相关插件

下表为本机真实 `.uplugin` 的 **VersionName / Version**。它是版本观察，不是 Game Jam 已启用清单。未在下表列出的引擎描述文件仍在完整快照中。

| 插件标识 | VersionName | Version | 来源/状态 | 本次规则 |
| --- | --- | --- | --- | --- |
| GitSourceControl | **1.4** | **14** | 引擎内置，默认启用，Beta | 可使用；Git/LFS 操作以命令行确认 |
| EnhancedInput | **1.0** | **1** | 引擎内置，默认启用 | 模板/玩法使用它时保留，随引擎锁定 |
| Niagara | **1.0** | **1** | 引擎内置，默认启用 | 需要特效时使用，随引擎锁定 |
| ModelingToolsEditorMode | **0.1** | **1** | 引擎内置，Beta；其他本机工程显式启用 | 需要时启用，限制为 Editor |
| GameplayStateTree | **1.0** | **1** | 引擎内置；其他本机工程显式启用 | 按玩法需要启用，不列为初始必需 |
| StateTree | **0.1** | **1** | 引擎内置，GameplayStateTree 的依赖 | 使用 GameplayStateTree 时一并核对 |
| Landmass | **1.0** | **1** | 引擎内置，Beta；其他本机工程显式启用 | 按需求引入，不列为初始必需 |
| EditorScriptingUtilities | **1.0** | **1** | 引擎内置，Beta | 编辑器工具按需使用 |
| PythonScriptPlugin | **1.0** | **1** | 引擎内置，Beta | 编辑器用途；某些插件会间接依赖它 |
| ModelContextProtocol | **1.0** | **1** | 引擎内置，Experimental | 个人工具不自动加入项目依赖 |
| AllToolsets | **1.0** | **1** | 引擎内置，Experimental，有多个插件依赖 | 个人工具不自动加入项目依赖 |
| AIAssistant | **1.0** | **1** | 引擎内置，Experimental | 个人工具不自动加入项目依赖 |
| InEditorDocumentation | **1.0** | **1** | 引擎内置，Experimental | 按需使用，不列为初始必需 |
| UnrealMCPToolkit | **1.1** | **2** | 其他本机工程的项目级插件；声明 EngineVersion **5.8.0**，模块为 Editor | **未引入本次工程；不要求队友安装** |

UnrealMCPToolkit 的观察来自 `RowenAndSiye_Fuke/Plugins/UnrealMCPToolkit/UnrealMCPToolkit.uplugin`；其描述依赖 ModelContextProtocol 和 PythonScriptPlugin。读取到了版本声明，不代表已验证它在本次工程中的编译、授权或兼容性。

JGD2026 当前还启用了 `ModelingToolsEditorMode`（VersionName 0.1、Beta、Editor-only）；这是项目文件中的实际条目。项目团队若不需要它，可在后续检查依赖后移除。

引擎内置插件的 `VersionName` 经常长期保持 `1.0`；应同时固定 **UE 的实际构建版本**。新引入第三方插件还需要记录 commit / 发行包哈希，不把描述文件的版本声明当作唯一锁定依据。

## 3. 完整快照与机器可读基线

- [versions-lock.json](versions-lock.json)：团队版本基线、实测工具链、重点插件的描述哈希及验收状态。
- [plugins-snapshot.csv](plugins-snapshot.csv)：**895 个引擎插件描述文件 + 1 个其他工程插件，共 896 行记录**；包含版本、描述文件路径、默认启用/Beta/Experimental 标志与 SHA-256。

快照的范围是安装引擎的插件描述文件，加上一个已发现的其他工程项目插件；没有把所有本机工程复制进来。`EnabledByDefault` 缺省、为 false 或为 true，都不等于“已在 Game Jam 运行时挂载”。实际启用情况还受 `.uproject`、插件依赖、目标平台及 Editor/Runtime 限制影响。

快照中的 SHA-256 只覆盖 `.uplugin` 描述文件；不会证明插件源码/二进制整体相同。第三方插件实际采用时，另记录完整发行包 SHA-256 或 Git commit。机器可读记录保存当前工程模板和显式启用插件；其他依赖须随项目变更继续更新。

## 4. 首个工程提交时完成版本记录

后续更新工程版本记录时完成：

1. 保持 `ProjectCreated` 为 true，记录实际 `.uproject` 名称，并更新 `ProjectPluginManifest` 状态。
2. 记录 `.uproject` 显式插件启用/禁用项、目标限制，以及依赖启用情况；本项目至少启用 Paper2D，用编辑器日志或构建结果核对。
3. 新插件记录来源、版本、完整发行包哈希/commit、许可、安装位置和依赖。
4. 从新工程完整编译日志确认 MSVC 与 SDK；纯蓝图工程注明 C++ 编译检查不适用，仍验证打包。
5. 两台电脑确认工程可打开；首轮目标平台打包并运行后，更新 JSON 的对应 `Verification` 字段。

检测和规则文件已准备；Git 初始化、远端上传、仓库权限/保护设置、UE 工程创建和这些工程验收均未由本次编写过程执行。
