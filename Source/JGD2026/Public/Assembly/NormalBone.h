#pragma once

#include "CoreMinimal.h"
#include "Assembly/AssemblyTypes.h"
#include "GameFramework/Actor.h"
#include "NormalBone.generated.h"

class UAssemblyComponent;
class UBoxComponent;
class UPaperSpriteComponent;

/** One bone's view and body. AssemblyComponent owns inventory and placement rules. */
UCLASS(BlueprintType, Blueprintable)
class JGD2026_API ANormalBone : public AActor
{
	GENERATED_BODY()

public:
	ANormalBone();
	virtual void OnConstruction(const FTransform& Transform) override;

	// Editor preview defaults; an installed instance uses the assembly definition instead.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bone|Preview")
	FNormalBoneParameters PreviewParameters;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bone|Body", meta = (ClampMin = "0.01"))
	float BodyHalfThickness = 4.0f;
	// Artwork is authored for Medium thickness. Thin/Thick apply 0.5/1.5 to its visual width.
	// This changes only the sprite; physical dimensions and bone-local anchors retain their definitions.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bone|Visual")
	bool bUseArtworkProportions = false;
	// Ordinary bones may cover the seam at a joint using an artwork rim. Uniform XZ only:
	// the physical body, attachment anchors and the model's no-overlap rules stay unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bone|Visual", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float ArtworkOverlapScale = 1.0f;
	// Local +Y is toward the side-view camera. Only the Sprite moves; the body and anchors stay put.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone|Visual")
	float VisualDepthOffset = 0.0f;
	// Applies a rigid-part definition without installing it or consuming stock.
	UFUNCTION(BlueprintCallable, Category = "Bone|Assembly")
	virtual bool ApplyRigidPartDefinition(const FAssemblyBoneDefinition& Definition, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Bone|Assembly")
	FAssemblyBoneDefinition GetRigidPartDefinition() const;

	// Call only after TryInstallBone succeeds. This never consumes inventory or creates a model instance.
	UFUNCTION(BlueprintCallable, Category = "Bone|Assembly")
	bool InitializeBone(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason);
	// Does not teleport a simulating body, update Playing poses or override a different pose source.
	UFUNCTION(BlueprintCallable, Category = "Bone|Assembly")
	bool RefreshAssemblyPose(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Bone|Assembly")
	void ClearAssemblyBinding();
	UFUNCTION(BlueprintPure, Category = "Bone|Assembly")
	bool IsBoneInitialized() const { return BoundAssembly.IsValid() && BoneData.InstanceId.IsValid(); }
	UFUNCTION(BlueprintPure, Category = "Bone|Assembly")
	FGuid GetInstanceId() const { return BoneData.InstanceId; }
	UFUNCTION(BlueprintPure, Category = "Bone|Assembly")
	FAssemblyBoneInstance GetBoneData() const { return BoneData; }
	UFUNCTION(BlueprintPure, Category = "Bone|Assembly")
	UAssemblyComponent* GetAssemblyComponent() const;
	UFUNCTION(BlueprintPure, Category = "Bone|Body")
	UBoxComponent* GetBoneBody() const { return BoneBody; }
	UFUNCTION(BlueprintPure, Category = "Bone|Visual")
	UPaperSpriteComponent* GetBoneSprite() const { return BoneSprite; }
	UFUNCTION(BlueprintPure, Category = "Bone|Parameters")
	FNormalBoneParameters GetBoneParameters() const;
	UFUNCTION(BlueprintPure, Category = "Bone|Parameters")
	FNormalBonePhysicalStats GetPhysicalStats() const;
	UFUNCTION(BlueprintPure, Category = "Bone|Parameters")
	FText GetBoneDisplayName() const;
	UFUNCTION(BlueprintPure, Category = "Bone|Parameters")
	FIntPoint GetRequiredGridSize(float GridCellSize = 64.0f) const;
	// Component shelves can read a definition without spawning a preview Actor.
	UFUNCTION(BlueprintPure, Category = "Bone|Parameters")
	static bool CalculateBoneStats(const FNormalBoneParameters& Parameters, FNormalBonePhysicalStats& OutStats, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Bone|Parameters")
	static FText DescribeBoneParameters(const FNormalBoneParameters& Parameters);
	UFUNCTION(BlueprintPure, Category = "Bone|Parameters")
	static TArray<FIntPoint> MakeRectangularBoneFootprint(const FNormalBoneParameters& Parameters, float GridCellSize, FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Bone|Joints")
	FVector2D GetEndLocalPosition(EBoneEnd End) const;
	UFUNCTION(BlueprintPure, Category = "Bone|Joints")
	bool GetEndWorldPosition(EBoneEnd End, FVector& OutWorldPosition) const;

	// Continuous bone-local coordinates: X = local X, Y = local Z, measured in Unreal units.
	// Free placement uses the physical rectangle; legacy Grid placement also filters the footprint.
	UFUNCTION(BlueprintPure, Category = "Bone|Muscle")
	bool TryMakeMuscleEndpoint(FVector2D LocalPoint, FAssemblyMuscleEndpoint& OutEndpoint) const;
	UFUNCTION(BlueprintPure, Category = "Bone|Muscle")
	bool TryGetMuscleEndpoint(FVector WorldPoint, FAssemblyMuscleEndpoint& OutEndpoint) const;
	UFUNCTION(BlueprintPure, Category = "Bone|Muscle")
	bool GetConnectionWorldPosition(FVector2D LocalPoint, FVector& OutWorldPosition) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bone|Body")
	TObjectPtr<UBoxComponent> BoneBody;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bone|Visual")
	TObjectPtr<UPaperSpriteComponent> BoneSprite;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyComponent> BoundAssembly;
	UPROPERTY(Transient)
	FAssemblyBoneInstance BoneData;
	UPROPERTY(Transient)
	TArray<FIntPoint> Footprint;
	UPROPERTY(Transient)
	FNormalBoneParameters InstalledParameters;
	UPROPERTY(Transient)
	FAssemblyBoneDefinition AppliedDefinition;
	bool bHasAppliedDefinition = false;
	bool bApplyingModelDefinition = false;
	float CellSize = 64.0f;

	bool CanApplyPose(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason) const;
	bool ApplyModelDefinition(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason);
	bool ReadAssemblyData(UAssemblyComponent* Assembly, FGuid InstanceId, FAssemblyBoneInstance& OutBone,
		TArray<FIntPoint>& OutFootprint, FNormalBoneParameters& OutParameters, float& OutCellSize,
		FTransform& OutPose, FText& OutReason) const;
	bool HasValidGeometry(const FNormalBonePhysicalStats& Stats) const;
	void UpdateGeometry(const FNormalBonePhysicalStats& Stats);
	void RefreshVisualGeometry(const FNormalBonePhysicalStats& Stats);
	UFUNCTION()
	void HandleAssemblyChanged();
	UFUNCTION()
	void HandleAssemblyReset();
};
