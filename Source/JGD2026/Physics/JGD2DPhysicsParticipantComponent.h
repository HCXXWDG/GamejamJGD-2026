// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JGD2DPhysicsParticipantComponent.generated.h"

class UJGDCharacterPhysicsProfile;

/** Opt-in bridge that registers a Character with the world physics subsystem. */
UCLASS(ClassGroup = (JGD), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class JGD2026_API UJGD2DPhysicsParticipantComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJGD2DPhysicsParticipantComponent();

	UFUNCTION(BlueprintCallable, Category = "JGD|Physics")
	void SetBaseProfile(UJGDCharacterPhysicsProfile* NewProfile);

	UFUNCTION(BlueprintCallable, Category = "JGD|Physics")
	void RefreshPhysicsProfile();

	UFUNCTION(BlueprintPure, Category = "JGD|Physics")
	UJGDCharacterPhysicsProfile* GetBaseProfile() const { return BaseProfile; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics")
	TObjectPtr<UJGDCharacterPhysicsProfile> BaseProfile;
};
