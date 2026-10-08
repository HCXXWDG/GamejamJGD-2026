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

/** Authoritative assembly model. Does not spawn actors, create UI, bind input or simulate physics. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAssemblyComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	FAssemblyGridSettings DefaultGrid;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Config")
	TArray<FAssemblyBoneDefinition> DefaultBones;
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

	FAssemblyMuscleDriveNativeEvent OnMuscleDriveRequestedNative;
	FAssemblyCoreRotationNativeEvent OnCoreRotationRequestedNative;

	// Validate before replacing the round. Failure leaves the previous round intact.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Flow")
	bool ConfigureAssembly(const FAssemblyGridSettings& Grid, const TArray<FAssemblyBoneDefinition>& Bones,
		const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason);
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

	bool CanEdit(FText& OutReason) const;
	bool CanMutate(FText& OutReason) const;
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
};
