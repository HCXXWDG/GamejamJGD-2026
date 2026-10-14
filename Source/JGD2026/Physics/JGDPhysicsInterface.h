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
 * Note on purity: the function is declared const. UHT automatically derives BlueprintPure
 * for a const BlueprintCallable function that has outputs, which is what makes the Blueprint
 * call node pure (no exec pins). Writing the BlueprintPure specifier explicitly is rejected
 * on interface functions ("BlueprintPure specifier is not allowed for interface functions"),
 * but the auto-derived purity is allowed. Do not drop the const qualifier - it is what keeps
 * the node pure.
 */
class JGD2026_API IJGDPhysicsInterface
{
	GENERATED_BODY()

public:
	/** Returns the momentum of this object. Takes no input. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "JGD|Physics")
	float GetMomentum() const;
};
