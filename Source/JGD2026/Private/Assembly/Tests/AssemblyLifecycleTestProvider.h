#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Assembly/AssemblyPhysicsLifecycle.h"
#include "AssemblyLifecycleTestProvider.generated.h"

class UAssemblyComponent;
class UAssemblyPhysicsBridgeComponent;
class UAssemblySessionComponent;
class ACharacter;

/** Synchronous executor double; it never enables physics or creates constraints. */
UCLASS(Transient, NotBlueprintable)
class UAssemblyLifecycleTestProvider : public UActorComponent, public IAssemblyPhysicsLifecycle
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TObjectPtr<UAssemblyComponent> Model;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblyPhysicsBridgeComponent> Bridge;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblySessionComponent> Session;
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> TemporaryBody;
	UPROPERTY(Transient)
	TArray<FAssemblyPhysicsSnapshotMessage> Snapshots;
	TArray<FString> Calls;
	bool bPrepareSucceeds = true;
	bool bPlayingApplySucceeds = true;
	bool bAssemblyApplySucceeds = true;
	bool bAllocateResourcesDuringAssemblyApply = false;
	bool bExecutorResourcesActive = false;
	bool bUseDefaultPlayingApply = false;
	bool bClearDuringPlayingApply = false;
	bool bShutdownSessionDuringPlayingApply = false;
	bool bDestroyDuringPlayingApply = false;
	bool bRestartDuringPlayingApply = false;
	bool bRequestInputDuringPlayingApply = false;
	bool bCoreInputAcceptedDuringPlayingApply = false;
	bool bMuscleInputAcceptedDuringPlayingApply = false;
	bool bRestoreMovementAfterPlayingNotification = false;
	bool bRestoreCapsuleAfterPlayingNotification = false;
	bool bRestoreMovementAfterSessionNotification = false;
	bool bRestoreCapsuleAfterSessionNotification = false;
	bool bSessionObserverTriggered = false;
	bool bCapsuleDisabledDuringPlayingApply = false;
	bool bCapsuleDisabledDuringFirstPhase = false;
	int32 PlayingApplyCount = 0;
	int32 PlayingNotificationCount = 0;
	int32 AssemblyCompensationCount = 0;
	bool bStopSucceeds = true;
	bool bRestartDuringPrepare = false;
	bool bClearDuringPrepare = false;
	bool bRestartDuringStop = false;
	bool bReplacePoseDuringPrepare = false;
	bool bReplaceCoreDuringPrepare = false;
	bool bRequestInputDuringStop = false;
	bool bCoreInputAcceptedDuringStop = false;
	bool bMuscleInputAcceptedDuringStop = false;
	bool bShutdownSessionDuringPrepare = false;
	bool bMovementFrozenAfterSessionShutdown = false;
	bool bMovementFrozenDuringShutdown = false;
	bool bMovementFrozenDuringPhase = false;
	bool bAnyPhaseBeforeMovementSuspension = false;
	bool bCapsuleDisabledDuringShutdown = false;
	bool bRefreshOnMovementRestore = false;
	bool bRefreshAcceptedOnMovementRestore = false;
	int32 MovementRestoreRefreshCount = 0;
	bool bNestedRestartSucceeded = false;
	bool bCoreAliveDuringStop = false;
	bool bAllBodiesAliveDuringStop = false;
	bool bAllPosesStillRegisteredDuringStop = false;
	bool bAllBodiesAliveDuringShutdown = false;
	int32 PrepareCount = 0;
	int32 AbortCount = 0;
	int32 StopCount = 0;
	int32 PhaseCount = 0;
	int32 ShutdownCount = 0;

	virtual bool PrepareForPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) override;
	virtual void AbortPreparedPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot) override;
	virtual bool StopBeforeReset_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) override;
	virtual bool ApplyAssemblyPhase_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) override;
	virtual void ShutdownPhysics_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot) override;
	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode);
	UFUNCTION()
	void HandlePhaseChanged(EAssemblyPhase Phase);
	UFUNCTION()
	void HandleSessionChanged();
	UFUNCTION()
	void HandleSessionMessage(FText Message);
	void DisruptSessionProtection();
};
