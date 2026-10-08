# 事件总线（Event Bus）

用 GameplayTag 当频道、用 `FInstancedStruct` 当负载的模块间通信设施。

**核心约定**：发送方只认频道 Tag，不认识接收方；接收方也只认频道 Tag，不认识发送方。
所以地块模块不需要知道 UI 存在，UI 也不需要引用地块，两边只共享一个 Tag 字符串。

| 项 | 位置 |
|---|---|
| 总线实现 | `Source/JGD2026/MessageBus/JGDMessageSubsystem.h` / `.cpp` |
| 委托与句柄类型 | `Source/JGD2026/MessageBus/JGDMessageBusTypes.h` |
| 频道 Tag 列表 | `Config/DefaultGameplayTags.ini` |

总线挂在 `UGameInstanceSubsystem` 上，随 GameInstance 创建、跨关卡存活。

---

## 一、频道（GameplayTag）

在编辑器里打开 **项目设置 → Gameplay Tags → Gameplay Tag List → Add**，新增的 Tag 会自动写回 `Config/DefaultGameplayTags.ini`，可以正常进版本管理。

**命名约定**：`Event.<模块>.<动作>`，例如：

```
Event.Grid.BlockMoved
Event.Grid.BlockPlaced
Event.Player.Spawned
Event.Game.PhaseChanged
```

> **注意**：通过项目设置面板添加的 Tag 即时生效；如果是**手改 ini 文件**，需要重启编辑器才会加载。

---

## 二、负载（FInstancedStruct）

负载是一个 `FInstancedStruct`，可以装**任意 USTRUCT**（C++ 结构体或蓝图结构体都行）。

- **负载不是必须的**。只表示「这件事发生了」时，`Broadcast Message` 的 `Message` 引脚留空不连即可，接收端拿到的 `Message` 是空的（`Is Valid` 为 false）。
- 一个频道不强制绑定某一种结构体类型，但**建议一个频道固定一种负载结构体**，否则接收端解包会拿到空值。
- 负载**只在本机内存中传递，不支持网络复制**。

---

## 三、蓝图用法

### 3.1 获取总线

节点 `Get JGD Message Subsystem`，`World Context Object` 传 `self` 即可。

### 3.2 发送

```
Get JGD Message Subsystem
  └─ Broadcast Message
       ├─ Channel : Event.Grid.BlockMoved
       └─ Message : Make Instanced Struct  ← 输入引脚接你的结构体
```

`Make Instanced Struct` 的输入引脚可以接任意结构体，包括蓝图结构体变量或 `Make <你的结构体>` 节点的输出。

**`Message` 引脚可以不连。** 只表示「这件事发生了」、不需要携带数据时，把 `Message` 空着就行：

```
Get JGD Message Subsystem
  └─ Broadcast Message
       ├─ Channel : Event.Grid.BlockMoved
       └─ Message : （不连）
```

留空时就是一条不带负载的纯通知，接收端收到的回调完全一样，只是 `Message` 是空的。
接收端可以用 `Is Valid` 节点（`Is Instanced Struct Valid`）判断这条消息有没有负载。

### 3.3 监听

```
Get JGD Message Subsystem
  └─ Register Listener
       ├─ Channel  : Event.Grid.BlockMoved
       └─ Delegate : 在这个引脚上右键 → Create Event
```

右键 `Delegate` 引脚选 **Create Event**，蓝图会自动生成一个签名匹配的自定义事件并完成绑定。
（和引擎自带的 `Set Timer by Event` 是同一套绑定机制。）

生成的自动事件长这样，在它里面解包：

```
[自定义事件] Channel / Message
  └─ Get Instanced Struct Value
       ├─ Instanced Struct : Message
       └─ Value            : 你的结构体类型
       └─ Exec Result      : Valid 分支后 Value 才有效
```

**把 `Register Listener` 的返回值存进一个变量**，退订时要用。

### 3.4 退订

```
Unregister Listener         ← 把存下来的句柄传回去
Unregister All Listeners    ← 传 self，一次性退掉这个对象的所有监听
```

推荐在 `BeginPlay` 里注册、`EndPlay` 里退订。**监听对象被销毁后总线会自动忽略它**，所以漏掉退订不会导致崩溃或 GC 泄漏，但显式退订更干净。

### 3.5 完整例子：地块移动 → UI 提示

```
[BP_MoveBlock 移动结束]
  Get JGD Message Subsystem → Broadcast Message
    Channel : Event.Grid.BlockMoved
    Message : Make Instanced Struct (GridLocation = 当前格子坐标)

[WBP_HUD]
  Event Construct:
    Get JGD Message Subsystem → Register Listener
      Channel  : Event.Grid.BlockMoved
      Delegate : Create Event → [OnBlockMoved]
    返回值 → 存进变量 BlockMovedHandle

  [OnBlockMoved] (Channel, Message):
    Message → Get Instanced Struct Value → Valid →
      → 更新 UI 文本

  Event Destruct:
    Get JGD Message Subsystem → Unregister Listener (BlockMovedHandle)
```

`BP_MoveBlock` 完全不需要引用 `WBP_HUD`，反过来也一样。

---

## 四、C++ 用法

### 4.1 发送

