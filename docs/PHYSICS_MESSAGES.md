# 物理模块消息接口

物理模块复用 `UJGDMessageSubsystem`（每个 GameInstance 一份）。公共定义在
`Source/JGD2026/Physics/JGDPhysicsMessages.h`，频道在 `Config/DefaultGameplayTags.ini`。
负载统一为 `FInstancedStruct`；不能拿任意结构体发送到同名频道。

## 频道与负载

| 精确频道 | C++ 结构体 | 字段 |
| --- | --- | --- |
| `Event.Physics.Request.ApplyWorldProfile` | `FJGDApplyWorldPhysicsProfileRequest` | `TargetWorld`、`WorldProfile` |
| `Event.Physics.Request.SetCharacterBaseProfile` | `FJGDSetCharacterBasePhysicsProfileRequest` | `Character`、`BaseProfile` |
| `Event.Physics.World.ProfileChanged` | `FJGDWorldPhysicsProfileChangedMessage` | `World`、`PreviousProfile`、`NewProfile` |
| `Event.Physics.Character.ProfileChanged` | `FJGDCharacterPhysicsProfileChangedMessage` | `World`、`Character`、`PreviousProfile`、`NewProfile` |

- **精确匹配**：订阅 `Event.Physics` 不会收到上述子频道消息。
- 世界请求必须填写有效 `TargetWorld`；角色请求按 `Character->GetWorld()` 路由。
  其他世界的请求静默忽略；结构体类型不符、目标失效、非空但已失效的 Profile 会警告并拒绝。
- `WorldProfile = None`：恢复该世界首次应用配置前的重力，取消世界默认角色 Profile。
  没有区域或角色 Base 覆盖的角色使用内置 `FJGDCharacterPhysicsSettings` 默认值。
- `BaseProfile = None`：清除角色专属配置，仍按 **最高优先级 Volume → Base → World 默认 → 内置默认** 解析。
  同优先级区域取后进入的区域。有 Participant 时会同步其保存值；无组件时自动注册角色。
- 仅在世界 `OnWorldBeginPlay` 后、`OnWorldEndPlay` 前处理请求；开始前的请求不缓存、不重放。
  此总线是本地同步调用，不提供网络复制、可靠网络传输或历史状态回放。
- 角色从 Subsystem 注销后若仍位于 `AJGDPhysicsProfileVolume` 内，再次注册会从当前 Overlap
  和引擎选中的 PhysicsVolume 重建区域列表，不要求角色先离开再进入。

## 通知时机

通知代表配置已经应用：角色运动参数已写入；世界通知还保证重力设置及本次角色刷新已完成。
首次应用发送一次（即使 `PreviousProfile` 和 `NewProfile` 都为 None）；此后仅在 Profile
对象引用变化时发送。重复 Apply/Refresh 仍更新参数，但同一资产内容修改不会单独触发变化通知。
角色注销再注册视为新的首次应用。

监听方可以在回调中注销/销毁角色；物理模块使用快照遍历及重新查找注册项。
嵌套切换产生的通知按应用顺序排队，在当前通知完成后同步发出，避免旧通知覆盖新显示。
因此负载描述一次已完成的变化，不承诺后续监听执行时状态没有被其他回调再次修改。
勿在回调中无条件切换两个 Profile，避免形成无限反馈。

`OnCharacterPhysicsProfileChanged` 旧委托继续保留；新模块使用总线即可，不要两边重复订阅。
后订阅的 UI/角色在订阅后调用一次 `GetResolvedCharacterProfile` 初始化显示，然后靠通知更新。
`AJGDWindTestCharacter` 已采用这个模式，并过滤 `World` 和 `Character == Self`。

## 蓝图接入

### 发送

1. 在 BeginPlay 后取得 `Get JGD Message Subsystem`。
2. 世界请求：`Make JGD Apply World Physics Profile Request`，`Target World` 连接
   `Get Physics Message World`，`World Profile` 连接世界 DataAsset（或 None）。
   角色请求：`Make JGD Set Character Base Physics Profile Request`，填入 Character 和 Base Profile。
3. 把结构体接到 `Make Instanced Struct` 的 Value，再接 `Broadcast Message` 的 Message。
4. Channel 选择对应的 **Request** 完整 Tag。

总线广播本身没有“成功”返回值；需要观察有效配置变化时订阅通知。同一 Profile 的重复请求不会产生新通知。

### 订阅与退订

