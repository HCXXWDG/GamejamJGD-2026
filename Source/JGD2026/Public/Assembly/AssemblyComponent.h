#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Assembly/AssemblyTypes.h"
#include "AssemblyComponent.generated.h"

class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAssemblyChangedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAssemblyPhaseEvent, EAssemblyPhase, Phase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAssemblyMuscleDriveEvent, FGuid, MuscleId, bool, bContracting);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAssemblyCoreRotationEvent, bool, bHeld);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAssemblyMuscleDriveNativeEvent, FGuid, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FAssemblyCoreRotationNativeEvent, bool);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAssemblyJointBreakEvent, FGuid, JointId);
DECLARE_DELEGATE_RetVal_OneParam(bool, FAssemblyLifecycleCheck, FText&);

/** Authoritative assembly model. Does not spawn actors, create UI, bind input or simulate physics. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAssemblyComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	EAssemblyPlacementMode DefaultPlacementMode = EAssemblyPlacementMode::Free;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	FAssemblyFreeSettings DefaultFreeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	FAssemblyGridSettings DefaultGrid;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	TArray<FAssemblyBoneDefinition> DefaultBones;
	// Appended only by the first default Free entry. Explicit ConfigureFreeAssembly receives a complete list.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	TArray<FAssemblyBoneDefinition> DefaultJointParts;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	TArray<FAssemblyMuscleDefinition> DefaultMuscles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	bool bAutoEnterAssembly = true;

	// UI/render/physics read the current model after a successful edit or key selection.
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyChangedEvent OnAssemblyChanged;
	// Cancel dragging, clear old view/physics instances and restore the spawn pose.
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyChangedEvent OnAssemblyReset;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyPhaseEvent OnPhaseChanged;
	// true requests contraction; false requests relaxation. The physics adapter applies it.
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Physics")
	FAssemblyMuscleDriveEvent OnMuscleDriveRequested;
	// The physics adapter owns angular velocity, torque and constraint propagation.
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Physics")
	FAssemblyCoreRotationEvent OnCoreRotationRequested;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Physics")
	FAssemblyJointBreakEvent OnJointBreakRequested;

	FAssemblyMuscleDriveNativeEvent OnMuscleDriveRequestedNative;
	FAssemblyCoreRotationNativeEvent OnCoreRotationRequestedNative;

	// One lifecycle owner protects every public start/reset entry, including direct Blueprint calls.
	// Physics remains outside the model; pose-source registration is allowed during preparation.
	bool RegisterLifecycleCallbacks(UObject* Owner, FAssemblyLifecycleCheck BeforePlaying,
		FAssemblyLifecycleCheck BeforeReset, FSimpleDelegate AbortPlaying, FSimpleDelegate BeforeEndPlay, FText& OutReason);
	// Optional transactional extension. Commit activates once; Verify runs after phase listeners;
	// Finalize releases the abort token only after every synchronous confirmation succeeded.
	bool RegisterPlayingCommitCallbacks(UObject* Owner, FAssemblyLifecycleCheck Commit,
		FAssemblyLifecycleCheck Verify, FAssemblyLifecycleCheck Finalize, FText& OutReason);
	// Bridge recovery after a failed resume. Keeps this round and inventory; it is not a restart.
	bool RecoverAssemblyAfterPhysicsFailure(FText& OutReason);
	bool IsRecoveringFromPhysicsFailure() const { return bRecoveringPhysicsFailure; }
	void UnregisterLifecycleCallbacksIfMatches(const UObject* Owner);
	bool IsEndingPlay() const { return bEndingPlay; }

	// Validate before replacing the round. Failure leaves the previous round intact.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Flow")
	bool ConfigureAssembly(const FAssemblyGridSettings& Grid, const TArray<FAssemblyBoneDefinition>& Bones,
		const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Flow")
	bool ConfigureFreeAssembly(const FAssemblyFreeSettings& Settings, const TArray<FAssemblyBoneDefinition>& Bones,
		const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Flow")
	EAssemblyPlacementMode GetPlacementMode() const { return bInitialized ? RoundPlacementMode : DefaultPlacementMode; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	FAssemblyFreeSettings GetFreeSettings() const { return bInitialized ? RoundFreeSettings : DefaultFreeSettings; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	TArray<FAssemblyJointInstance> GetJoints() const { return InstalledJoints; }
	UFUNCTION(BlueprintCallable, Category = "Assembly|Flow")
	bool EnterAssembly(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Flow")
	bool RestartAssembly(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Flow")
	bool TryStartPlaying(FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Flow")
	EAssemblyPhase GetPhase() const { return Phase; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Flow")
	bool IsInitialized() const { return bInitialized; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Flow")
	bool CanControlMuscles() const { return bInitialized && Phase == EAssemblyPhase::Playing; }

	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	FAssemblyGridSettings GetGridSettings() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	TArray<FAssemblyBoneDefinition> GetBoneDefinitions() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	bool GetBoneDefinition(FName TypeId, FAssemblyBoneDefinition& OutDefinition) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|JointParts")
	TArray<FAssemblyBoneDefinition> GetJointPartDefinitions() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|JointParts")
	TArray<FAssemblyBoneInstance> GetJointParts() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|JointParts")
	bool IsJointPart(FGuid InstanceId) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	TArray<FAssemblyMuscleDefinition> GetMuscleDefinitions() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	TArray<FAssemblyBoneInstance> GetBones() const { return InstalledBones; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	TArray<FAssemblyMuscleInstance> GetMuscles() const { return InstalledMuscles; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	int32 GetRemainingQuantity(FName TypeId) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	bool GetBone(FGuid InstanceId, FAssemblyBoneInstance& OutBone) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	bool GetMuscle(FGuid InstanceId, FAssemblyMuscleInstance& OutMuscle) const;

	UFUNCTION(BlueprintCallable, Category = "Assembly|Input")
	bool SelectMuscleKey(EAssemblyMuscleKey Key, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Input")
	EAssemblyMuscleKey GetSelectedMuscleKey() const { return SelectedKey; }

	// Preview and commit use the same rules; IgnoreId is for moving an existing bone of this type.
	UFUNCTION(BlueprintPure, Category = "Assembly|Bones")
	FAssemblyPlacementResult ValidateBonePlacement(FName TypeId, FIntPoint AnchorCell, int32 QuarterTurns, FGuid IgnoreId) const;
	UFUNCTION(BlueprintCallable, Category = "Assembly|Bones")
	bool TryInstallBone(FName TypeId, FIntPoint AnchorCell, int32 QuarterTurns, FGuid& OutInstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Bones")
	bool TryMoveBone(FGuid InstanceId, FIntPoint AnchorCell, int32 QuarterTurns, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Bones")
	FAssemblyPlacementResult ValidateFreeBonePlacement(FName TypeId, FVector2D LocalPosition, float RotationDegrees, FGuid IgnoreId) const;
	// Resolve a raw pointer pose for a drag preview. Commit the returned exact pose; never feed it back as the pointer.
	// PreviousSnap retains the contact target until the larger release distance is exceeded.
	UFUNCTION(BlueprintPure, Category = "Assembly|Drag")
	FAssemblyPlacementResult ResolveFreeBoneDragPlacement(FName TypeId, FVector2D RawPosition, float RotationDegrees,
		FGuid IgnoreId, const FAssemblySurfaceSnapState& PreviousSnap) const;
	UFUNCTION(BlueprintCallable, Category = "Assembly|Bones")
	bool TryInstallFreeBone(FName TypeId, FVector2D LocalPosition, float RotationDegrees, FGuid& OutInstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Bones")
	bool TryMoveFreeBone(FGuid InstanceId, FVector2D LocalPosition, float RotationDegrees, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|JointParts")
	FAssemblyPlacementResult ValidateJointPartPlacement(FName TypeId, FVector2D LocalPosition, float RotationDegrees, FGuid IgnoreId) const;
	UFUNCTION(BlueprintCallable, Category = "Assembly|JointParts")
	bool TryInstallJointPart(FName TypeId, FVector2D LocalPosition, float RotationDegrees, FGuid& OutInstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|JointParts")
	bool TryMoveJointPart(FGuid InstanceId, FVector2D LocalPosition, float RotationDegrees, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|JointParts")
	bool RemoveJointPart(FGuid InstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Joints")
	bool TrySetJointKind(FGuid JointId, EAssemblyJointKind Kind, FText& OutReason);
	// Purple Round pieces permit only hinge variants; yellow pieces permit only fixed variants.
	// Legacy edges without independent pieces retain the four original choices.
	UFUNCTION(BlueprintPure, Category = "Assembly|Joints")
	bool IsJointKindAllowed(FGuid JointId, EAssemblyJointKind Kind) const;
	UFUNCTION(BlueprintCallable, Category = "Assembly|Joints")
	bool TrySetJointLimits(FGuid JointId, float MinAngleDegrees, float MaxAngleDegrees, FText& OutReason);
	// Sends intent only; the physics module confirms the break via the bridge.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	bool RequestJointBreak(FGuid JointId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	bool ConfirmJointBroken(FGuid JointId, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Joints")
	bool IsStructureConnected() const;

	UFUNCTION(BlueprintPure, Category = "Assembly|Muscles")
	FAssemblyValidationResult ValidateMusclePlacement(FName TypeId, const FAssemblyMuscleEndpoint& EndpointA,
		const FAssemblyMuscleEndpoint& EndpointB, EAssemblyMuscleKey Key, FGuid IgnoreId) const;
	// Installation uses the explicitly selected key; bones never require a selected key.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Muscles")
	bool TryInstallMuscle(FName TypeId, const FAssemblyMuscleEndpoint& EndpointA,
		const FAssemblyMuscleEndpoint& EndpointB, FGuid& OutInstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Muscles")
	bool TryMoveMuscle(FGuid InstanceId, const FAssemblyMuscleEndpoint& EndpointA,
		const FAssemblyMuscleEndpoint& EndpointB, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Muscles")
	bool TrySetMuscleKey(FGuid InstanceId, EAssemblyMuscleKey Key, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Data")
	bool TryRemoveInstance(FGuid InstanceId, FText& OutReason);
	// Also checks stock conservation, instance identities and every muscle endpoint.
	UFUNCTION(BlueprintPure, Category = "Assembly|Data")
	FAssemblyValidationResult ValidateAssembly() const;

	// Started -> true; Completed AND Canceled -> false. Identical requests are coalesced.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Input")
	bool SetMuscleInput(EAssemblyMuscleKey Key, bool bPressed);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Input")
	bool SetCoreRotationInput(bool bHeld);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Input")
	void ClearInputState();
	UFUNCTION(BlueprintPure, Category = "Assembly|Input")
	bool IsCoreRotationHeld() const { return bCoreRotationHeld; }

	// Set to the core's real body/scene component; otherwise use the owning actor's transform.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Coordinates")
	void SetGridFrame(USceneComponent* Frame);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Coordinates")
	bool TrySetAssemblyFrame(USceneComponent* Frame, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	FTransform GetGridFrameTransform() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	FVector CellToLocal(FIntPoint Cell) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	FIntPoint LocalToCell(FVector LocalPosition) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	FVector CellToWorld(FIntPoint Cell) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	FIntPoint WorldToCell(FVector WorldPosition) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	bool IsCellInBounds(FIntPoint Cell) const;
	// Feed PlayerController deprojection's world ray; no UMG/DPI conversion occurs here.
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	bool RayToGrid(FVector RayOrigin, FVector RayDirection, FVector& OutLocalPosition, FIntPoint& OutCell) const;
	// Continuous coordinates, no grid bounds or quantization. Same local XZ game plane.
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	bool RayToAssemblyPlane(FVector RayOrigin, FVector RayDirection, FVector2D& OutLocalPosition) const;

	// Endpoints follow the actual physical pose; no forced attachment or transformation.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	bool RegisterBonePoseSource(FGuid BoneId, USceneComponent* PoseSource, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics")
	USceneComponent* GetBonePoseSource(FGuid BoneId) const;
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics")
	void UnregisterBonePoseSource(FGuid BoneId);
	// A view may only clear its own registration, never a replacement made by the physics adapter.
	bool UnregisterBonePoseSourceIfMatches(FGuid BoneId, const USceneComponent* ExpectedSource);
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	bool GetBoneWorldTransform(FGuid BoneId, FTransform& OutTransform) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	bool GetEndpointWorldPosition(const FAssemblyMuscleEndpoint& Endpoint, FVector& OutWorldPosition) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Coordinates")
	bool FindBoneAtWorldPosition(FVector WorldPosition, FAssemblyMuscleEndpoint& OutEndpoint) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class UAssemblyInteractionComponent;
	UPROPERTY(Transient)
	FAssemblyFreeSettings RoundFreeSettings;
	EAssemblyPlacementMode RoundPlacementMode = EAssemblyPlacementMode::Free;
	UPROPERTY(Transient)
	TArray<FAssemblyJointInstance> InstalledJoints;
	UPROPERTY(Transient)
	FAssemblyGridSettings RoundGrid;
	UPROPERTY(Transient)
	TArray<FAssemblyBoneDefinition> RoundBoneDefinitions;
	UPROPERTY(Transient)
	TArray<FAssemblyMuscleDefinition> RoundMuscleDefinitions;
	UPROPERTY(Transient)
	TArray<FAssemblyBoneInstance> InstalledBones;
	UPROPERTY(Transient)
	TArray<FAssemblyMuscleInstance> InstalledMuscles;
	UPROPERTY(Transient)
	TMap<FName, int32> RemainingQuantities;
	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> GridFrame;
	UPROPERTY(Transient)
	TMap<FGuid, TWeakObjectPtr<USceneComponent>> BonePoseSources;

	EAssemblyPhase Phase = EAssemblyPhase::Assembly;
	EAssemblyMuscleKey SelectedKey = EAssemblyMuscleKey::None;
	bool bInitialized = false;
	bool bCoreRotationHeld = false;
	bool bNotifying = false;
	bool bEndingPlay = false;
	bool bInLifecycleCallback = false;
	bool bRecoveringPhysicsFailure = false;
	TWeakObjectPtr<UObject> LifecycleOwner;
	FAssemblyLifecycleCheck BeforePlayingCallback;
	FAssemblyLifecycleCheck BeforeResetCallback;
	FAssemblyLifecycleCheck CommitPlayingCallback;
	FAssemblyLifecycleCheck VerifyPlayingCallback;
	FAssemblyLifecycleCheck FinalizePlayingCallback;
	FSimpleDelegate AbortPlayingCallback;
	FSimpleDelegate BeforeEndPlayCallback;

	bool CanEdit(FText& OutReason) const;
	bool CanEditFreeBone(FName TypeId, FGuid IgnoreId, FText& OutReason) const;
	// Read-only snapshot in the current assembly frame. Registered bodies remain authoritative.
	bool BuildEffectiveFreeBoneSnapshot(TArray<FAssemblyBoneInstance>& OutBones, FText& OutReason) const;
	FAssemblyValidationResult ValidateAssemblyState(const TArray<FAssemblyBoneInstance>& Bones,
		const TArray<FAssemblyJointInstance>& Joints) const;
	bool CanMutate(FText& OutReason) const;
	bool RunLifecycleCheck(bool bBeforePlaying, FText& OutReason);
	// Actual body bounds in both modes, plus occupied footprint cells in Grid mode.
	bool IsPointOnBoneDefinition(const FAssemblyBoneDefinition& Definition, FVector2D LocalPoint) const;
	bool ValidateMuscleEndpoints(const FAssemblyMuscleEndpoint& A, const FAssemblyMuscleEndpoint& B,
		EAssemblyMuscleKey Key, FText& OutReason) const;
	const FAssemblyBoneDefinition* FindBoneDefinition(FName TypeId) const;
	const FAssemblyMuscleDefinition* FindMuscleDefinition(FName TypeId) const;
	const FAssemblyBoneInstance* FindBone(FGuid Id) const;
	const FAssemblyMuscleInstance* FindMuscle(FGuid Id) const;
	void ResetRound();
	void NotifyChanged();
	void NotifyPhase();
	void RequestMuscleDrive(FGuid Id, bool bContracting);
	void RequestCoreRotation(bool bHeld);
	void RebuildFreeJoints(const TArray<FAssemblyBoneInstance>* EffectiveBones = nullptr);
	bool IsFreeConnected(const TArray<FAssemblyBoneInstance>& Bones, const TArray<FAssemblyMuscleInstance>& Muscles,
		const TArray<FAssemblyJointInstance>& Joints) const;
	TSet<FGuid> GetFreeCoreReachable(const TArray<FAssemblyBoneInstance>& Bones, const TArray<FAssemblyMuscleInstance>& Muscles,
		const TArray<FAssemblyJointInstance>& Joints) const;
	bool PreservesFreeCoreConnections(const TArray<FAssemblyBoneInstance>& Bones, const TArray<FAssemblyMuscleInstance>& Muscles,
		const TArray<FAssemblyJointInstance>& Joints, FGuid RemovedBone = FGuid()) const;
	// View creation rollback bypasses user removal policy; callable only by the presenter.
	void RollbackNewInstance(FGuid InstanceId);
};