```cpp
#include "MessageBus/JGDMessageSubsystem.h"

const FGameplayTag Channel = FGameplayTag::RequestGameplayTag(TEXT("Event.Grid.BlockMoved"));

if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this))
{
    if (Bus->HasListeners(Channel))   // 没人在听就别费劲构造负载了
    {
        FBlockMovedMessage Msg;
        Msg.GridLocation = NewGridLocation;

        Bus->BroadcastMessage(Channel, FInstancedStruct::Make(Msg));
    }
}
```

`FInstancedStruct::Make(...)` 也可以直接传构造参数：`FInstancedStruct::Make<FBlockMovedMessage>(X, Y)`。

不需要负载时**直接把第二个参数省掉**，`Message` 有默认值：

```cpp
if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this))
{
    Bus->BroadcastMessage(FGameplayTag::RequestGameplayTag(TEXT("Event.Game.PhaseChanged")));
}
```

等价于显式传一个空的 `FInstancedStruct()`，接收端拿到的 `Message` 是空的。

### 4.2 监听

因为用的是**动态委托**（为了蓝图能绑），C++ 侧的回调**必须是 `UFUNCTION()`，不能用 lambda**。

```cpp
// ---- 头文件 ----
#include "MessageBus/JGDMessageBusTypes.h"

UCLASS()
class UMyComponent : public UActorComponent
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UFUNCTION()
    void HandleBlockMoved(FGameplayTag Channel, const FInstancedStruct& Message);

private:
    FJGDMessageListenerHandle BlockMovedHandle;
};
```

```cpp
// ---- 源文件 ----
#include "MessageBus/JGDMessageSubsystem.h"

void UMyComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this))
    {
        FJGDMessageReceived Delegate;
        Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UMyComponent, HandleBlockMoved));

        BlockMovedHandle = Bus->RegisterListener(
            FGameplayTag::RequestGameplayTag(TEXT("Event.Grid.BlockMoved")), Delegate);
    }
}

void UMyComponent::HandleBlockMoved(FGameplayTag Channel, const FInstancedStruct& Message)
{
    // 类型不匹配时 GetPtr 返回 nullptr，不会崩
    if (const FBlockMovedMessage* Msg = Message.GetPtr<FBlockMovedMessage>())
    {
        // 使用 Msg->GridLocation
    }
}

void UMyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this))
    {
        Bus->UnregisterListener(BlockMovedHandle);
    }

    Super::EndPlay(EndPlayReason);
}
```

### 4.3 API 速查

```cpp
// 取总线（WorldContextObject 传 this 或任意有 World 的对象）
static UJGDMessageSubsystem* UJGDMessageSubsystem::Get(const UObject* WorldContextObject);

// 发送。Message 可省略，省略即不带负载的纯通知。
void BroadcastMessage(FGameplayTag Channel, const FInstancedStruct& Message = FInstancedStruct());

// 订阅，返回句柄
FJGDMessageListenerHandle RegisterListener(FGameplayTag Channel, const FJGDMessageReceived& Delegate);

// 按句柄退订（重复调用安全）
void UnregisterListener(FJGDMessageListenerHandle& Handle);

// 退掉某个对象的全部订阅
void UnregisterAllListeners(const UObject* Listener);

// 该频道是否有人订阅
bool HasListeners(FGameplayTag Channel) const;
```

### 4.4 频道 Tag 的引用方式

因为 Tag 走的是 ini 配置而不是 C++ 原生声明，C++ 里要按名字请求：

```cpp
FGameplayTag::RequestGameplayTag(TEXT("Event.Grid.BlockMoved"))
```

这个调用每次都会查一次标签表。**高频路径**上建议缓存：

```cpp
// 文件作用域的懒加载缓存，避免每次广播都查表
static const FGameplayTag Channel_BlockMoved =
    FGameplayTag::RequestGameplayTag(TEXT("Event.Grid.BlockMoved"));
```

如果某个 Tag 拼错了，`RequestGameplayTag` 会报错并在日志里给出提示，不会静默失败。

---

## 五、行为细节与常见坑

**广播是同步的。** `BroadcastMessage` 返回时所有监听回调都已经执行完了。别在回调里做重活（加载资源、大规模生成 Actor），否则会卡住调用方那一帧。

**回调里可以安全地订阅/退订。** 总线在分发前会先拷贝一份监听列表，所以在回调中注册或退订（甚至退订自己）都不会破坏遍历。

**监听对象销毁后自动失效。** 动态委托内部是弱引用，监听对象被 GC 后 `IsBound()` 自动变 `false`，总线会在下次广播时清掉它。所以漏掉退订不会导致对象无法回收。

**C++ 回调必须是 `UFUNCTION()`。** 用 lambda 绑不上——这是为了让蓝图能绑同一个委托而做的取舍。

**别删掉 `BroadcastMessage` 上的 `AutoCreateRefTerm` 元数据。** `Message` 是 `const FInstancedStruct&`（引用传递），蓝图默认不允许引用引脚留空；正是 `meta = (AutoCreateRefTerm = "Message")` 让蓝图编译器在引脚未连接时自动补一个默认值。去掉它，`Message` 就会变成必连引脚，蓝图会报错。

**无效频道会打 Warning。** 传了一个没有注册过的 Tag 给 `BroadcastMessage`，或者 `RegisterListener` 时回调没绑定，日志里会有 `LogJGDMessageBus` 的警告。

**一个频道建议只对应一种负载结构体。** 总线本身不校验类型，类型对不上时接收端 `GetPtr` / `Get Instanced Struct Value` 会拿不到值。

**不支持网络复制。** 这套总线只在单机/单进程内通信。联机同步需要另做 RPC 层。
