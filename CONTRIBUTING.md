# GamejamJGD-2026 协作规范

适用仓库：[HCXXWDG/GamejamJGD-2026](https://github.com/HCXXWDG/GamejamJGD-2026)。制定日期：2026-10-03。

本规范适用于几位程序共同开发的短期 UE Game Jam。JGD2026 工程已创建并纳入本仓库；团队版本基线依据发起人本机当前 UE 安装及项目配置记录。默认开发与交付平台为 Windows / Win64，赛题要求其他平台时，团队需统一修改平台和工具链基线。完整检测记录见 [环境与插件版本](docs/ENVIRONMENT.md)。

## 1. 开工前必须达成的十条约定

1. 全员使用 **Epic Games Launcher 版 UE 5.8.3，Changelist 58210709**；比赛期间冻结引擎与项目插件版本。
2. 使用 Git + Git LFS；所有 `.uasset`、`.umap` 都走 LFS，修改已有文件前先取得文件锁。
3. `main` 始终尽力保持可打开、可运行、可打包；开发放在任务短分支，通过 PR 合并。
4. 一个蓝图、一个地图文件，同一时间只有一人编辑；程序代码按功能拆分，公共接口改动先与使用方确认。
5. 先同步、再编辑；执行切分支、拉取、合并、回滚前，保存工作并关闭 UE 编辑器。
6. 地图、角色主蓝图、GameMode、输入配置、共享结构体/枚举都有负责人；认领任务时写清涉及的文件路径。
7. 每次提交一个能解释的改动，包含依赖资源；禁止提交缓存、构建产物和未经检查的批量资源重保存。
8. 改插件、`.uproject`、`Default*.ini`、公共 C++ 头文件、共享蓝图接口前通知相关负责人。
9. 每个 PR 写清测试方法；代码变更做完整编译，玩法变更做 PIE 测试，打包由集成人员定期验证。
10. 最终截止前 2 小时冻结新功能，保留最后一个实际打包并运行成功的提交与安装包。

## 2. 版本规定

| 项目 | 团队规定 | 说明 |
| --- | --- | --- |
| UE | **5.8.3 / CL 58210709 / `++UE5+Release-5.8`** | 必须一致，包括补丁版本；不用 Preview、源码自改版或其他 UE 版本保存团队资源 |
| `.uproject` | `EngineAssociation` 使用 `5.8` | 该字段表示主次版本；团队补丁版本与 CL 以 `versions-lock.json` 和引擎 `Build.version` 为准 |
| Git | **2.50.0 或更新的稳定版** | 发起人实测 2.50.0.windows.1；不要求 Git 补丁号一致 |
| Git LFS | **3.6.1 或更新的稳定版** | 发起人实测 3.6.1；每台机器、每个克隆都要初始化 LFS |
| UE Git 插件 | 引擎内置 **GitSourceControl 1.4，Version 14** | 可用作编辑器状态提示；团队 Git/LFS 流程以终端命令为准 |
| Windows C++ 工具链 | **MSVC 14.50.35729 + Windows SDK 10.0.22621.0** | 启用 C++ 时统一；首次编译检查 UBT 的实际选择 |
| IDE | 推荐 **Visual Studio 2026**；本机为 **18.5.1** | IDE 补丁号不硬锁；Rider/VS Code 也可，但编译器和 SDK 仍按上行统一 |
| 第三方项目插件 | **初始基线不要求任何第三方插件** | 项目创建后以 `.uproject`、插件依赖和 `docs/versions-lock.json` 共同记录 |

UE 5.8 对 VS 2026/VS 2022 的支持及工具链要求可查 [Epic 官方说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine)。这里的编译器与 SDK 是基于本机实测选定的团队基线，并非宣称所有兼容版本都必须相同。

比赛期间不接受“先在自己电脑升级看看，再保存资源到共享分支”。确需升级时，先在独立分支由集成人员完成工程打开、完整编译、PIE 和目标平台打包验证，再统一更新版本记录；旧基线至少保留一个可用提交。

## 3. 仓库内容与现有工程

当前工程 `JGD2026` 已放在仓库根目录；所有成员克隆并打开同一个 `.uproject`，不要各自新建工程后复制资源。

```text
JGD2026.uproject
Config/                     # 共享项目配置
Content/JGD2026/             # 本项目资产
Source/                     # 采用 C++ 时创建
Plugins/                    # 获准引入的项目级插件
Build/                      # 图标、平台资源等需要版本管理的构建输入
docs/                       # 协作规则、版本记录、接口约定
.github/                    # PR 模板
.gitignore
.gitattributes
```

**提交：** `.uproject`、`Config/Default*.ini`、项目资源及其依赖、C++ 源码与 `*.Build.cs` / `*.Target.cs`、必要的 `Build/` 输入、项目插件描述/源码/资源、文档。

**忽略：** `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`、`.vs/`、IDE 临时文件、生成的 `.sln`、本地产物和打包目录。已附上 [.gitignore](.gitignore)。`Content/**/__ExternalActors__`、`__ExternalObjects__` 是可能需要提交的地图资产，不能当作缓存删除或忽略。

**无源码的预编译插件是例外：** 必须保留才能使用的 `Plugins/<插件>/Binaries/`，需要在 `.gitignore` 为该插件精确放行，并让二进制走 LFS；同时验证公开分发权限。不要放行整个工程的 `Binaries/`。

当前 `JGD2026` 工程按 **Games → Blank → C++** 模板创建，目标为 Win64 Desktop，并关闭 Starter Content。Blank C++ 为程序协作提供可共同编译的骨架，2D 游戏功能由 Paper2D 资源与蓝图搭建。项目已启用内置 **Paper2D 1.0** 和 Editor-only 的 **ModelingToolsEditorMode 0.1**；以 `.uproject` 和版本锁文件为准，不要在成员电脑上另建工程。Paper2D 提供 Sprite、Flipbook 和 Tile Map 等 2D 组件，详见 [Epic Paper 2D 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/paper-2d-overview-in-unreal-engine)。如团队决定改为纯蓝图并不提交 C++，先由集成人员单独验证后再统一修改工程。资源第一次入库前先确认 `.gitattributes` 生效，初始化负责人负责默认地图和首次打包。

当前工程已由目标 GitHub 仓库管理。新成员克隆现有仓库并为该克隆配置 Git LFS；不要再次初始化仓库或复制另一份 UE 工程：

```powershell
git clone https://github.com/HCXXWDG/GamejamJGD-2026.git
cd GamejamJGD-2026
git lfs install --local
git config --local lfs.https://github.com/HCXXWDG/GamejamJGD-2026.git/info/lfs.locksverify true
git status --short
```

克隆后先同步 main 并创建个人任务分支：

```powershell
git switch main
git pull --ff-only origin main
git switch -c feat/xy-example-task
git status --short
```

分支名使用本人缩写和实际任务名；完成后按第 4 节提交、推送分支并通过 PR 合并。运行 `git lfs ls-files` 可检查仓库跟踪的 LFS 文件。

## 4. 分支、提交和 PR

采用 **`main` + 任务短分支**。不额外设置长期 `develop`；短分支通常在半天内合并，每完成一个可运行的小功能就集成。

| 分支 | 用途 | 示例 |
| --- | --- | --- |
| `main` | 团队集成版本 | 全员从这里取得最新玩法 |
| `feat/<姓名缩写>-<任务>` | 功能 | `feat/xy-player-dash` |
| `fix/<姓名缩写>-<问题>` | 修复 | `fix/xy-input-blocked` |
| `chore/<姓名缩写>-<任务>` | 配置、插件、工具 | `chore/xy-package-settings` |
| `docs/<姓名缩写>-<主题>` | 文档 | `docs/xy-combat-interface` |

不要建立长期个人分支；一个任务分支只处理一个任务。全员作为此仓库的协作者使用同一远端，便于统一锁资产。

提交标题使用 `类型: 简短说明`，例如 `feat: add player dash`、`fix: prevent repeated death event`、`asset: add enemy hit animation`。说明可以用中文；不能只有“更新”“改了”“final”。

PR 默认需要另一位程序简短检查，采用 **Squash and merge**。PR 合并前，作者将最新 `origin/main` 合入任务分支，并在合入后的内容上重新检查受影响功能。涉及二进制资产的 PR 尽快合并，避免长期占锁。

禁止向共享分支强推、重写已共享历史；需要撤销已合并变更时，用新的 revert PR。禁止以 `reset --hard` / `clean -fd` 作为日常同步步骤。

管理员在首个提交之后设置 `main` 保护：要求 PR、1 个审批、解决讨论；关闭 force push 和删除分支，应用到管理员。当前未建立 UE CI，先做人工编译/打包验证，不设置不存在的必需 CI 检查。GitHub 的相关功能见 [受保护分支说明](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)。

临近截止的紧急修复也走小 PR；可由一位同伴快速审核，集成人员负责打包确认。不要让日常配置依赖管理员绕过分支保护。

## 5. 每位成员的日常操作

### 5.1 首次加入

下列克隆命令从工程目录的父目录运行；已有克隆不要再次执行：

```powershell
git clone https://github.com/HCXXWDG/GamejamJGD-2026.git
cd GamejamJGD-2026
git lfs install --local
git config --local lfs.https://github.com/HCXXWDG/GamejamJGD-2026.git/info/lfs.locksverify true
git lfs pull
git --version
git lfs version
git lfs locks
```

先确认 UE 补丁版本与 CL，再打开 `.uproject`。C++ 工程生成项目文件后完整编译 `Development Editor / Win64`；不要上传自己生成的解决方案或编译产物。检查 `git lfs ls-files`；若资源在文本编辑器中只显示 `version https://git-lfs.github.com/spec/v1`，说明拿到的是指针，需要先解决 LFS 下载，不能用 UE 重保存来“修复”。

### 5.2 开始一个任务

先在团队任务板或沟通群认领任务，写清要改的资产路径与预计交接时间。保存并关闭 UE；工作区必须干净，未完成工作先提交在当前任务分支或另行备份，不直接覆盖。

```powershell
git status --short
git switch main
git pull --ff-only
git lfs pull
git switch -c feat/xy-player-dash
git lfs locks
# 示例文件需在工程中实际存在；替换为自己认领的真实路径
git lfs lock "Content/JGD2026/Player/BP_Player.uasset"
```

取得锁后再开 UE 编辑该文件；锁定失败意味着先等待或协商交接。无需改二进制资产的纯代码任务不需要资产锁。

### 5.3 保存、提交和合并

1. 在 UE 中保存本次改动，编译相关蓝图并做 PIE 检查；退出编辑器。
2. 用 `git status --short` 检查所有变化，显式 `git add` 本任务的文件及其依赖，不盲目添加无关资源。
3. 检查 `git diff --cached --stat`、文本配置差异、`git lfs ls-files`，然后提交并 `git push -u origin HEAD`。
4. 开 PR，填写已附上的模板。**推送任务分支后仍保留资产锁。**
5. 审核期间需要追上主分支时，在干净工作区、编辑器关闭的情况下执行：

```powershell
git fetch origin
git merge origin/main
git lfs pull
```

6. 冲突解决后重新检查功能，推送任务分支并完成 PR 合并。PR 已合并后，切回并更新 `main`，确认自己的资产改动已进入 `main`，再解锁交接：

```powershell
git switch main
git pull --ff-only
git lfs pull
git lfs unlock "Content/JGD2026/Player/BP_Player.uasset"
```

示例命令不能跳过工作区检查或冲突处理连续盲跑。任务分支已被 squash 合并后，下一项任务从更新的 `main` 新建分支；旧分支不再继续开发。

## 6. 蓝图、地图与 LFS 文件锁

`.uasset` 与 `.umap` 按不可自动合并的二进制文件管理，包括蓝图、材质、数据资产、结构体、枚举与地图。文本代码的合并办法不能直接用在这些文件上。LFS 负责资源存储，`lockable` 与文件锁配合约定编辑权；已附上 [.gitattributes](.gitattributes)。

**文件锁针对仓库路径，并不因为换分支就允许另一人同时编辑。** `git lfs locks` 是交接依据。LFS 推送锁检查依赖客户端配置及 pre-push hook，不是 GitHub 所有操作都会强制执行的权限屏障；不能关闭 hook、关闭锁校验或用网页上传来绕过规则。锁行为参见 [Git LFS 官方命令说明](https://github.com/git-lfs/git-lfs/blob/main/docs/man/git-lfs-lock.adoc) 和 [锁校验配置](https://github.com/git-lfs/git-lfs/blob/main/docs/man/git-lfs-config.adoc)。

具体约定：

- 修改已有资产前先同步 `main` 并取得锁；只有自己的资产锁可以正常解锁。
- 新建资产先认领唯一名称/路径；新文件初次加入时可能没有远端记录，推送并开始持续编辑后也按同一路径的锁规则管理。
- 同一资产的锁持续到 PR 合并且 `main` 更新确认完成；只推送个人分支就解锁，会让下一位从旧资产继续修改。
- 卡住或离开工位时及时交接。管理员仅在确认原编辑者的工作已提交或另行保存后才能强制解锁；不能静默抢锁。
- 不通过复制同一个共享蓝图到不同分支来规避锁；原型可放自己认领的独立测试地图/资产中，之后由负责人集成。
- 已产生二进制冲突时先停止编辑，在仓库外保存双方版本；由负责人确认一个基础版本，再在 UE 中人工重做另一方改动。禁止不检查内容就全选 ours/theirs。

**地图协作：** 默认一张 `.umap` 一位负责人。其他程序在各自测试地图调试，再由地图负责人放入主场景。确需多人并行布景时，先设计子关卡，或评估 World Partition / One File Per Actor；外部 Actor 与 Object 文件也属于受控资产，必须一起提交并安排编辑权，不能只提交主地图。

**资源移动/重命名：** 在 UE Content Browser 内操作，由负责人安排；先取得原文件及受影响引用资产的锁。重命名会留下 Redirector，Fix Up 会重保存引用包，应由负责人在无人编辑相关资产时统一处理，提交移动、删除和引用更新。参见 [Epic Redirector 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-redirectors-in-unreal-engine)。

本次默认不启用 Multi-User Editing。若团队之后使用它，仍要保存并提交最终资产；它也要求成员从一致的工程状态开始，参见 [Epic 多用户编辑说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/multi-user-editing-overview-for-unreal-engine)。

## 7. 程序分工与公共接口

认领任务时必须登记“负责人、功能范围、涉及文件、公共接口、测试入口”。在同一功能多人开发时，将行为拆为独立 Actor Component、蓝图子类、接口或独立 C++ 类，再由负责人集成；避免几个人往同一个 `BP_Player` 或 GameMode 里塞逻辑。

| 共享区域 | 负责人约定 | 其他成员的协作方式 |
| --- | --- | --- |
| `BP_Player` / PlayerController / 输入资产 | 玩家模块负责人 | 使用独立组件或接口接入，申请锁后才改共享资产 |
| 主地图及默认地图设置 | 关卡/集成负责人 | 在独立测试地图调试后交付组件和摆放说明 |
| GameMode / GameInstance / 开始结束流程 | 核心流程负责人 | 先约定事件和接口，避免各自写一套胜负判断 |
| UI 与公共 Widget | UI 负责人 | 通过事件/接口传数据，说明输入模式和鼠标控制的修改 |
| 共享结构体、枚举、数据表 | 使用方共同确认，指定一人修改 | 一起核对蓝图引用、默认值、存档和序列化影响 |
| `.uproject`、插件、`Default*.ini`、打包设置 | 集成负责人 | 单独说明变更理由与受影响模块 |

开工时把上述岗位落实到姓名；人数少可以一人兼任，但每个共享文件必须有明确的交接对象。

公共接口变更说明至少包含：函数/事件名称、输入输出、单位、调用方、触发时机、失败或空对象处理。例如伤害接口约定血量单位、是否重复触发死亡、由谁更新 UI。接口变更与调用方修复放在同一个可运行 PR，或先保留旧接口兼容。

资源目录按模块分：`Content/JGD2026/{Core,Player,Enemy,UI,Maps,Audio,FX,Data}`。命名使用英文或拼音、固定大小写，后缀小写；建议前缀 `BP_`、`BPC_`、`BPI_`、`WBP_`、`DA_`、`DT_`、`E_`、`S_`、`M_`、`MI_`、`T_`、`SM_`、`SK_`、`IA_`、`IMC_`、`L_`。原型测试资源放 `Content/JGD2026/Dev/<姓名缩写>/`；用于最终游戏时由负责人整理其路径、引用和打包策略。

C++ 按 UE 命名与反射规范开发；提交 `.h/.cpp`、模块构建与目标文件，不提交 Live Coding 产物。修改 `UCLASS` / `UPROPERTY` / `USTRUCT` 等反射声明后，关闭编辑器做完整编译并重开验证；本机 Live Coding 成功不能替代这个检查。

## 8. 插件与项目配置变更

内置插件随 **UE 5.8.3 / CL 58210709** 统一，不因许多描述文件都写 `1.0` 就认为跨引擎版本兼容。当前 `.uproject` 显式启用 Paper2D 与 Editor-only 的 ModelingToolsEditorMode；新增、移除或改插件必须同步更新版本锁，并检查默认启用项和传递依赖。

引入新插件时单独开 PR，写清：插件名称、来源、`VersionName` / `Version`、Git commit 或发行包 SHA-256、适配引擎、运行时/编辑器用途、依赖、安装路径、许可与全员取得方式。能合法入库的第三方插件优先放 `Plugins/`，避免只装在某人的 Engine 目录。

插件获准进入 `main` 前，至少由另一台成员电脑打开工程、编译，并由集成人员验证目标平台打包。仅有描述文件 `EngineVersion` 匹配不是兼容性证明。只有编辑器用途的插件限制为 Editor，不让游戏运行时依赖个人 AI/MCP 服务。

发起人其他工程的 **UnrealMCPToolkit 1.1 / Version 2 / EngineVersion 5.8.0** 已被检测到，但本项目初始不要求它。ModelContextProtocol、AllToolsets、AIAssistant 等工具也不因发起人曾启用就自动加入本项目。

Input、Collision、GameplayTags、Maps & Modes、Packaging 等 `Default*.ini` 属于团队共享设置；提交前检查差异，并注明影响。API Key、个人绝对路径和本地服务凭据不进入共享配置。

## 9. 合并与交付检查

每个 PR 至少完成与改动相应的检查；可以不适用，但要说明原因，不能勾选未做的验证：

- [ ] 使用规定 UE 版本，工程可打开，必要插件没有缺失。
- [ ] 相关蓝图 Compile 通过，新增资源及其依赖全部提交。
- [ ] C++ 变更关闭编辑器后完整编译通过，并确认 UBT 编译器/SDK 选择。
- [ ] 用写明的地图/入口做 PIE：基本流程、输入、失败/重试及受影响功能正常。
- [ ] 合入最新 `origin/main` 后复测；检查无冲突、无无关资产重保存。
- [ ] LFS 资源实际可下载，文件锁仍由作者持有；合并后再释放。
- [ ] 插件、公共配置或打包相关变更已进行目标平台打包与运行检查。

集成人员每 **2～3 小时** 从更新的 `main` 打包一次；第一次可玩的核心循环完成时就验证打包，不等最后。纯文档变化无需运行游戏测试。每次打包记录提交 SHA、配置、打包命令/设置、结果与已知问题；至少做一次独立目录的新克隆，确保不是靠本地未提交文件运行。

最终截止前 2 小时冻结新功能，仅合入修复和必要资源。最终包从确定的 `main` 提交生成，检查启动、默认地图、输入、核心循环、胜负/重试、声音与退出；在另一台机器或干净环境运行。记录提交 SHA，给验证成功的提交打 `jam-final` 标签并保存安装包；后续修复另打标签，不能移动已有最终标签。安装包在比赛指定入口或 Releases/团队交付位置分发，不提交到工程 Git 历史。

## 10. 仓库容量与公开内容

只导入游戏实际使用的资产；源码素材如 `.blend/.psd/.fbx` 可以按既定 LFS 规则保存，但不要反复提交巨大素材全集。GitHub 普通 Git 会阻止大于 100 MiB 的文件，详见 [大文件限制](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)。

LFS 不是无限空间：每个新版本保存整个文件，下载也消耗仓库所有者的带宽额度。当前 GitHub Free/Pro 文档列出 10 GiB 存储和 10 GiB 下载带宽额度；实际配额与预算以仓库所有者账户为准，见 [Git LFS 计费说明](https://docs.github.com/en/billing/concepts/product-billing/git-lfs)。集成人员查看用量，遇到额度问题及时处理，不在比赛中擅自把整个共享历史迁移或改写。

此仓库目前公开。将要上传的第三方素材、Fab/商城资产与插件源码必须确认允许公开分发；允许在游戏中使用不等于允许把原始资源公开上 GitHub。许可仅允许团队私有使用时，先采用符合许可的私有协作方式，再上传。将可用素材的来源和授权记录放入 `docs/`。