1. BeginPlay：取得总线 → `Register Listener`，Channel 选完整变化通知 Tag。
2. Delegate 用 `Create Event` 绑定同签名函数/事件：`Channel`（GameplayTag）、`Message`（InstancedStruct）。
3. **保存返回的 Listener Handle**；每个订阅保存一个句柄。
4. 回调：`Get Instanced Struct Value` 的 Value 接到对应通知结构体的变量/Break 节点，以确定类型。
   只在 **Valid** 分支读取；检查 World，角色通知还要检查 Character 是否为目标角色。
5. EndPlay：对保存的句柄调用 `Unregister Listener`。重复退订安全。

蓝图可见名称会按编辑器规则插入空格；结构体与 Tag 的准确拼写以上表和头文件为准。

## C++ 示例

发送世界请求（普通 Actor 成员函数中）：

```cpp
#include "MessageBus/JGDMessageSubsystem.h"
#include "Physics/JGDPhysicsMessages.h"

if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this))
{
    FJGDApplyWorldPhysicsProfileRequest Request;
    Request.TargetWorld = GetWorld();
    Request.WorldProfile = DesiredWorldProfile; // nullptr 同样是有效操作
    Bus->BroadcastMessage(JGDPhysicsMessageTags::ApplyWorldProfile(), FInstancedStruct::Make(Request));
}
```

发送角色请求：

```cpp
FJGDSetCharacterBasePhysicsProfileRequest Request;
Request.Character = Character;
Request.BaseProfile = DesiredBaseProfile;
if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(Character))
{
    Bus->BroadcastMessage(JGDPhysicsMessageTags::SetCharacterBaseProfile(), FInstancedStruct::Make(Request));
}
```

订阅示例（`AMyActor` 是调用方自己的类）：

```cpp
// .h，放在 generated.h 之前：
#include "MessageBus/JGDMessageBusTypes.h"
class UJGDMessageSubsystem;

// AMyActor 类内：
TWeakObjectPtr<UJGDMessageSubsystem> PhysicsBus;
FJGDMessageListenerHandle PhysicsListener;
UFUNCTION()
void HandlePhysicsChanged(FGameplayTag Channel, const FInstancedStruct& Message);

// .cpp 的 BeginPlay：
if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this))
{
    PhysicsBus = Bus;
    FJGDMessageReceived Delegate;
    Delegate.BindDynamic(this, &AMyActor::HandlePhysicsChanged);
    PhysicsListener = Bus->RegisterListener(JGDPhysicsMessageTags::CharacterProfileChanged(), Delegate);
}

// .cpp：
void AMyActor::HandlePhysicsChanged(FGameplayTag Channel, const FInstancedStruct& Message)
{
    const auto* Changed = Message.GetPtr<FJGDCharacterPhysicsProfileChangedMessage>();
    if (!PhysicsListener.IsValid() || !Changed || Changed->World != GetWorld()) return;
    // 若只关心一个角色，再比较 Changed->Character 与目标角色。
    // 使用 Changed->NewProfile 更新显示；nullptr 表示内置默认值。
}

// .cpp 的 EndPlay（Super::EndPlay 之前）：
if (UJGDMessageSubsystem* Bus = PhysicsBus.Get())
{
    Bus->UnregisterListener(PhysicsListener);
}
PhysicsListener.Invalidate();
PhysicsBus.Reset();
```

## 兼容与验证入口

已有 DataAsset、角色注册组件、Volume 优先级、风场 Tick/AddForce 公式及直接调用 API 不变。
GameplayTag 仅负责模块间请求/通知，不接管每帧风力，也不要求箱子注册为 Character。

`BP_WindVolume_Test` 以世界空间 `WindDirection` 的 XZ 投影作为角色和刚体的唯一风向；箭头只做
编辑器可视化。`AffectedRigidBodies = false` 时不收集新刚体，也不对已经收集的刚体继续施力；
角色风力不受这个刚体专用开关影响。

关闭编辑器完整构建 `JGD2026Editor Win64 Development` 后，运行 `JGD2026.Physics` 自动化测试。
其中 `Messages` 覆盖路由、回退、去重、退订及回调重入；`PIE.WindBlueprints` 会创建空白临时关卡，
为风场生成真实 Brush，并验证进出区域、区域内重注册、Profile 显示、统一风向及运行时关闭刚体风力，
不保存任何地图或资产。
PIE 测试会切换当前关卡，请在单独编辑器进程运行；检测到未保存地图或正在 PIE 时会拒绝执行。

```powershell
& '<UE目录>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<工程目录>/JGD2026.uproject' -Unattended -NullRHI -NoSound -NoP4 '-ExecCmds=Automation RunTests JGD2026.Physics' '-TestExit=Automation Test Queue Empty'
```
