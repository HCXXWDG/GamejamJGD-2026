#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Assembly/AssemblyPhysicsMessages.h"
#include "MessageBus/JGDMessageBusTypes.h"
#include "AssemblyPhysicsBridgeComponent.generated.h"

class UJGDMessageSubsystem;

/** Retains the original destination and payload while a drive broadcast is being fanned out. */
USTRUCT()
struct FAssemblyQueuedDriveMessage
{
	GENERATED_BODY()
	UPROPERTY(Transient)
	TObjectPtr<UJGDMessageSubsystem> Bus;
	UPROPERTY(Transient)
	FGameplayTag Channel;
	UPROPERTY(Transient)
	FInstancedStruct Payload;
};

/** Typed GameplayTags adapter. Owns no movement, physics, input assets or widgets. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblyPhysicsBridgeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAssemblyPhysicsBridgeComponent();
	// False keeps the existing visual prototype usable before a physical executor is delivered.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics|Lifecycle")
	bool bRequirePhysicsLifecycleProvider = false;
	// Null restores unique automatic discovery on the owner and its components.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Lifecycle")
	bool SetPhysicsLifecycleProvider(UObject* Provider, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics|Lifecycle")
	UObject* GetPhysicsLifecycleProvider() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics|Lifecycle")
	bool HasPhysicsLifecycleProvider() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics|Lifecycle")
	bool IsPhysicsLifecycleBusy() const { return bLifecycleBusy; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics|Lifecycle")
	bool IsPhysicsLifecycleShutdown() const
	{
		return bLifecycleShutdown && !bLifecycleBusy && !bClearing
			&& !bLifecycleShutdownPending && !bClearBridgePending;
	}
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Lifecycle")
	bool PrepareForPlaying(FText& OutReason);
	// Native model transaction: Apply once, verify after all phase listeners, then release the abort token.
	bool CommitPreparedPlaying(FText& OutReason);
	bool ValidateCommittedPlaying(FText& OutReason);
	bool FinalizePreparedPlaying(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Lifecycle")
	bool StopBeforeReset(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Lifecycle")
	void AbortPreparedPlaying();
	// Reapply Assembly mode, or prepare/reapply Playing after a session resumes.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Lifecycle")
	bool RefreshPhysicsLifecycle(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Lifecycle")
	void ShutdownPhysicsLifecycle();
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
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> PhysicsLifecycleProvider;
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> PreparedProvider;
	UPROPERTY(Transient)
	FAssemblyPhysicsSnapshotMessage PreparedSnapshot;
	// References to the editable views before an executor replaces any pose registration.
	UPROPERTY(Transient)
	FAssemblyPhysicsSnapshotMessage BeforePreparationSnapshot;
	UPROPERTY(Transient)
	FAssemblyPhysicsSnapshotMessage LastLifecycleSnapshot;
	FGuid SourceId;
	int32 RoundGeneration = 0;
	int64 Revision = 0;
	bool bInitialized = false;
	bool bClearing = false;
	bool bRebuildPending = false;
	bool bCoreWasHeld = false;
	bool bExplicitLifecycleProvider = false;
	bool bLifecycleBusy = false;
	bool bLifecycleShutdown = false;
	bool bHasPreparedPlaying = false;
	bool bPreparedPlayingApplied = false;
	bool bPlayingPhaseCommitted = false;
	bool bLifecycleShutdownPending = false;
	bool bClearBridgePending = false;
	bool bAbortPlayingPending = false;
	FJGDMessageListenerHandle JointBrokenListener;
	// Retained only while driving; ClearBridge must release every emitted held intent.
	UPROPERTY(Transient)
	TMap<FGuid, FAssemblyPhysicsMuscleDriveMessage> ActiveMuscleRequests;
	UPROPERTY(Transient)
	TArray<FAssemblyPhysicsJointBrokenMessage> PendingJointConfirmations;
	TSet<FGuid> OutstandingJointBreaks;
	// These survive ClearBridge until the outer broadcast unwinds. Nested stop must reach every
	// listener after the old press, even when that listener has already cleared the current binding.
	UPROPERTY(Transient)
	TArray<FAssemblyQueuedDriveMessage> QueuedDriveMessages;
	int32 DriveDispatchDepth = 0;
	bool bDrainingDriveMessages = false;

	FAssemblyPhysicsBonePose MakeBonePose(FGuid BoneId) const;
	bool IsCurrentContext(const FAssemblyPhysicsContext& Context) const;
	void BroadcastSnapshot(FGameplayTag Channel, bool bClearState = false);
	void BroadcastMuscleRequest(const FAssemblyPhysicsMuscleDriveMessage& Request);
	void BroadcastCoreRequest(bool bHeld);
	void DispatchDriveMessage(UJGDMessageSubsystem* Bus, FGameplayTag Channel, const FInstancedStruct& Payload);
	void DrainDriveMessages();
	void UnbindModel();
	bool ResolvePhysicsLifecycleProvider(FText& OutReason);
	bool HasSessionLifecycleOwnership() const;
	bool ValidatePhysicalBodies(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) const;
	bool IsLifecycleBindingCurrent(const FAssemblyPhysicsContext& Context, const UObject* Provider) const;
	bool ApplyPhysicsPhase(FText& OutReason);
	bool EnsureFormalProxyReady(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason);
	bool ValidateFormalProxyReady(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) const;
	void AbortPreparedState();
	void RestorePreparationPoseSources(const FAssemblyPhysicsSnapshotMessage& BeforePreparation);
	void ReleaseHeldPhysicsIntents();
	bool ReplayHeldPhysicsIntents(const FAssemblyPhysicsContext& Binding, FText& OutReason);
	void FlushDeferredLifecycleActions();
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
