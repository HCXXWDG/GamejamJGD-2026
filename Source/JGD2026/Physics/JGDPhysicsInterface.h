// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "JGDPhysicsInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable, BlueprintType)
class UJGDPhysicsInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Query interface for objects that expose a momentum value.
 *
 * Both C++ and Blueprint classes can implement it:
 *
 *   C++       - inherit IJGDPhysicsInterface and override GetMomentum_Implementation():
 *
 *                   class AMyActor : public AActor, public IJGDPhysicsInterface
 *                   {
 *                       GENERATED_BODY()
 *                   public:
 *                       virtual float GetMomentum_Implementation() const override;
 *                   };
 *
 *   Blueprint - Class Settings -> Interfaces -> Add -> JGDPhysicsInterface, then
 *               implement the Get Momentum event in the Interfaces group.
 *
 * Always dispatch through IJGDPhysicsInterface::Execute_GetMomentum(Object) so that
 * native and Blueprint implementations are both handled.
 *
 * Note: UHT rejects BlueprintPure on interface functions ("BlueprintPure specifier is
 * not allowed for interface functions"), so the Blueprint call node carries exec pins.
 */
class JGD2026_API IJGDPhysicsInterface
{
	GENERATED_BODY()

public:
	/** Returns the momentum of this object. Takes no input. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "JGD|Physics")
	float GetMomentum() const;
};
