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
| `Event.Physics.Request.RegisterRigidBody` | `FJGDRegisterRigidBodyPhysicsRequest` | `TargetWorld`、`Body`、`BaseProfile`、`RegistrationId` |
| `Event.Physics.Request.UnregisterRigidBody` | `FJGDUnregisterRigidBodyPhysicsRequest` | `TargetWorld`、`Body`、`RegistrationId` |
| `Event.Physics.Request.SetRigidBodyBaseProfile` | `FJGDSetRigidBodyBasePhysicsProfileRequest` | `TargetWorld`、`Body`、`BaseProfile` |
| `Event.Physics.RigidBody.ProfileChanged` | `FJGDRigidBodyPhysicsProfileChangedMessage` | `World`、`Body`、`RegistrationId`、`PreviousProfile`、`NewProfile` |

- **精确匹配**：订阅 `Event.Physics` 不会收到上述子频道消息。
- 世界请求必须填写有效 `TargetWorld`；角色请求按 `Character->GetWorld()` 路由。
  其他世界的请求静默忽略；结构体类型不符、目标失效、非空但已失效的 Profile 会警告并拒绝。
- `WorldProfile = None`：恢复该世界首次应用配置前的重力，取消世界默认角色 Profile。
  没有区域或角色 Base 覆盖的角色使用内置 `FJGDCharacterPhysicsSettings` 默认值。
- `BaseProfile = None`：清除角色专属配置，仍按 **UE 当前选中的 PhysicsVolume → Base → World 默认 → 内置默认** 解析。
  重叠 PhysicsVolume 的选择与优先级由 UE 负责。有 Participant 时会同步其保存值；无组件时自动注册角色。
- 仅在世界 `OnWorldBeginPlay` 后、`OnWorldEndPlay` 前处理请求；开始前的请求不缓存、不重放。
  此总线是本地同步调用，不提供网络复制、可靠网络传输或历史状态回放。
- 角色从 Subsystem 注销后若仍位于 `AJGDPhysicsProfileVolume` 内，再次注册会恢复引擎当前选中的
  PhysicsVolume，不要求角色先离开再进入。普通几何 Overlap 不单独加入区域列表，以保持重建和
  `ActorEnteredVolume` / `ActorLeavingVolume` 的原生通知语义一致。
- 刚体由 `UJGDPhysicsWorldSubsystem` 按 **当前 PhysicsVolume → Base → World 默认刚体 Profile → 内建刚体默认**
  解析。刚体注册必须使用有效 `RegistrationId`；同一 Body 不能同时归属两个注册者。注销只接受匹配的
  `Body + RegistrationId`，并恢复首次注册时捕获的重力、线性阻力和角阻力。
- `UJGDPhysicsRigidBodyProfile` 只设置重力与线性/角阻力，不接管模拟开关、碰撞或关节。Assembly 执行器
  负责这些阶段状态，并在 Playing Commit 时注册刚体、退出时按注册 ID 批量注销。
- 刚体区域通知来自 Actor 的原生 PhysicsVolume 进出事件；注册位于区域内时会读取当前选中 PhysicsVolume，
  注销后在区域内重新注册会恢复该区域 Profile。区域移除会解析剩余 Base/World 回退。

## 组装刚体抓地

Playing 阶段的核心、骨头及关节通过真实物理材质设置摩擦，参数来自
`Content/Physics/Profiles/DA_AssemblyPhysicsDrive_Default` 的 **Contact** 分类：

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `Contact Friction` | 1.2 | 接触滑动时的摩擦 |
| `Contact Static Friction` | 1.5 | 开始滑动前的抓地力 |
| `Contact Restitution` | 0.0 | 碰撞弹性，0 表示不主动弹起 |

执行器复制各刚体原有物理材质，保留密度等参数，再使用 **Max** 合并摩擦、**Min** 合并弹性。
返回组装、重开或退出时会恢复原始材质覆盖，不修改素材资源。参数在下次开始操控时应用。
`DA_CharacterPhysics_Default` 的 `Ground Friction` 属于 CharacterMovement，不能调整当前组装刚体的抓地力。

## 通知时机

通知代表配置已经应用：角色运动参数已写入；世界通知还保证重力设置及本次角色刷新已完成。
首次应用发送一次（即使 `PreviousProfile` 和 `NewProfile` 都为 None）；此后仅在 Profile
对象引用变化时发送。重复 Apply/Refresh 仍更新参数，但同一资产内容修改不会单独触发变化通知。
角色注销再注册视为新的首次应用。

