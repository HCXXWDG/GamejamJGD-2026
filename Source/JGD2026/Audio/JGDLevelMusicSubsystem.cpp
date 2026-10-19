// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/JGDLevelMusicSubsystem.h"

#include "Assembly/AssemblyPlayPreferences.h"
#include "Audio/JGDAudioSubsystem.h"
#include "Audio/JGDMusicSettings.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

bool UJGDLevelMusicSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UJGDLevelMusicSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RefreshMusic();
}

bool UJGDLevelMusicSubsystem::RefreshMusic()
{
	UWorld* World = GetWorld();
	if (bDeinitializing || !HasCalledBeginPlay() || !World || World->bIsTearingDown
		|| !World->IsGameWorld() || World->IsNetMode(NM_DedicatedServer))
	{
		StopCurrentMusic();
		return false;
	}

	// Menu maps do not create an assembly toolbar, but must still respect saved volume.
	UAssemblyPlayPreferences::Get()->ApplyAudio(this);
	const UJGDMusicSettings* Settings = GetDefault<UJGDMusicSettings>();
	const TSoftObjectPtr<USoundBase> Selection = Settings->GetMusicForMap(
		FName(*UGameplayStatics::GetCurrentLevelName(World, true)));
	if (!Settings->bAutoPlayMusic || Selection.IsNull())
	{
		StopCurrentMusic();
		return false;
	}

	USoundBase* DesiredMusic = Selection.LoadSynchronous();
	UJGDAudioSubsystem* Bus = UJGDAudioSubsystem::Get(this);
	if (!IsValid(DesiredMusic) || !IsValid(Bus))
	{
		StopCurrentMusic();
		return false;
	}

	if (AudioBus.Get() == Bus && CurrentMusic == DesiredMusic && Bus->IsAudioPlaying(MusicHandle))
	{
		Bus->SetAudioLoudness(MusicHandle, Settings->GetSafeLoudness());
		return true;
	}

	StopCurrentMusic();
	AudioBus = Bus;
	// The existing 2D bus treats this as a UI sound: pause/settings leave music playing.
	MusicHandle = Bus->PlayAudio2D(DesiredMusic, Settings->GetSafeLoudness());
	if (!Bus->IsAudioPlaying(MusicHandle))
	{
		StopCurrentMusic();
		return false;
	}
	CurrentMusic = DesiredMusic;
	return true;
}

USoundBase* UJGDLevelMusicSubsystem::GetCurrentMusic() const
{
	return CurrentMusic.Get();
}

FJGDAudioHandle UJGDLevelMusicSubsystem::GetMusicHandle() const
{
	return MusicHandle;
}

bool UJGDLevelMusicSubsystem::IsMusicPlaying() const
{
	const UJGDAudioSubsystem* Bus = AudioBus.Get();
	return Bus && Bus->IsAudioPlaying(MusicHandle);
}

void UJGDLevelMusicSubsystem::StopCurrentMusic()
{
	if (UJGDAudioSubsystem* Bus = AudioBus.Get())
	{
		// Do not stop UI cues or other modules' audio when this world goes away.
		Bus->StopAudio(MusicHandle);
	}
	MusicHandle.Invalidate();
	CurrentMusic = nullptr;
	AudioBus.Reset();
}

void UJGDLevelMusicSubsystem::Deinitialize()
{
	bDeinitializing = true;
	StopCurrentMusic();
	Super::Deinitialize();
}
