// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Audio/JGDAudioTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "JGDLevelMusicSubsystem.generated.h"

class UJGDAudioSubsystem;
class USoundBase;

/** Owns one world's music voice; the existing GameInstance audio bus plays it. */
UCLASS()
class JGD2026_API UJGDLevelMusicSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Applies current project settings. An already playing selection keeps its position. */
	UFUNCTION(BlueprintCallable, Category = "JGD|Audio|Music")
	bool RefreshMusic();

	UFUNCTION(BlueprintPure, Category = "JGD|Audio|Music")
	USoundBase* GetCurrentMusic() const;

	UFUNCTION(BlueprintPure, Category = "JGD|Audio|Music")
	FJGDAudioHandle GetMusicHandle() const;

	UFUNCTION(BlueprintPure, Category = "JGD|Audio|Music")
	bool IsMusicPlaying() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void StopCurrentMusic();

	UPROPERTY(Transient)
	TWeakObjectPtr<UJGDAudioSubsystem> AudioBus;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CurrentMusic;

	UPROPERTY(Transient)
	FJGDAudioHandle MusicHandle;

	bool bDeinitializing = false;
};
