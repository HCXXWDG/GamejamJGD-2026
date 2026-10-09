# 音频总线（Audio Bus）

播放音频的统一入口。调用方只需要给出「**音频资源 + 世界坐标 + 响度**」，
不需要知道资源是 MetaSound、SoundCue 还是 SoundWave，也不需要自己管理 `AudioComponent` 的生存期。

| 项 | 位置 |
|---|---|
| 总线实现 | `Source/JGD2026/Audio/JGDAudioSubsystem.h` / `.cpp` |
| 句柄类型 | `Source/JGD2026/Audio/JGDAudioTypes.h` |

总线挂在 `UGameInstanceSubsystem` 上，随 GameInstance 创建、跨关卡存活。

---

## 一、支持的音频格式

MetaSound、SoundCue、SoundWave 在引擎里是同一棵继承树，都派生自 `USoundBase`：

```
USoundBase
├── USoundWave              (.wav)
├── USoundCue               (cue)
└── USoundWaveProcedural
    └── UMetaSoundSource    (MetaSound)
```

所以**一个 `USoundBase*` 参数就全覆盖了** —— 总线内部没有任何 `Cast` 或分支，也不需要依赖 Metasound 插件模块。
以后加新的音频类型（比如 `USoundWaveProcedural` 的自定义子类），不用改总线。

---

## 二、响度（Loudness）

**线性倍率，`1.0` = 资源的原始音量。**

| 传值 | 效果 |
|---|---|
| `0.0` | 静音 |
| `0.5` | 音量减半 |
| `1.0` | 原始音量 |
| `2.0` | 放大一倍 |

- 允许超过 1.0 放大。
- 负值会被**钳到 0**，不会反向。
- 内部直接映射到引擎的 `VolumeMultiplier`（引擎文档对它的描述就是 "a linear scalar multiplied with the volume"），语义完全一致。

> 如果你习惯用分贝：`dB = 20 * log10(Loudness)`。比如 `-6dB ≈ 0.5`，`-20dB ≈ 0.1`。

---

## 三、蓝图用法

### 3.1 获取总线

节点 `Get JGD Audio Subsystem`，`World Context Object` 传 `self` 即可。

这是个**纯函数节点**（没有执行引脚），可以插在任意数据流里，也能直接连到别的节点的输入引脚上。

### 3.2 播放（带坐标，3D 空间化）

```
Get JGD Audio Subsystem
  └─ Play Audio at Location
       ├─ Sound     : 你的音频资源（MetaSound / Cue / Wave 都行）
       ├─ Location  : 世界坐标
       ├─ Loudness  : 1.0
       └─ 返回值 → 存进变量（可选，需要后续控制时才存）
```

`Attenuation Override` 和 `Concurrency` 两个引脚默认收在节点的 **Advanced** 折叠区里，不用管。

- `Attenuation Override` 留空 → 用音频资源自带的衰减设置。
- `Concurrency` 留空 → 不做并发限制。

### 3.3 播放（不带坐标，2D）

UI 音、全局提示音这类不需要定位的声音：

```
Get JGD Audio Subsystem
  └─ Play Audio 2D
       ├─ Sound    : 你的音频资源
       └─ Loudness : 1.0
```

### 3.4 句柄控制

`Play Audio at Location` / `Play Audio 2D` 返回一个**句柄**。把返回值存进变量，就能后续控制：

```
Stop Audio            ← 停止（Fade Out Time > 0 时淡出）
Set Audio Loudness    ← 实时改响度
Set Audio Location    ← 实时改坐标（跟随移动的对象）
Is Audio Playing      ← 还在响吗
```

`Stop Audio` 的句柄引脚是**引用传递**，停止后句柄会自动变成无效（`Is Valid` = false），重复停止是安全的。

不需要后续控制的声音**可以不存返回值**，播完引擎会自动回收。

### 3.5 完整例子：移动地块播放音效并跟随

```
[BP_MoveBlock 开始移动]
  Get JGD Audio Subsystem
    └─ Play Audio at Location
         Sound    : S_MoveBlock_Start
         Location : GetActorLocation
         Loudness : 0.8
         返回值 → Set MoveAudioHandle

[BP_MoveBlock Tick]  ← 只在需要跟随时
  Get JGD Audio Subsystem
    └─ Set Audio Location (MoveAudioHandle, GetActorLocation)

[BP_MoveBlock 移动结束]
  Get JGD Audio Subsystem
    └─ Stop Audio (MoveAudioHandle, Fade Out Time = 0.3)
```