刚体 `ProfileChanged` 在重力和阻力写入后同步广播；新注册的首次解析也通知一次。注销负责恢复原值，
不会在 Body 已销毁后广播。消息是同步调用，没有成功回执；需要提交结果的 Assembly 生命周期使用同步的
`RegisterRigidBody` C++ API，并在失败时回滚。

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

已有角色 DataAsset、角色注册组件、UE PhysicsVolume 优先级、风场 Tick/AddForce 公式及直接调用 API 不变。
GameplayTag 仅负责模块间请求/通知，不接管每帧风力，也不要求箱子注册为 Character。

Assembly 的正式阶段切换经 `UAssemblyComponent` → `UAssemblyPhysicsBridgeComponent` →
`IAssemblyPhysicsLifecycle` 同步确认：先 Prepare 和再次校验，再应用 Playing 物理状态并复核，最后模型才写入 Playing。
任何 Prepare/Apply 失败都会 Abort 并回滚到安全 Assembly。`UJGDAssemblyPhysicsExecutorComponent` 由角色 Owner
持有，并作为 Bridge 的唯一生命周期 Provider；它借用快照给出的通用核心/部件刚体，拥有运行时约束、驱动状态，
并在 Playing 提交时按 Bridge `SourceId` 向 `UJGDPhysicsWorldSubsystem` 注册 Profile，停止/回滚时注销和恢复区域/Profile 状态。

核心旋转通过 `Event.Physics.Core.RotationRequested` / `FAssemblyPhysicsCoreRotationMessage` 接入。
执行器不读取键盘、不依赖 Space；输入端只需发送通用按住/停止消息，执行器保留独立的
`SetCoreRotationDriveInput` / `ApplyCoreRotationDrive` 接口。新的按下请求只有在 Phase 为 Playing、World/Assembly/
SourceId/RoundGeneration/Revision 与当前 Bridge 绑定一致、Session 代理保护有效、核心刚体仍有效且正在模拟，
并且 `WorldClockwiseAxis` 为有限非零向量时才接受；执行器归一化并使用该世界空间轴。停止消息按当前绑定清理，
并且生命周期 Shutdown、回滚、Assembly 阶段消息和重开都会本地清空缓存输入与轴，不能留下持续驱动。

Assembly 执行器的核心力矩、肌肉刚度/阻尼/最大力/收缩比例和关节断裂阈值统一存于
`/Game/Physics/Profiles/DA_AssemblyPhysicsDrive_Default` DataAsset。运行时创建的 `BP_Player` 执行器
通过 `UJGDPhysicsSystemSettings::DefaultAssemblyDriveProfile` 加载该资源；若组件显式设置 `DriveProfile`，
则使用组件覆盖。资源缺失或参数非法时拒绝初始化或进入 Playing，不默默采用另一组 C++ 数值。
在编辑器中修改并保存 DataAsset 后重新启动 PIE 验证；关节断裂阈值在准备 Playing 时创建约束，
因此修改后还需重新进入 Playing。此资源只负责驱动参数，刚体重力和阻尼仍由既有 RigidBody Profile 管理。

`BP_WindVolume_Test` 以世界空间 `WindDirection` 的 XZ 投影作为角色和刚体的唯一风向；箭头只做
编辑器可视化。`AffectedRigidBodies = false` 时不收集新刚体，也不对已经收集的刚体继续施力；
角色风力不受这个刚体专用开关影响。

自动化测试会另建临时关卡，不依赖或修改现有地图资产。

关闭编辑器完整构建 `JGD2026Editor Win64 Development` 后，运行 `JGD2026.Physics` 和
`JGD2026.Assembly.PhysicsExecutor` 自动化测试。
其中 `Messages` 覆盖路由、回退、去重、退订及回调重入；PIE 用例会创建空白临时关卡，
为区域生成真实 Brush，并验证风场进出、区域内重注册、Profile 显示、统一风向、运行时关闭刚体风力，
以及高低优先级区域交叠时重注册并依次退出后恢复 Base Profile；不保存任何地图或资产。
PIE 测试会切换当前关卡，请在单独编辑器进程运行；检测到未保存地图或正在 PIE 时会拒绝执行。

```powershell
& '<UE目录>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<工程目录>/JGD2026.uproject' -Unattended -NullRHI -NoSound -NoP4 '-ExecCmds=Automation RunTests JGD2026.Physics' '-TestExit=Automation Test Queue Empty'
```
