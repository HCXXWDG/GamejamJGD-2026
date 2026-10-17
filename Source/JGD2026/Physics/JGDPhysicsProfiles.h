// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "JGDPhysicsProfiles.generated.h"

class UCharacterMovementComponent;
class UPrimitiveComponent;

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

/** Resolved rigid-body settings applied only while a body is registered with the world subsystem. */
UCLASS(BlueprintType)
class JGD2026_API UJGDPhysicsRigidBodyProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rigid Body")
	bool bEnableGravity = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rigid Body", meta = (ClampMin = "0.0"))
	float LinearDamping = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rigid Body", meta = (ClampMin = "0.0"))
	float AngularDamping = 0.0f;

	void ApplyTo(UPrimitiveComponent& Body) const;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rigid Body")
	TObjectPtr<UJGDPhysicsRigidBodyProfile> DefaultRigidBodyProfile;
};

/** Shared, designer-authored drive values for the runtime assembly physics executor. */
UCLASS(BlueprintType)
class JGD2026_API UJGDAssemblyPhysicsDriveProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0"))
	float MuscleStiffness = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0"))
	float MuscleDamping = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0"))
	float MuscleMaxForce = 30000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float MuscleContractionRatio = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraints", meta = (ClampMin = "0.0"))
	float JointBreakForce = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraints", meta = (ClampMin = "0.0"))
	float JointBreakTorque = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Core", meta = (ClampMin = "0.0"))
	float CoreRotationTorque = 50000.0f;

	bool HasValidParameters() const;
};
