// Copyright Epic Games, Inc. All Rights Reserved.

#include "JGDAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogJGDAudio, Log, All);

UJGDAudioSubsystem* UJGDAudioSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;

	return GameInstance ? GameInstance->GetSubsystem<UJGDAudioSubsystem>() : nullptr;
}

UJGDAudioSubsystem* UJGDAudioSubsystem::GetAudioSubsystem(const UObject* WorldContextObject)
{
	return Get(WorldContextObject);
}

UWorld* UJGDAudioSubsystem::GetGameWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();

	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

FJGDAudioHandle UJGDAudioSubsystem::PlayAudioAtLocation(USoundBase* Sound, FVector Location, float Loudness,
	USoundAttenuation* AttenuationOverride, USoundConcurrency* Concurrency)
{
	if (Sound == nullptr)
	{
		UE_LOG(LogJGDAudio, Warning, TEXT("PlayAudioAtLocation 失败：音频资源为空。"));
		return FJGDAudioHandle();
	}

	UWorld* World = GetGameWorld();
	if (World == nullptr)
	{
		UE_LOG(LogJGDAudio, Warning, TEXT("PlayAudioAtLocation 失败：拿不到 World。"));
		return FJGDAudioHandle();
	}

	// 负响度没有意义，钳到 0；允许 > 1 放大。
	const float SafeLoudness = FMath::Max(0.0f, Loudness);

	UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(
		World,
		Sound,
		Location,
		FRotator::ZeroRotator,
		SafeLoudness,
		/*PitchMultiplier=*/1.0f,
		/*StartTime=*/0.0f,
		AttenuationOverride,
		Concurrency,
		/*bAutoDestroy=*/true);

	return RegisterVoice(Component);
}

FJGDAudioHandle UJGDAudioSubsystem::PlayAudio2D(USoundBase* Sound, float Loudness, USoundConcurrency* Concurrency)
{
	if (Sound == nullptr)
	{
		UE_LOG(LogJGDAudio, Warning, TEXT("PlayAudio2D 失败：音频资源为空。"));
		return FJGDAudioHandle();
	}

	UWorld* World = GetGameWorld();
	if (World == nullptr)
	{
		UE_LOG(LogJGDAudio, Warning, TEXT("PlayAudio2D 失败：拿不到 World。"));
		return FJGDAudioHandle();
	}

	const float SafeLoudness = FMath::Max(0.0f, Loudness);

	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(
		World,
		Sound,
		SafeLoudness,
		/*PitchMultiplier=*/1.0f,
		/*StartTime=*/0.0f,
		Concurrency,
		/*bPersistAcrossLevelTransition=*/false,
		/*bAutoDestroy=*/true);

	return RegisterVoice(Component);
}

void UJGDAudioSubsystem::StopAudio(FJGDAudioHandle& Handle, float FadeOutTime)
{
	if (UAudioComponent* Component = FindVoice(Handle))
	{
		if (FadeOutTime > 0.0f)
		{
			// 淡出期间保留条目，免得组件在淡出过程中失去引用；播完后由 PruneDeadVoices 清掉。
			Component->FadeOut(FadeOutTime, 0.0f);
		}
		else
		{
			Component->Stop();
			ActiveVoices.Remove(Handle.VoiceId);
		}
	}
	else
	{
		ActiveVoices.Remove(Handle.VoiceId);
	}

	Handle.Invalidate();
}

void UJGDAudioSubsystem::StopAllAudio(float FadeOutTime)
{
	// 先拷一份 key，回调里可能改动 ActiveVoices。
	TArray<int32> VoiceIds;
	ActiveVoices.GetKeys(VoiceIds);

	for (int32 VoiceId : VoiceIds)
	{
		TObjectPtr<UAudioComponent>* Found = ActiveVoices.Find(VoiceId);
		if (Found == nullptr)
		{
			continue;
		}

		if (UAudioComponent* Component = *Found; IsValid(Component))
		{
			if (FadeOutTime > 0.0f)
			{
				Component->FadeOut(FadeOutTime, 0.0f);
				continue;
			}

			Component->Stop();
		}

		ActiveVoices.Remove(VoiceId);
	}
}

void UJGDAudioSubsystem::SetAudioLoudness(FJGDAudioHandle Handle, float Loudness)
{
	if (UAudioComponent* Component = FindVoice(Handle))
	{
		Component->SetVolumeMultiplier(FMath::Max(0.0f, Loudness));
	}
}

void UJGDAudioSubsystem::SetAudioLocation(FJGDAudioHandle Handle, FVector Location)
{
	if (UAudioComponent* Component = FindVoice(Handle))
	{
		Component->SetWorldLocation(Location);
	}
}

bool UJGDAudioSubsystem::IsAudioPlaying(FJGDAudioHandle Handle) const
{
	const UAudioComponent* Component = FindVoice(Handle);

	return Component != nullptr && Component->IsPlaying();
}

int32 UJGDAudioSubsystem::GetActiveVoiceCount() const
{
	int32 Count = 0;

	for (const TPair<int32, TObjectPtr<UAudioComponent>>& Pair : ActiveVoices)
	{
		if (IsValid(Pair.Value))
		{
			++Count;
		}
	}

	return Count;
}

void UJGDAudioSubsystem::Deinitialize()
{
	for (const TPair<int32, TObjectPtr<UAudioComponent>>& Pair : ActiveVoices)
	{
		if (UAudioComponent* Component = Pair.Value; IsValid(Component))
		{
			Component->Stop();
		}
	}

	ActiveVoices.Empty();
	NextVoiceId = 0;

	Super::Deinitialize();
}

UAudioComponent* UJGDAudioSubsystem::FindVoice(FJGDAudioHandle Handle) const
{
	if (!Handle.bValid)
	{
		return nullptr;
	}

	const TObjectPtr<UAudioComponent>* Found = ActiveVoices.Find(Handle.VoiceId);

	if (Found == nullptr || !IsValid(*Found))
	{
		return nullptr;
	}

	return Found->Get();
}

FJGDAudioHandle UJGDAudioSubsystem::RegisterVoice(UAudioComponent* Component)
{
	FJGDAudioHandle Handle;

	if (Component == nullptr)
	{
		// 引擎对"起始就超出所有监听者可听范围"的短音会直接不创建组件，这不是错误。
		UE_LOG(LogJGDAudio, Verbose, TEXT("声音未创建（资源无效，或超出所有监听者的可听范围）。"));
		return Handle;
	}

	PruneDeadVoices();

	Handle.VoiceId = NextVoiceId++;
	Handle.bValid = true;

	ActiveVoices.Add(Handle.VoiceId, Component);

	return Handle;
}

void UJGDAudioSubsystem::PruneDeadVoices()
{
	for (auto It = ActiveVoices.CreateIterator(); It; ++It)
	{
		if (!IsValid(It.Value()))
		{
			It.RemoveCurrent();
		}
	}
}
