// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Character/JGDWindTestCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Physics/JGD2DPhysicsParticipantComponent.h"
#include "Physics/JGDPhysicsProfileVolume.h"
#include "Physics/JGDPhysicsWorldSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

namespace JGDPhysicsPIETests
{
	const FName CharacterTag(TEXT("PhysicsAutomationCharacter"));
	const FName BoxTag(TEXT("PhysicsAutomationBox"));
	const FName WindTag(TEXT("PhysicsAutomationWind"));

	class FCheckWindPIE : public IAutomationLatentCommand
	{
	public:
		explicit FCheckWindPIE(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - StartedAt > 30.0)
			{
				Test->AddError(TEXT("Timed out waiting for wind PIE validation."));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			if (!World || !World->HasBegunPlay())
			{
				return false;
			}
			if (Stage == 0)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					if (It->ActorHasTag(CharacterTag)) Character = Cast<AJGDWindTestCharacter>(*It);
					if (It->ActorHasTag(WindTag)) Wind = Cast<AJGDPhysicsProfileVolume>(*It);
					if (It->ActorHasTag(BoxTag))
					{
						TInlineComponentArray<UPrimitiveComponent*> Components(*It);
						for (UPrimitiveComponent* Component : Components)
						{
							if (Component->GetFName() == TEXT("BaseBlock")) Body = Component;
						}
					}
				}
				if (!Test->TestNotNull(TEXT("Existing player blueprint spawned in PIE"), Character.Get())
					|| !Test->TestNotNull(TEXT("Existing wind blueprint spawned in PIE"), Wind.Get())
					|| !Test->TestNotNull(TEXT("Existing box sprite spawned in PIE"), Body.Get()))
				{
					return true;
				}
				Test->TestTrue(TEXT("Real PIE world"), World->WorldType == EWorldType::PIE);
				Test->TestTrue(TEXT("Existing box has physics simulation enabled"), Body->IsSimulatingPhysics());
				Physics = World->GetSubsystem<UJGDPhysicsWorldSubsystem>();
				OriginalProfile = Physics->GetResolvedCharacterProfile(Character.Get());
				CheckDisplayedProfile(OriginalProfile.Get(), TEXT("Initial cached display matches resolved profile"));
				UArrowComponent* Arrow = Wind->FindComponentByClass<UArrowComponent>();
				if (!Test->TestNotNull(TEXT("Wind blueprint exposes a direction arrow"), Arrow))
				{
					return true;
				}
				if (!SetWindDirection(FVector(-1.0f, 0.0f, 0.0f)) || !SetAffectedRigidBodies(true))
				{
					return true;
				}
				// WindDirection is configured to -X on the PIE instance. Point the visual arrow
				// to +X so the force assertions prove that the arrow is not a second source.
				Arrow->SetWorldRotation(FVector::ForwardVector.Rotation());
				// Keep both bodies at a controlled height while measuring horizontal acceleration.
				Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				Character->GetCharacterMovement()->StopMovementImmediately();
				Body->SetEnableGravity(false);
				Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Character->SetActorLocation(FVector(0, 0, 600), false, nullptr, ETeleportType::TeleportPhysics);
				Body->SetWorldLocation(FVector(0, 0, 850), false, nullptr, ETeleportType::TeleportPhysics);
				Stage = 1;
				StageStartedAt = World->GetTimeSeconds();
				return false;
			}
			if (!Character.IsValid() || !Wind.IsValid() || !Body.IsValid())
			{
				Test->AddError(TEXT("A wind test actor was unexpectedly destroyed during PIE."));
				return true;
			}
			if (World->GetTimeSeconds() - StageStartedAt < 0.35f)
			{
				return false;
			}
			if (Stage == 1)
			{
				Test->TestEqual(TEXT("Entering actual brush volume selects wind profile"),
					Physics->GetResolvedCharacterProfile(Character.Get()), Wind->CharacterProfile.Get());
				CheckDisplayedProfile(Wind->CharacterProfile.Get(), TEXT("Wind profile reaches cached display via bus"));
				Test->AddInfo(FString::Printf(TEXT("Wind PIE measured X velocities: character=%.2f, box=%.2f cm/s"),
					Character->GetVelocity().X, Body->GetPhysicsLinearVelocity().X));
				Test->TestTrue(TEXT("Character uses configured -X WindDirection"), Character->GetVelocity().X < -1.0f);
				Test->TestTrue(TEXT("Rigid body uses configured -X WindDirection instead of +X arrow"),
					Body->GetPhysicsLinearVelocity().X < -1.0f);

				UJGD2DPhysicsParticipantComponent* Participant =
					Character->FindComponentByClass<UJGD2DPhysicsParticipantComponent>();
				if (!Test->TestNotNull(TEXT("Existing player exposes its physics participant"), Participant))
				{
					return true;
				}
				Physics->UnregisterCharacter(Character.Get());
				Physics->RegisterCharacter(Character.Get(), Participant->GetBaseProfile());
				Test->TestEqual(TEXT("Registering again inside the brush restores the volume profile"),
					Physics->GetResolvedCharacterProfile(Character.Get()), Wind->CharacterProfile.Get());
				CheckDisplayedProfile(Wind->CharacterProfile.Get(), TEXT("Re-registration keeps the displayed volume profile"));

				if (!SetAffectedRigidBodies(false))
				{
					return true;
				}
				Character->GetCharacterMovement()->ClearAccumulatedForces();
				Character->GetCharacterMovement()->StopMovementImmediately();
				Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Stage = 2;
				StageStartedAt = World->GetTimeSeconds();
				return false;
			}
			if (Stage == 2)
			{
				Test->TestTrue(TEXT("Character wind remains active when only rigid bodies are disabled"),
					Character->GetVelocity().X < -1.0f);
				Test->TestTrue(TEXT("Runtime AffectedRigidBodies=false stops new rigid-body force"),
					FMath::IsNearlyZero(Body->GetPhysicsLinearVelocity().X, 0.1f));
				Character->SetActorLocation(FVector(1500, 0, 600), false, nullptr, ETeleportType::TeleportPhysics);
				Character->GetCharacterMovement()->StopMovementImmediately();
				Body->SetWorldLocation(FVector(1500, 0, 850), false, nullptr, ETeleportType::TeleportPhysics);
				Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Stage = 3;
				StageStartedAt = World->GetTimeSeconds();
				return false;
			}
			Test->TestEqual(TEXT("Leaving brush restores original profile"), Physics->GetResolvedCharacterProfile(Character.Get()), OriginalProfile.Get());
			CheckDisplayedProfile(OriginalProfile.Get(), TEXT("Leaving wind updates cached display"));
			if (Stage == 3)
			{
				// AddForce accumulated before teleport can be consumed on the next movement tick.
				// After overlap exit has settled, start a fresh observation window for NEW forces.
				Character->GetCharacterMovement()->ClearAccumulatedForces();
				Character->GetCharacterMovement()->StopMovementImmediately();
				Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Stage = 4;
				StageStartedAt = World->GetTimeSeconds();
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("Outside wind X velocities: character=%.2f, box=%.2f cm/s"),
				Character->GetVelocity().X, Body->GetPhysicsLinearVelocity().X));
			Test->TestTrue(TEXT("Character stops receiving wind outside volume"), FMath::IsNearlyZero(Character->GetVelocity().X, 0.1f));
			Test->TestTrue(TEXT("Box stops receiving wind outside volume"), FMath::IsNearlyZero(Body->GetPhysicsLinearVelocity().X, 0.1f));
			return true;
		}

	private:
		bool SetWindDirection(const FVector& Direction)
		{
			FStructProperty* Property = FindFProperty<FStructProperty>(Wind->GetClass(), TEXT("WindDirection"));
			if (!Test->TestNotNull(TEXT("Wind blueprint exposes WindDirection"), Property))
			{
				return false;
			}
			*Property->ContainerPtrToValuePtr<FVector>(Wind.Get()) = Direction;
			return true;
		}

		bool SetAffectedRigidBodies(bool bEnabled)
		{
			FBoolProperty* Property = FindFProperty<FBoolProperty>(Wind->GetClass(), TEXT("AffectedRigidBodies"));
			if (!Test->TestNotNull(TEXT("Wind blueprint exposes AffectedRigidBodies"), Property))
			{
				return false;
			}
			Property->SetPropertyValue_InContainer(Wind.Get(), bEnabled);
			return true;
		}

		void CheckDisplayedProfile(UJGDCharacterPhysicsProfile* Expected, const TCHAR* What)
		{
			const auto* Property = FindFProperty<FObjectPropertyBase>(AJGDWindTestCharacter::StaticClass(), TEXT("DisplayedPhysicsProfile"));
			Test->TestEqual(What, Property->GetObjectPropertyValue_InContainer(Character.Get()), static_cast<UObject*>(Expected));
		}

		FAutomationTestBase* Test;
		TWeakObjectPtr<AJGDWindTestCharacter> Character;
		TWeakObjectPtr<AJGDPhysicsProfileVolume> Wind;
		TWeakObjectPtr<UPrimitiveComponent> Body;
		TWeakObjectPtr<UJGDCharacterPhysicsProfile> OriginalProfile;
		UJGDPhysicsWorldSubsystem* Physics = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		float StageStartedAt = 0.0f;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDPhysicsWindPIETest, "JGD2026.Physics.PIE.WindBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDPhysicsWindPIETest::RunTest(const FString& Parameters)
{
	using namespace JGDPhysicsPIETests;
	if (GEditor->PlayWorld || GEditor->GetEditorWorldContext().World()->GetOutermost()->IsDirty())
	{
		AddError(TEXT("Run the PIE regression in a separate editor process or with a saved, idle editor. No unsaved map will be discarded."));
		return false;
	}
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	UClass* CharacterClass = LoadClass<AJGDWindTestCharacter>(nullptr, TEXT("/Game/Physics/BP_Player_WindTest.BP_Player_WindTest_C"));
	UClass* WindClass = LoadClass<AJGDPhysicsProfileVolume>(nullptr, TEXT("/Game/Physics/Volume/BP_WindVolume_Test.BP_WindVolume_Test_C"));
	UClass* BoxClass = LoadClass<AActor>(nullptr, TEXT("/Game/Physics/BP_TestBox.BP_TestBox_C"));
	if (!TestNotNull(TEXT("Player blueprint loads"), CharacterClass)
		|| !TestNotNull(TEXT("Wind blueprint loads"), WindClass) || !TestNotNull(TEXT("Box blueprint loads"), BoxClass))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Character = World->SpawnActor<AJGDWindTestCharacter>(CharacterClass, FVector(-1500, 0, 600), FRotator::ZeroRotator, Params);
	Character->Tags.Add(CharacterTag);
	auto* Box = World->SpawnActor<AActor>(BoxClass, FVector(-1500, 0, 850), FRotator::ZeroRotator, Params);
	Box->Tags.Add(BoxTag);
	auto* Wind = World->SpawnActor<AJGDPhysicsProfileVolume>(WindClass, FVector(0, 0, 600), FRotator::ZeroRotator, Params);
	FStructProperty* WindDirectionProperty = FindFProperty<FStructProperty>(WindClass, TEXT("WindDirection"));
	FBoolProperty* AffectedRigidBodiesProperty = FindFProperty<FBoolProperty>(WindClass, TEXT("AffectedRigidBodies"));
	if (!TestNotNull(TEXT("Wind blueprint exposes WindDirection"), WindDirectionProperty)
		|| !TestNotNull(TEXT("Wind blueprint exposes AffectedRigidBodies"), AffectedRigidBodiesProperty))
	{
		return false;
	}
	TestFalse(TEXT("Wind blueprint keeps rigid bodies disabled by default"),
		AffectedRigidBodiesProperty->GetPropertyValue_InContainer(Wind));
	Wind->Tags.Add(WindTag);
	Wind->Priority = 10;
	auto* Builder = NewObject<UCubeBuilder>();
	Builder->X = 1000.0f;
	Builder->Y = 600.0f;
	Builder->Z = 1000.0f;
	UActorFactory::CreateBrushForVolumeActor(Wind, Builder);
	Wind->SetActorLocation(FVector(0, 0, 600));
	World->SpawnActor<APlayerStart>(FVector(-1500, 0, 600), FRotator::ZeroRotator, Params);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckWindPIE(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.25f));
	return true;
}

#endif
