// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/JGD2DPhysicsParticipantComponent.h"

#include "JGD2026.h"
#include "GameFramework/Character.h"
#include "Physics/JGDPhysicsProfiles.h"
#include "Physics/JGDPhysicsWorldSubsystem.h"

UJGD2DPhysicsParticipantComponent::UJGD2DPhysicsParticipantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UJGD2DPhysicsParticipantComponent::BeginPlay()
{
	Super::BeginPlay();

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		UE_LOG(LogJGDPhysics, Warning,
			TEXT("%s must be attached to an ACharacter. Owner: %s"),
			*GetName(), *GetNameSafe(GetOwner()));
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
		{
			Subsystem->RegisterCharacter(Character, BaseProfile);
		}
	}
}

void UJGD2DPhysicsParticipantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UWorld* World = GetWorld())
		{
			if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
			{
				Subsystem->UnregisterCharacter(Character);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UJGD2DPhysicsParticipantComponent::SetBaseProfile(UJGDCharacterPhysicsProfile* NewProfile)
{
	BaseProfile = NewProfile;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UWorld* World = GetWorld())
		{
			if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
			{
				Subsystem->SetCharacterBaseProfile(Character, BaseProfile);
			}
		}
	}
}

void UJGD2DPhysicsParticipantComponent::RefreshPhysicsProfile()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UWorld* World = GetWorld())
		{
			if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
			{
				Subsystem->RefreshCharacterProfile(Character);
			}
		}
	}
}