`BP_MoveBlock` 完全不需要引用任何音频相关的类，只需要拿到总线。

---

## 四、C++ 用法

### 4.1 播放

```cpp
#include "Audio/JGDAudioSubsystem.h"

if (UJGDAudioSubsystem* AudioBus = UJGDAudioSubsystem::Get(this))
{
    // 3D：在世界坐标处播放
    FJGDAudioHandle Handle = AudioBus->PlayAudioAtLocation(
        MoveSound,          // USoundBase*，MetaSound / Cue / Wave 都行
        GetActorLocation(), // FVector
        0.8f);              // 响度

    // 2D：UI 音
    AudioBus->PlayAudio2D(UIClickSound, 1.0f);
}
```

### 4.2 句柄控制

```cpp
// 跟随移动
AudioBus->SetAudioLocation(Handle, GetActorLocation());

// 实时改响度
AudioBus->SetAudioLoudness(Handle, 0.3f);

// 淡出停止
AudioBus->StopAudio(Handle, 0.3f);

// 还在响吗
if (AudioBus->IsAudioPlaying(Handle))
{
    // ...
}
```

句柄存在成员变量里，对象销毁时不需要手动清理 —— 声音播完组件会自动销毁，句柄自动失效。

### 4.3 API 速查

```cpp
// 取总线（WorldContextObject 传 this 或任意有 World 的对象）
static UJGDAudioSubsystem* UJGDAudioSubsystem::Get(const UObject* WorldContextObject);

// 3D 播放
FJGDAudioHandle PlayAudioAtLocation(USoundBase* Sound, FVector Location, float Loudness = 1.0f,
                                    USoundAttenuation* AttenuationOverride = nullptr,
                                    USoundConcurrency* Concurrency = nullptr);

// 2D 播放
FJGDAudioHandle PlayAudio2D(USoundBase* Sound, float Loudness = 1.0f,
                            USoundConcurrency* Concurrency = nullptr);

// 控制
void StopAudio(FJGDAudioHandle& Handle, float FadeOutTime = 0.0f);
void StopAllAudio(float FadeOutTime = 0.0f);
void SetAudioLoudness(FJGDAudioHandle Handle, float Loudness);
void SetAudioLocation(FJGDAudioHandle Handle, FVector Location);
bool IsAudioPlaying(FJGDAudioHandle Handle) const;
int32 GetActiveVoiceCount() const;
```

---

## 五、行为细节与常见坑

**世界是 3D 的，XZ 只是玩法平面。** 项目玩法限制在 XZ 平面上，但 Actor 的世界坐标依然是三维的，所以 `Location` 是 `FVector`，空间化交给引擎按世界坐标完成 —— 声源在监听者（摄像机）左边就偏左声道，不需要我们手动算声像。

**衰减来自音频资源本身。** 总线不强制统一的衰减曲线 —— 每个音效用自己的 Attenuation 设置。需要临时覆盖时传 `AttenuationOverride`。

**资源为空会返回无效句柄并打警告。** 日志分类是 `LogJGDAudio`。

**超出可听范围时返回无效句柄，这不是错误。** 引擎对「起始位置就超出所有监听者可听范围」的短音会直接不创建 `AudioComponent`，此时总线返回一个无效句柄（`Is Valid` = false）。用之前判断一下就行，日志里只有 Verbose 级别的记录。

**句柄失效的判断方式**：`Is Valid` 节点，或 C++ 的 `Handle.IsValid()`。声音播完（引擎自动回收组件）或调用过 `Stop Audio` 之后，句柄都会失效。

**淡出期间句柄立即失效，但声音还在响。** `Stop Audio` 传了 `Fade Out Time` 时，句柄马上变无效（不能再控制它），声音会在指定时长内淡出后停止。

**生命周期**：总线随 GameInstance 存活，跨关卡不重置。GameInstance 销毁时会停掉所有由总线播放的声音。如果希望换关卡时清空，在关卡切换处调 `Stop All Audio`。

**不支持网络复制。** 音频是本地表现层，不在网络上同步。联机时每个客户端各自播放。

**并发限制用 `Concurrency` 参数。** 比如同一帧 10 个地块同时移动，想避免 10 个音效叠在一起，就创建一个 `USoundConcurrency` 资源传进来，而不是在玩法代码里做去重。
