// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/JGDPhysicsProfileVolume.h"

#include "GameFramework/Character.h"
#include "Physics/JGDPhysicsWorldSubsystem.h"

AJGDPhysicsProfileVolume::AJGDPhysicsProfileVolume()
{
	bPhysicsOnContact = false;
}

void AJGDPhysicsProfileVolume::ActorEnteredVolume(AActor* Other)
{
	Super::ActorEnteredVolume(Other);

	if (ACharacter* Character = Cast<ACharacter>(Other))
	{
		if (UWorld* World = GetWorld())
		{
			if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
			{
				Subsystem->NotifyCharacterEnteredVolume(Character, this);
			}
		}
	}
	if (UWorld* World = GetWorld())
	{
		if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
		{
			Subsystem->NotifyRigidBodiesEnteredVolume(Other, this);
		}
	}
}

void AJGDPhysicsProfileVolume::ActorLeavingVolume(AActor* Other)
{
	if (ACharacter* Character = Cast<ACharacter>(Other))
	{
		if (UWorld* World = GetWorld())
		{
			if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
			{
				Subsystem->NotifyCharacterLeftVolume(Character, this);
			}
		}
	}
	if (UWorld* World = GetWorld())
	{
		if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
		{
			Subsystem->NotifyRigidBodiesLeftVolume(Other, this);
		}
	}

	Super::ActorLeavingVolume(Other);
}

void AJGDPhysicsProfileVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UJGDPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJGDPhysicsWorldSubsystem>())
		{
			Subsystem->NotifyVolumeRemoved(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
