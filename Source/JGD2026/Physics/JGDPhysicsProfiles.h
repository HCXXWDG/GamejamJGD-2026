// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "JGDPhysicsProfiles.generated.h"

class UCharacterMovementComponent;

/** Tunable movement values shared by global and volume-based character profiles. */
USTRUCT(BlueprintType)
struct JGD2026_API FJGDCharacterPhysicsSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float GravityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MaxWalkSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MaxAcceleration = 2048.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float GroundFriction = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float BrakingDecelerationWalking = 2048.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jumping", meta = (ClampMin = "0.0"))
	float JumpZVelocity = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jumping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Planar Movement")
	bool bConstrainToPlane = true;

	/** Normal of the gameplay plane. Y is the depth axis for this project's XZ gameplay plane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Planar Movement", meta = (EditCondition = "bConstrainToPlane"))
	FVector PlaneConstraintNormal = FVector::YAxisVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Planar Movement", meta = (EditCondition = "bConstrainToPlane"))
	bool bSnapToPlaneWhenApplied = true;

	void ApplyTo(UCharacterMovementComponent& MovementComponent) const;
};

/** Designer-authored character movement preset. */
UCLASS(BlueprintType)
class JGD2026_API UJGDCharacterPhysicsProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics")
	FJGDCharacterPhysicsSettings Settings;
};

/** Global physics preset loaded once for each gameplay world. */
UCLASS(BlueprintType)
class JGD2026_API UJGDPhysicsWorldProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	bool bOverrideWorldGravity = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World", meta = (EditCondition = "bOverrideWorldGravity", Units = "cm/s^2"))
	float WorldGravityZ = -980.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TObjectPtr<UJGDCharacterPhysicsProfile> DefaultCharacterProfile;
};
