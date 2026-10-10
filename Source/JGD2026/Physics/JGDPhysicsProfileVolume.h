// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "JGDPhysicsProfileVolume.generated.h"

class UJGDCharacterPhysicsProfile;

/** Native PhysicsVolume that supplies a character profile while a character is inside it. */
UCLASS(Blueprintable)
class JGD2026_API AJGDPhysicsProfileVolume : public APhysicsVolume
{
	GENERATED_BODY()

public:
	AJGDPhysicsProfileVolume();

	virtual void ActorEnteredVolume(AActor* Other) override;
	virtual void ActorLeavingVolume(AActor* Other) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JGD")
	TObjectPtr<UJGDCharacterPhysicsProfile> CharacterProfile;
};
