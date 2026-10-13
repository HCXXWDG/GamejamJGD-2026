#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Assembly/AssemblyPhysicsMessages.h"
#include "AssemblyPhysicsLifecycle.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class JGD2026_API UAssemblyPhysicsLifecycle : public UInterface
{
	GENERATED_BODY()
};

/** Synchronous lifecycle acknowledgement; drive intents continue to use GameplayTags.
 * Implement on one actor/component in the assembly's world, and bind it to the bridge.
 * Prepare may register bodies/pose sources, but must not replace or reset the model.
 * Stop succeeds only after old constraints, drives and simulation are safe to remove.
 * Abort and Shutdown must be idempotent and release partially prepared resources.
 */
class JGD2026_API IAssemblyPhysicsLifecycle
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Assembly|Physics|Lifecycle")
	bool PrepareForPlaying(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason);
	virtual bool PrepareForPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason);
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Assembly|Physics|Lifecycle")
	void AbortPreparedPlaying(const FAssemblyPhysicsSnapshotMessage& Snapshot);
	virtual void AbortPreparedPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot);
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Assembly|Physics|Lifecycle")
	bool StopBeforeReset(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason);
	virtual bool StopBeforeReset_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason);
	// Assembly: stable editing, no muscle drive; Space may rotate through physics.
	// Playing: the prepared physical structure accepts muscle and core drive intents.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Assembly|Physics|Lifecycle")
	void ApplyAssemblyPhase(const FAssemblyPhysicsSnapshotMessage& Snapshot);
	virtual void ApplyAssemblyPhase_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot);
	// Forced shutdown cannot veto actor destruction; release constraints before bodies disappear.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Assembly|Physics|Lifecycle")
	void ShutdownPhysics(const FAssemblyPhysicsSnapshotMessage& Snapshot);
	virtual void ShutdownPhysics_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot);
};
