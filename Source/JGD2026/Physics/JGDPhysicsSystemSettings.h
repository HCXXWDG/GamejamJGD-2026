// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "JGDPhysicsSystemSettings.generated.h"

class UJGDPhysicsWorldProfile;

/** Project-wide locator for the designer-authored default physics profile. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "JGD Physics System"))
class JGD2026_API UJGDPhysicsSystemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category = "Defaults")
	TSoftObjectPtr<UJGDPhysicsWorldProfile> DefaultWorldProfile;
};
