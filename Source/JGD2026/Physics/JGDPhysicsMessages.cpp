// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/JGDPhysicsMessages.h"

#include "Engine/Engine.h"

FGameplayTag JGDPhysicsMessageTags::ApplyWorldProfile()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.Request.ApplyWorldProfile"));
	return Tag;
}

FGameplayTag JGDPhysicsMessageTags::SetCharacterBaseProfile()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.Request.SetCharacterBaseProfile"));
	return Tag;
}

FGameplayTag JGDPhysicsMessageTags::WorldProfileChanged()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.World.ProfileChanged"));
	return Tag;
}

FGameplayTag JGDPhysicsMessageTags::CharacterProfileChanged()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.Character.ProfileChanged"));
	return Tag;
}

FGameplayTag JGDPhysicsMessageTags::RegisterRigidBody()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.Request.RegisterRigidBody"));
	return Tag;
}

FGameplayTag JGDPhysicsMessageTags::UnregisterRigidBody()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.Request.UnregisterRigidBody"));
	return Tag;
}

FGameplayTag JGDPhysicsMessageTags::SetRigidBodyBaseProfile()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.Request.SetRigidBodyBaseProfile"));
	return Tag;
}

FGameplayTag JGDPhysicsMessageTags::RigidBodyProfileChanged()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.Physics.RigidBody.ProfileChanged"));
	return Tag;
}

UWorld* UJGDPhysicsMessageLibrary::GetPhysicsMessageWorld(const UObject* WorldContextObject)
{
	return GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
}
