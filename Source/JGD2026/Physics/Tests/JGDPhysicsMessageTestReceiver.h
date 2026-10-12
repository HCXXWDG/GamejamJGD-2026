// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MessageBus/JGDMessageBusTypes.h"
#include "UObject/Object.h"
#include "JGDPhysicsMessageTestReceiver.generated.h"

class ACharacter;
class UJGDCharacterPhysicsProfile;

/** Reflection adapter for automation tests of the public dynamic delegates; never saved as an asset. */
UCLASS(Transient, NotBlueprintable)
class UJGDPhysicsMessageTestReceiver : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void(FGameplayTag, const FInstancedStruct&)> OnMessage;
	TFunction<void(ACharacter*, UJGDCharacterPhysicsProfile*, UJGDCharacterPhysicsProfile*)> OnLegacy;

	UFUNCTION()
	void Receive(FGameplayTag Channel, const FInstancedStruct& Message)
	{
		if (OnMessage)
		{
			OnMessage(Channel, Message);
		}
	}

	UFUNCTION()
	void ReceiveLegacy(ACharacter* Character, UJGDCharacterPhysicsProfile* PreviousProfile, UJGDCharacterPhysicsProfile* NewProfile)
	{
		if (OnLegacy)
		{
			OnLegacy(Character, PreviousProfile, NewProfile);
		}
	}
};
