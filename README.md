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

打开 `JGD2026.uproject` 后默认进入 `/Game/JGD2026/Maps/L_2D_Sandbox`。点击运行即可查看灰盒地面与三个彩色占位 Sprite；通用骨架由 `BP_2DGameMode`、`BP_2DPlayerController` 和 `BP_2DViewPawn` 组成。

相机从 +X、+Y、+Z 方向看向场景，使用正交投影。调整视角时打开 `/Game/JGD2026/Core/View/BP_2DViewPawn`，选择 `ViewCamera`；初始宽度 2200，相对位置 (1200,1200,1200)，旋转 Pitch -35.2644 / Yaw -135 / Roll 0。视图 Pawn 是固定镜头骨架，角色和输入可在后续玩法模块中加入。

`Scripts/Editor/create_2d_starter.py` 使用 Epic 官方编辑器 API；在 UE 的“工具 → 执行 Python 脚本”中运行。已有完整预设时只验证资源；遇到同名外部资源或半成品时停止，保留团队修改。首次创建要求先保存当前编辑器工作。设计文档与实施计划仅保存在本地，通过 `.git/info/exclude` 排除。
