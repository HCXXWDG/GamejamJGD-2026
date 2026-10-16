#pragma once

#include "CoreMinimal.h"
#include "MessageBus/JGDMessageBusTypes.h"
#include "AssemblyPhysicsTestReceiver.generated.h"

class UAssemblyComponent;
class UAssemblyPhysicsBridgeComponent;
class UAssemblySessionComponent;

/** Reflection-only receiver needed by the project's dynamic message bus test harness. */
UCLASS(Transient, NotBlueprintable)
class UAssemblyPhysicsTestReceiver : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FGameplayTag> Channels;
	UPROPERTY()
	TArray<FInstancedStruct> Messages;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblyComponent> Model;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblyPhysicsBridgeComponent> Bridge;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblySessionComponent> Session;
	bool bClearInputOnHeldCore = false;
	bool bClearBridgeOnHeldCore = false;
	bool bClearInputOnContractingMuscle = false;
	bool bShutdownSessionOnContractingMuscle = false;
	bool bTriggered = false;
	UFUNCTION()
	void Receive(FGameplayTag Channel, const FInstancedStruct& Message);
	int32 Count(FGameplayTag Channel) const;
	const FInstancedStruct* Last(FGameplayTag Channel) const;
};
