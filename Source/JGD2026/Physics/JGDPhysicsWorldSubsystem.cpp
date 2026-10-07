// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/JGDPhysicsWorldSubsystem.h"

#include "JGD2026.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Physics/JGDPhysicsProfileVolume.h"
#include "Physics/JGDPhysicsProfiles.h"
#include "Physics/JGDPhysicsSystemSettings.h"

void UJGDPhysicsWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UJGDPhysicsWorldSubsystem::Deinitialize()
{
	RegisteredCharacters.Empty();
	CurrentWorldProfile = nullptr;
	bHasCapturedOriginalWorldGravity = false;
	Super::Deinitialize();
}

void UJGDPhysicsWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UJGDPhysicsSystemSettings* Settings = GetDefault<UJGDPhysicsSystemSettings>();
	UJGDPhysicsWorldProfile* Profile = Settings ? Settings->DefaultWorldProfile.LoadSynchronous() : nullptr;
	ApplyWorldProfile(Profile);

	if (!Profile)
	{
		UE_LOG(LogJGDPhysics, Warning,
			TEXT("No default world physics profile is configured. Built-in character defaults will be used."));
	}
}

void UJGDPhysicsWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	RegisteredCharacters.Empty();
	CurrentWorldProfile = nullptr;
	bHasCapturedOriginalWorldGravity = false;
	Super::OnWorldEndPlay(InWorld);
}

bool UJGDPhysicsWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::GamePreview;
}

void UJGDPhysicsWorldSubsystem::RegisterCharacter(
	ACharacter* Character,
	UJGDCharacterPhysicsProfile* BaseProfile)
{
	if (!IsValid(Character) || !Character->GetCharacterMovement())
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Cannot register an invalid character or a character without movement."));
		return;
	}

	FJGDCharacterPhysicsRegistration& Registration = RegisteredCharacters.FindOrAdd(Character);
	Registration.BaseProfile = BaseProfile;
	ApplyResolvedProfile(*Character, Registration);
}

void UJGDPhysicsWorldSubsystem::UnregisterCharacter(ACharacter* Character)
{
	if (Character)
	{
		RegisteredCharacters.Remove(Character);
	}
}

void UJGDPhysicsWorldSubsystem::SetCharacterBaseProfile(
	ACharacter* Character,
	UJGDCharacterPhysicsProfile* BaseProfile)
{
	if (!IsValid(Character))
	{
		return;
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		Registration->BaseProfile = BaseProfile;
		ApplyResolvedProfile(*Character, *Registration);
		return;
	}

	RegisterCharacter(Character, BaseProfile);
}

void UJGDPhysicsWorldSubsystem::RefreshCharacterProfile(ACharacter* Character)
{
	if (!IsValid(Character))
	{
		return;
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		ApplyResolvedProfile(*Character, *Registration);
	}
}

UJGDCharacterPhysicsProfile* UJGDPhysicsWorldSubsystem::GetResolvedCharacterProfile(ACharacter* Character) const
{
	if (const FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		return ResolveProfile(*Registration);
	}

	return CurrentWorldProfile ? CurrentWorldProfile->DefaultCharacterProfile.Get() : nullptr;
}

void UJGDPhysicsWorldSubsystem::ApplyWorldProfile(UJGDPhysicsWorldProfile* WorldProfile)
{
	CurrentWorldProfile = WorldProfile;

	if (UWorld* World = GetWorld())
	{
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			if (!bHasCapturedOriginalWorldGravity)
			{
				bOriginalGlobalGravitySet = WorldSettings->bGlobalGravitySet;
				OriginalGlobalGravityZ = WorldSettings->GlobalGravityZ;
				bHasCapturedOriginalWorldGravity = true;
			}

			const bool bOverrideGravity = WorldProfile && WorldProfile->bOverrideWorldGravity;
			if (bOverrideGravity)
			{
				WorldSettings->bGlobalGravitySet = true;
				WorldSettings->GlobalGravityZ = WorldProfile->WorldGravityZ;
			}
			else
			{
				WorldSettings->bGlobalGravitySet = bOriginalGlobalGravitySet;
				WorldSettings->GlobalGravityZ = OriginalGlobalGravityZ;
			}

			// Reset the cached value. The world updates the Chaos scene from this value each frame.
			WorldSettings->bWorldGravitySet = false;
			WorldSettings->GetGravityZ();
		}
	}

	RefreshAllCharacters();
}

