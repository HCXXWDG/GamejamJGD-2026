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
	/** Sliding grip applied to the assembly's real physics bodies while playing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact", meta = (ClampMin = "0.0"))
	float ContactFriction = 1.2f;

	/** Grip before a resting contact starts sliding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact", meta = (ClampMin = "0.0"))
	float ContactStaticFriction = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ContactRestitution = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0"))
	float MuscleStiffness = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0"))
	float MuscleDamping = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0"))
	float MuscleMaxForce = 100000.0f;

	/** Active stiffness and force-limit multiplier. Passive support keeps its normal strength. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float MuscleContractionStrength = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float MuscleContractionRatio = 0.3f;

	/** Time constant in seconds for contraction and return to the authored assembly length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Muscles", meta = (ClampMin = "0.0", Units = "s"))
	float MuscleTransitionTime = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraints", meta = (ClampMin = "0.0"))
	float JointBreakForce = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraints", meta = (ClampMin = "0.0"))
	float JointBreakTorque = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Core", meta = (ClampMin = "0.0"))
	float CoreRotationTorque = 1000000.0f;

	bool HasValidParameters() const;
};
