// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/JGDMusicSettings.h"

#include "Engine/World.h"
#include "Misc/PackageName.h"

UJGDMusicSettings::UJGDMusicSettings()
{
	CategoryName = TEXT("Game");
	MenuMapNames.Add(TEXT("Menu"));
}

TSoftObjectPtr<USoundBase> UJGDMusicSettings::GetMusicForMap(FName MapName) const
{
	const FName ShortName(*UWorld::RemovePIEPrefix(FPackageName::GetShortName(MapName.ToString())));
	return MenuMapNames.Contains(ShortName) ? MenuMusic : GameplayMusic;
}

float UJGDMusicSettings::GetSafeLoudness() const
{
	return FMath::IsFinite(MusicLoudness) ? FMath::Clamp(MusicLoudness, 0.0f, 1.0f) : 0.6f;
}