void UJGDPhysicsWorldSubsystem::NotifyCharacterEnteredVolume(
	ACharacter* Character,
	AJGDPhysicsProfileVolume* Volume)
{
	if (!IsValid(Character) || !IsValid(Volume))
	{
		return;
	}

	if (!RegisteredCharacters.Contains(Character))
	{
		RegisterCharacter(Character);
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		Registration->ActiveVolumes.AddUnique(Volume);
		ApplyResolvedProfile(*Character, *Registration);
	}
}

void UJGDPhysicsWorldSubsystem::NotifyCharacterLeftVolume(
	ACharacter* Character,
	AJGDPhysicsProfileVolume* Volume)
{
	if (!IsValid(Character) || !Volume)
	{
		return;
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		Registration->ActiveVolumes.RemoveAll(
			[Volume](const TWeakObjectPtr<AJGDPhysicsProfileVolume>& Item)
			{
				return !Item.IsValid() || Item.Get() == Volume;
			});
		ApplyResolvedProfile(*Character, *Registration);
	}
}

void UJGDPhysicsWorldSubsystem::NotifyVolumeRemoved(AJGDPhysicsProfileVolume* Volume)
{
	if (!Volume)
	{
		return;
	}

	for (auto It = RegisteredCharacters.CreateIterator(); It; ++It)
	{
		ACharacter* Character = It.Key().Get();
		if (!IsValid(Character))
		{
			It.RemoveCurrent();
			continue;
		}

		FJGDCharacterPhysicsRegistration& Registration = It.Value();
		const int32 RemovedCount = Registration.ActiveVolumes.RemoveAll(
			[Volume](const TWeakObjectPtr<AJGDPhysicsProfileVolume>& Item)
			{
				return !Item.IsValid() || Item.Get() == Volume;
			});
		if (RemovedCount > 0)
		{
			ApplyResolvedProfile(*Character, Registration);
		}
	}
}

UJGDCharacterPhysicsProfile* UJGDPhysicsWorldSubsystem::ResolveProfile(
	const FJGDCharacterPhysicsRegistration& Registration) const
{
	UJGDCharacterPhysicsProfile* ResolvedProfile = nullptr;
	int32 BestPriority = TNumericLimits<int32>::Lowest();

	// Later entries win ties, which makes overlapping equal-priority volumes deterministic.
	for (const TWeakObjectPtr<AJGDPhysicsProfileVolume>& WeakVolume : Registration.ActiveVolumes)
	{
		const AJGDPhysicsProfileVolume* Volume = WeakVolume.Get();
		if (Volume && Volume->CharacterProfile && Volume->Priority >= BestPriority)
		{
			BestPriority = Volume->Priority;
			ResolvedProfile = Volume->CharacterProfile;
		}
	}

	if (ResolvedProfile)
	{
		return ResolvedProfile;
	}

	if (UJGDCharacterPhysicsProfile* BaseProfile = Registration.BaseProfile.Get())
	{
		return BaseProfile;
	}

	return CurrentWorldProfile ? CurrentWorldProfile->DefaultCharacterProfile.Get() : nullptr;
}

void UJGDPhysicsWorldSubsystem::ApplyResolvedProfile(
	ACharacter& Character,
	FJGDCharacterPhysicsRegistration& Registration)
{
	UCharacterMovementComponent* MovementComponent = Character.GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	UJGDCharacterPhysicsProfile* PreviousProfile = Registration.AppliedProfile.Get();
	UJGDCharacterPhysicsProfile* NewProfile = ResolveProfile(Registration);

	if (NewProfile)
	{
		NewProfile->Settings.ApplyTo(*MovementComponent);
	}
	else
	{
		FJGDCharacterPhysicsSettings().ApplyTo(*MovementComponent);
	}

	const bool bProfileChanged = !Registration.bHasAppliedProfile || PreviousProfile != NewProfile;
	Registration.AppliedProfile = NewProfile;
	Registration.bHasAppliedProfile = true;

	if (bProfileChanged)
	{
		OnCharacterPhysicsProfileChanged.Broadcast(&Character, PreviousProfile, NewProfile);
	}
}

void UJGDPhysicsWorldSubsystem::RefreshAllCharacters()
{
	for (auto It = RegisteredCharacters.CreateIterator(); It; ++It)
	{
		ACharacter* Character = It.Key().Get();
		if (!IsValid(Character))
		{
			It.RemoveCurrent();
			continue;
		}

		ApplyResolvedProfile(*Character, It.Value());
	}
}
