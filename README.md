# GamejamJGD-2026

使用 UE 和 Paper2D 开发的二维 Game Jam 团队项目。

工程入口为仓库根目录的 `JGD2026.uproject`。成员可自行选择本地克隆目录，并使用同一份工程开发。

- **团队引擎：UE 5.8.3，Changelist 58210709，Epic Games Launcher 发行版。**
- 项目基于 **Games → Blank → C++** 模板，使用内置 **Paper2D 1.0**。
- 工程包含 C++ 模块，二维资源使用 Paper2D；官方 MCP、Python 和编辑器工具集仅在 Editor 中启用，版本与依赖见版本锁。
- Git ≥ 2.50.0；Git LFS ≥ 3.6.1。
- 开发流程：任务短分支 → PR → `main`；已有蓝图/地图修改前取得 LFS 文件锁。
- 团队插件以工程配置和版本锁为准；新增插件须按协作规范更新共享基线。

开工前阅读 [团队协作规范](CONTRIBUTING.md)，安装版本参考 [环境与插件记录](docs/ENVIRONMENT.md)。机器可读基线见 [versions-lock.json](docs/versions-lock.json)，插件描述清单见 [plugins-snapshot.csv](docs/plugins-snapshot.csv)。

克隆后，在仓库目录执行 `git lfs install` 和 `git lfs pull`，获取完整 UE 资源，再打开 `JGD2026.uproject`。提交时遵循仓库的 `.gitignore`、`.gitattributes` 和 PR 模板。

工程默认进入 `/Game/JGD2026/Maps/L_2D_Sandbox`。测试关卡提供平面测试地面；通用骨架由 `BP_2DGameMode`、`BP_2DPlayerController` 和 `BP_2DViewPawn` 组成。

画面使用 XZ 二维平面（X 为左右、Z 为上下）。主摄像机 `Camera_MainView` 位于测试关卡中，垂直正对游戏平面，并在 PIE 时自动成为玩家视角。想调镜头时，在 World Outliner 选择 `Camera_MainView`：移动或旋转 Actor 来调整取景方向；在 Details 的 Camera Component 中修改 `Ortho Width` 来调整画面范围。初始宽度为 2200，位置为 (0,1200,100)，旋转 Pitch 0 / Yaw -90 / Roll 0。`BP_2DViewPawn` 只负责玩家生成位置。

仓库仅提交游戏所需的源码、资源、配置和团队协作文件。开发过程中的辅助文档、实施计划、日志、截图和临时脚本保存在本地，可通过 `.git/info/exclude` 排除。
