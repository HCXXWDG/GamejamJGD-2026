# GamejamJGD-2026

UE Game Jam 团队协作仓库，目标地址：[HCXXWDG/GamejamJGD-2026](https://github.com/HCXXWDG/GamejamJGD-2026)。

UE 工程位于 `E:/Code_from_class/Games/JGD2026`，它也是 Git 仓库根目录。协作文件现在放在工程下并与项目一起版本管理；原 `JGDgamejam` 文件夹保留为本地副本。

- **团队引擎：UE 5.8.3，Changelist 58210709，Epic Games Launcher 发行版。**
- 项目模板：**Games → Blank → C++**；创建后启用内置 **Paper2D 1.0**。
- 当前 `.uproject` 有 C++ 模块，并显式启用 Paper2D 与 ModelingToolsEditorMode。
- Git ≥ 2.50.0；Git LFS ≥ 3.6.1。
- 开发流程：任务短分支 → PR → `main`；已有蓝图/地图修改前取得 LFS 文件锁。
- 初始不要求第三方插件；新工程创建后记录实际依赖。

开工前阅读 [团队协作规范](CONTRIBUTING.md)，安装版本参考 [环境与插件记录](docs/ENVIRONMENT.md)。机器可读基线见 [versions-lock.json](docs/versions-lock.json)，完整本机插件描述清单见 [plugins-snapshot.csv](docs/plugins-snapshot.csv)。

`.gitignore`、`.gitattributes` 和 `.github/PULL_REQUEST_TEMPLATE.md` 已在仓库根目录。项目使用 `main` 分支，`origin` 指向目标 GitHub 仓库。
