// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Physics/JGDPhysicsProfiles.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FJGDCharacterPhysicsProfileApplicationTest,
	"JGD2026.Physics.CharacterProfile.Application",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDCharacterPhysicsProfileApplicationTest::RunTest(const FString& Parameters)
{
	UCharacterMovementComponent* Movement = NewObject<UCharacterMovementComponent>();
	TestNotNull(TEXT("Movement component can be created"), Movement);
	if (!Movement)
	{
		return false;
	}

	FJGDCharacterPhysicsSettings Settings;
	Settings.GravityScale = 0.5f;
	Settings.MaxWalkSpeed = 900.0f;
	Settings.MaxAcceleration = 3000.0f;
	Settings.GroundFriction = 4.0f;
	Settings.BrakingDecelerationWalking = 1200.0f;
	Settings.JumpZVelocity = 650.0f;
	Settings.AirControl = 0.4f;
	Settings.PlaneConstraintNormal = FVector::ZeroVector;
	Settings.bSnapToPlaneWhenApplied = false;
	Settings.ApplyTo(*Movement);

	TestEqual(TEXT("Gravity scale"), Movement->GravityScale, 0.5f);
	TestEqual(TEXT("Maximum walk speed"), Movement->MaxWalkSpeed, 900.0f);
	TestEqual(TEXT("Maximum acceleration"), Movement->MaxAcceleration, 3000.0f);
	TestEqual(TEXT("Ground friction"), Movement->GroundFriction, 4.0f);
	TestEqual(TEXT("Walking braking deceleration"), Movement->BrakingDecelerationWalking, 1200.0f);
	TestEqual(TEXT("Jump velocity"), Movement->JumpZVelocity, 650.0f);
	TestEqual(TEXT("Air control"), Movement->AirControl, 0.4f);
	TestTrue(TEXT("Plane constraint is enabled"), Movement->bConstrainToPlane);
	TestTrue(
		TEXT("Invalid plane normals fall back to the XZ plane"),
		Movement->GetPlaneConstraintNormal().Equals(FVector::YAxisVector));

	return true;
}

#endif
