# GamejamJGD-2026

UE Game Jam 团队协作仓库，目标地址：[HCXXWDG/GamejamJGD-2026](https://github.com/HCXXWDG/GamejamJGD-2026)。

UE 工程位于 `E:/Code_from_class/Games/JGD2026`，它也是 Git 仓库根目录。协作文件现在放在工程下并与项目一起版本管理；原 `JGDgamejam` 文件夹保留为本地副本。

- **团队引擎：UE 5.8.3，Changelist 58210709，Epic Games Launcher 发行版。**
- 项目模板：**Games → Blank → C++**；创建后启用内置 **Paper2D 1.0**。
- 当前 `.uproject` 保留 C++ 模块；Paper2D 用于游戏资源，官方 MCP、Python 和编辑器工具集限制为 Editor 用途，版本与依赖见版本锁。
- Git ≥ 2.50.0；Git LFS ≥ 3.6.1。
- 开发流程：任务短分支 → PR → `main`；已有蓝图/地图修改前取得 LFS 文件锁。
- 初始不要求第三方插件；新工程创建后记录实际依赖。

开工前阅读 [团队协作规范](CONTRIBUTING.md)，安装版本参考 [环境与插件记录](docs/ENVIRONMENT.md)。机器可读基线见 [versions-lock.json](docs/versions-lock.json)，完整本机插件描述清单见 [plugins-snapshot.csv](docs/plugins-snapshot.csv)。

`.gitignore`、`.gitattributes` 和 `.github/PULL_REQUEST_TEMPLATE.md` 已在仓库根目录。项目使用 `main` 分支，`origin` 指向目标 GitHub 仓库。

打开 `JGD2026.uproject` 后默认进入 `/Game/JGD2026/Maps/L_2D_Sandbox`。当前关卡保留平面测试地面，已移除的占位 Sprite 保持删除状态；通用骨架由 `BP_2DGameMode`、`BP_2DPlayerController` 和 `BP_2DViewPawn` 组成。首次创建脚本的示例 Sprite 统一摆在同一二维平面。

画面使用 XZ 二维平面（X 为左右、Z 为上下）。主摄像机 `Camera_MainView` 位于测试关卡中，垂直正对游戏平面，并在 PIE 时自动成为玩家视角。想调镜头时，在 World Outliner 选择 `Camera_MainView`：移动或旋转 Actor 来调整取景方向；在 Details 的 Camera Component 中修改 `Ortho Width` 来调整画面范围。初始宽度为 2200，位置为 (0,1200,100)，旋转 Pitch 0 / Yaw -90 / Roll 0。`BP_2DViewPawn` 只负责玩家生成位置。

`Scripts/Editor/create_2d_starter.py` 使用 Epic 官方编辑器 API；在 UE 的“工具 → 执行 Python 脚本”中运行。已有完整预设时只验证资源；遇到同名外部资源或半成品时停止，保留团队修改。首次创建要求先保存当前编辑器工作。设计文档与实施计划仅保存在本地，通过 `.git/info/exclude` 排除。
