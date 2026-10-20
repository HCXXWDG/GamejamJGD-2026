// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/JGDPhysicsProfiles.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"

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

void UJGDPhysicsRigidBodyProfile::ApplyTo(UPrimitiveComponent& Body) const
{
	Body.SetEnableGravity(bEnableGravity);
	Body.SetLinearDamping(FMath::Max(0.0f, LinearDamping));
	Body.SetAngularDamping(FMath::Max(0.0f, AngularDamping));
}

bool UJGDAssemblyPhysicsDriveProfile::HasValidParameters() const
{
	return FMath::IsFinite(ContactFriction) && ContactFriction >= 0.0f
		&& FMath::IsFinite(ContactStaticFriction) && ContactStaticFriction >= 0.0f
		&& FMath::IsFinite(ContactRestitution) && ContactRestitution >= 0.0f && ContactRestitution <= 1.0f
		&& FMath::IsFinite(MuscleStiffness) && MuscleStiffness >= 0.0f
		&& FMath::IsFinite(MuscleDamping) && MuscleDamping >= 0.0f
		&& FMath::IsFinite(MuscleMaxForce) && MuscleMaxForce >= 0.0f
		&& FMath::IsFinite(MuscleContractionStrength) && MuscleContractionStrength >= 1.0f && MuscleContractionStrength <= 3.0f
		&& FMath::IsFinite(MuscleContractionRatio)
		&& MuscleContractionRatio >= 0.0f && MuscleContractionRatio <= 0.95f
		&& FMath::IsFinite(MuscleTransitionTime) && MuscleTransitionTime >= 0.0f
		&& FMath::IsFinite(JointBreakForce) && JointBreakForce >= 0.0f
		&& FMath::IsFinite(JointBreakTorque) && JointBreakTorque >= 0.0f
		&& FMath::IsFinite(CoreRotationTorque) && CoreRotationTorque >= 0.0f;
}
