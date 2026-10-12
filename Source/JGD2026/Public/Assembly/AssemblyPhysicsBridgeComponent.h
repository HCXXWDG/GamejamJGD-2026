#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Assembly/AssemblyPhysicsMessages.h"
#include "MessageBus/JGDMessageBusTypes.h"
#include "AssemblyPhysicsBridgeComponent.generated.h"

class UJGDMessageSubsystem;

/** Typed GameplayTags adapter. Owns no movement, physics, input assets or widgets. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblyPhysicsBridgeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAssemblyPhysicsBridgeComponent();
	// One active bridge per model. Initializing the same binding repeatedly is safe.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	bool InitializeBridge(UAssemblyComponent* Assembly, UPrimitiveComponent* InCoreBody, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	void ClearBridge();
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	void SetCoreBody(UPrimitiveComponent* InCoreBody);
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics")
	bool IsBridgeInitialized() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics")
	FAssemblyPhysicsContext GetPhysicsContext() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics")
	bool GetPhysicsSnapshot(FAssemblyPhysicsSnapshotMessage& OutSnapshot) const;
	// Call after registering/replacing bone pose sources. Coalesced until the next tick.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	void RequestPhysicsRebuild();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyComponent> BoundAssembly;
	UPROPERTY(Transient)
	TWeakObjectPtr<UPrimitiveComponent> CoreBody;
	UPROPERTY(Transient)
	TWeakObjectPtr<UJGDMessageSubsystem> MessageBus;
	FGuid SourceId;
	int32 RoundGeneration = 0;
	int64 Revision = 0;
	bool bInitialized = false;
	bool bClearing = false;
	bool bRebuildPending = false;
	bool bCoreWasHeld = false;
	FJGDMessageListenerHandle JointBrokenListener;
	// Retained only while driving; ClearBridge must release every emitted held intent.
	UPROPERTY(Transient)
	TMap<FGuid, FAssemblyPhysicsMuscleDriveMessage> ActiveMuscleRequests;
	UPROPERTY(Transient)
	TArray<FAssemblyPhysicsJointBrokenMessage> PendingJointConfirmations;
	TSet<FGuid> OutstandingJointBreaks;

	FAssemblyPhysicsBonePose MakeBonePose(FGuid BoneId) const;
	bool IsCurrentContext(const FAssemblyPhysicsContext& Context) const;
	void BroadcastSnapshot(FGameplayTag Channel, bool bClearState = false);
	void BroadcastMuscleRequest(const FAssemblyPhysicsMuscleDriveMessage& Request);
	void BroadcastCoreRequest(bool bHeld);
	void UnbindModel();
	UFUNCTION()
	void HandleAssemblyChanged();
	UFUNCTION()
	void HandleAssemblyReset();
	UFUNCTION()
	void HandlePhaseChanged(EAssemblyPhase Phase);
	UFUNCTION()
	void HandleMuscleDriveRequested(FGuid MuscleId, bool bContracting);
	UFUNCTION()
	void HandleCoreRotationRequested(bool bHeld);
	UFUNCTION()
	void HandleJointBreakRequested(FGuid JointId);
	UFUNCTION()
	void HandlePhysicsJointBroken(FGameplayTag Channel, const FInstancedStruct& Message);
};
