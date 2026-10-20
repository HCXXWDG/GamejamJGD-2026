# 关卡星级与收集物

每关有两个红色心脏收集物，使用 `T_Star_1` 美术生成的 `SP_LevelStar`。

- 到达终点获得基础一星，每拾取一个心脏额外获得一星，最高三星。
- 只有 Playing 阶段可以拾取；核心经过心脏时计入本次收集，不产生物理阻挡。
- 未通关的尝试不提交成绩。重开组装、重新挑战或掉落重置会清空本次收集并恢复心脏。
- 通关后暂停游戏，显示本次星级和历史最高星级。选关按钮显示该关最高纪录。
- 纪录保存在玩家本机的 `JGD2026_LevelRatings_v1` SaveGame 槽，重新启动游戏仍保留；较低成绩不覆盖较高成绩。

## 关卡摆放

三个正式关卡 `Level_0`、`Level_1`、`Level_2` 各有 `LevelStar_1`、`LevelStar_2`。
它们属于原生 `JGDLevelStar` Actor，可以在编辑器直接移动。

同一关中两个 Actor 的 `Star Index` 必须分别为 **0** 和 **1**。同一个编号只计一次。
`Pickup Radius` 控制拾取距离，`Display Size` 控制显示尺寸。拾取只在 XZ 游戏平面判断，并考虑核心半径及帧间移动，避免快速滚过时漏掉。

## 组件库存

- 第一关：仅核心。
- 第二关：四块普通骨头、两个紫色活动关节、四块肌肉。
- 第三关：沿用完整组件库存。

库存由 `AssemblyTutorial::ConfigureInventory` 按正式地图名配置；测试地图及自定义沙盒保留原有配置。

## 程序接口

- `UJGDLevelRatingComponent` 管理本次收集及重置，挂在 Session 所属角色上。
- `UJGDLevelProgressSubsystem` 是 GameInstance 子系统，跨关卡保存最高成绩；蓝图可用 `Get Best Stars` 读取。
- `UJGDStarRatingWidget` 显示三格评分，结算界面与现有选关按钮共用。
- 选关评分读取按钮现有的 `Level` 软地图引用，不更改原蓝图的跳转事件。
