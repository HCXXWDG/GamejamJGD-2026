// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "JGDMusicSettings.generated.h"

class USoundBase;

/** Selects the looping background music without coupling it to a map Blueprint. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Game Music"))
class JGD2026_API UJGDMusicSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UJGDMusicSettings();

	UPROPERTY(Config, EditAnywhere, Category = "Playback")
	bool bAutoPlayMusic = true;

	UPROPERTY(Config, EditAnywhere, Category = "Music")
	TSoftObjectPtr<USoundBase> MenuMusic;

	UPROPERTY(Config, EditAnywhere, Category = "Music")
	TSoftObjectPtr<USoundBase> GameplayMusic;

	/** Short map names, for example Menu. PIE prefixes are ignored. */
	UPROPERTY(Config, EditAnywhere, Category = "Music")
	TArray<FName> MenuMapNames;

	/** Linear music gain before the player's existing master-volume setting. */
	UPROPERTY(Config, EditAnywhere, Category = "Playback", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MusicLoudness = 0.6f;

	TSoftObjectPtr<USoundBase> GetMusicForMap(FName MapName) const;
	float GetSafeLoudness() const;
};
