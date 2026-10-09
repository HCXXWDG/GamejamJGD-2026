// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/JGDPhysicsProfiles.h"

#include "GameFramework/CharacterMovementComponent.h"

void FJGDCharacterPhysicsSettings::ApplyTo(UCharacterMovementComponent& MovementComponent) const
{
	MovementComponent.GravityScale = FMath::Max(0.0f, GravityScale);
	MovementComponent.MaxWalkSpeed = FMath::Max(0.0f, MaxWalkSpeed);
	MovementComponent.MaxAcceleration = FMath::Max(0.0f, MaxAcceleration);
	MovementComponent.GroundFriction = FMath::Max(0.0f, GroundFriction);
	MovementComponent.BrakingDecelerationWalking = FMath::Max(0.0f, BrakingDecelerationWalking);
	MovementComponent.JumpZVelocity = FMath::Max(0.0f, JumpZVelocity);
	MovementComponent.AirControl = FMath::Clamp(AirControl, 0.0f, 1.0f);

	MovementComponent.SetPlaneConstraintEnabled(bConstrainToPlane);
	if (!bConstrainToPlane)
	{
		return;
	}

	const FVector SafeNormal = PlaneConstraintNormal.IsNearlyZero()
		? FVector::YAxisVector
		: PlaneConstraintNormal.GetSafeNormal();
	MovementComponent.SetPlaneConstraintNormal(SafeNormal);

	if (bSnapToPlaneWhenApplied)
	{
		MovementComponent.SnapUpdatedComponentToPlane();
	}
}
