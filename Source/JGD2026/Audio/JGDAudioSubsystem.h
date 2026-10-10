// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "JGDAudioTypes.h"
#include "JGDAudioSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;

/**
 * 音频总线。
 *
 * 调用方只需要给出「资源 + 坐标 + 响度」，不需要关心资源是 MetaSound、SoundCue 还是 SoundWave
 * —— 这三者在引擎里都派生自 USoundBase，走同一个入口，总线内部不做任何分支。
 *
 *   USoundBase
 *   ├── USoundWave            (.wav)
 *   ├── USoundCue             (cue)
 *   └── USoundWaveProcedural
 *       └── UMetaSoundSource  (MetaSound)
 *
 * 挂在 UGameInstanceSubsystem 上，跨关卡存活。
 */
UCLASS()
class JGD2026_API UJGDAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** C++ 侧取总线：UJGDAudioSubsystem::Get(this) */
	static UJGDAudioSubsystem* Get(const UObject* WorldContextObject);

	/** 蓝图侧取总线（纯函数，没有执行引脚） */
	UFUNCTION(BlueprintPure, Category = "JGD|Audio",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get JGD Audio Subsystem"))
	static UJGDAudioSubsystem* GetAudioSubsystem(const UObject* WorldContextObject);

	/**
	 * 在世界坐标处播放一段音频（3D 空间化，按距离衰减）。
	 *
	 * @param Sound               音频资源。MetaSound / SoundCue / SoundWave 都可以。
	 * @param Location            世界坐标。
	 * @param Loudness            线性响度倍率。1.0 = 资源原始音量，0.5 = 减半，0.0 = 静音，允许 > 1 放大。
	 * @param AttenuationOverride 留空则用资源自带的衰减设置。
	 * @param Concurrency         并发限制，留空表示不限制。
	 * @return 句柄。资源为空或声音超出所有监听者可听范围时返回无效句柄。
	 */
	UFUNCTION(BlueprintCallable, Category = "JGD|Audio", meta = (AdvancedDisplay = "AttenuationOverride,Concurrency"))
	FJGDAudioHandle PlayAudioAtLocation(USoundBase* Sound, FVector Location, float Loudness = 1.0f,
		USoundAttenuation* AttenuationOverride = nullptr, USoundConcurrency* Concurrency = nullptr);

	/**
	 * 非空间化播放（UI 音、全局音效），与坐标无关。
	 *
	 * @param Loudness 线性响度倍率，语义同 PlayAudioAtLocation。
	 */
	UFUNCTION(BlueprintCallable, Category = "JGD|Audio", meta = (AdvancedDisplay = "Concurrency"))
	FJGDAudioHandle PlayAudio2D(USoundBase* Sound, float Loudness = 1.0f, USoundConcurrency* Concurrency = nullptr);

	/** 停止。FadeOutTime > 0 时淡出。重复调用安全。 */
	UFUNCTION(BlueprintCallable, Category = "JGD|Audio")
	void StopAudio(UPARAM(ref) FJGDAudioHandle& Handle, float FadeOutTime = 0.0f);

	/** 停止当前由本总线播放的全部声音。 */
	UFUNCTION(BlueprintCallable, Category = "JGD|Audio")
	void StopAllAudio(float FadeOutTime = 0.0f);

	/** 实时改响度，语义同 PlayAudioAtLocation 的 Loudness。 */
	UFUNCTION(BlueprintCallable, Category = "JGD|Audio")
	void SetAudioLoudness(FJGDAudioHandle Handle, float Loudness);

	/** 实时改坐标，用来让声音跟随移动的对象。 */
	UFUNCTION(BlueprintCallable, Category = "JGD|Audio")
	void SetAudioLocation(FJGDAudioHandle Handle, FVector Location);

	/** 这个句柄对应的声音还在响吗。 */
	UFUNCTION(BlueprintPure, Category = "JGD|Audio")
	bool IsAudioPlaying(FJGDAudioHandle Handle) const;

	/** 当前活跃声音数，调试用。 */
	UFUNCTION(BlueprintPure, Category = "JGD|Audio")
	int32 GetActiveVoiceCount() const;

	virtual void Deinitialize() override;

private:
	/** 找到句柄对应的、仍然活着的 AudioComponent，找不到返回 nullptr */
	UAudioComponent* FindVoice(FJGDAudioHandle Handle) const;

	FJGDAudioHandle RegisterVoice(UAudioComponent* Component);

	/** 清掉已经被销毁的 AudioComponent 条目 */
	void PruneDeadVoices();

	/** 当前世界 */
	UWorld* GetGameWorld() const;

	/**
	 * VoiceId -> 正在播放的 AudioComponent。
	 *
	 * 这里刻意用强引用（UPROPERTY 让 GC 追踪）：组件是引擎在 Transient 包里创建的，
	 * 我们自己拿着引用才能保证它在播放期间不被回收。播完（bAutoDestroy）组件会自己销毁，
	 * 那时 IsValid() 变 false，由 PruneDeadVoices 清掉。
	 */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAudioComponent>> ActiveVoices;

	int32 NextVoiceId = 0;
};
