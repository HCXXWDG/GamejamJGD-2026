#pragma once

#include "CoreMinimal.h"
#include "Assembly/AssemblyTypes.h"
#include "GameFramework/Actor.h"
#include "AssemblyMuscle.generated.h"

class UAssemblyComponent;
class UMaterialInterface;
class UPaperSprite;
class UPaperSpriteComponent;
class USceneComponent;
class USplineComponent;

/** A muscle's curve view. Input changes feedback colour; physics owns all contraction and forces. */
UCLASS(BlueprintType, Blueprintable)
class JGD2026_API AAssemblyMuscle : public AActor
{
	GENERATED_BODY()

public:
	AAssemblyMuscle();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	// Replaceable presentation assets. No grid, inventory or drive rules live here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual")
	TObjectPtr<UPaperSprite> CurveSprite;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual")
	TObjectPtr<UMaterialInterface> CurveMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual", meta = (ClampMin = "4", ClampMax = "64"))
	int32 CurveSegments = 24;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual", meta = (ClampMin = "0.01"))
	float LineWidth = 5.0f;
	// Signed midpoint deflection / endpoint distance. Purely visual, never a physical length request.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual", meta = (ClampMin = "-1", ClampMax = "1"))
	float BendRatio = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual")
	FLinearColor RelaxedColor = FLinearColor(0.9f, 0.22f, 0.35f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual")
	FLinearColor ContractingColor = FLinearColor(1.0f, 0.8f, 0.05f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Preview")
	FLinearColor ValidPreviewColor = FLinearColor(0.1f, 0.9f, 0.45f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Preview")
	FLinearColor InvalidPreviewColor = FLinearColor(1.0f, 0.1f, 0.1f, 1.0f);
	// Drawing priority only; anchors are still at their exact world positions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muscle|Visual")
	int32 TranslucencySortPriority = 10;

	// Call after model installation succeeds. This never consumes inventory or installs a muscle.
	UFUNCTION(BlueprintCallable, Category = "Muscle|Assembly")
	bool InitializeMuscle(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Muscle|Assembly")
	void ClearAssemblyBinding();
	UFUNCTION(BlueprintPure, Category = "Muscle|Assembly")
	bool IsMuscleInitialized() const { return BoundAssembly.IsValid() && MuscleData.InstanceId.IsValid(); }
	UFUNCTION(BlueprintPure, Category = "Muscle|Assembly")
	FGuid GetInstanceId() const { return MuscleData.InstanceId; }
	UFUNCTION(BlueprintPure, Category = "Muscle|Assembly")
	FAssemblyMuscleInstance GetMuscleData() const { return MuscleData; }
	UFUNCTION(BlueprintPure, Category = "Muscle|Assembly")
	UAssemblyComponent* GetAssemblyComponent() const;
	// World endpoints belong to preview only. Installed endpoints always come from their bone poses.
	UFUNCTION(BlueprintCallable, Category = "Muscle|Preview")
	bool SetPreviewEndpoints(FVector WorldA, FVector WorldB, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Muscle|Preview")
	void SetPreviewValid(bool bValid);
	UFUNCTION(BlueprintCallable, Category = "Muscle|Visual")
	void RefreshCurve();
	UFUNCTION(BlueprintPure, Category = "Muscle|Visual")
	USplineComponent* GetCurveSpline() const { return CurveSpline; }
	UFUNCTION(BlueprintPure, Category = "Muscle|Visual")
	bool GetCurveEndpoints(FVector& OutWorldA, FVector& OutWorldB) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Muscle|Visual")
	TObjectPtr<USceneComponent> CurveRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Muscle|Visual")
	TObjectPtr<USplineComponent> CurveSpline;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyComponent> BoundAssembly;
	UPROPERTY(Transient)
	FAssemblyMuscleInstance MuscleData;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPaperSpriteComponent>> SegmentSprites;
	FVector PreviewWorldA = FVector(-64.0, 0.0, 0.0);
	FVector PreviewWorldB = FVector(64.0, 0.0, 0.0);
	bool bHasWorldPreview = false;
	bool bPreviewValid = true;

	bool HasUsablePresentation() const;
	void EnsureSegmentSprites();
	void HideCurve();
	FLinearColor GetFeedbackColor() const;
	UFUNCTION()
	void HandleAssemblyChanged();
	UFUNCTION()
	void HandleAssemblyReset();
	UFUNCTION()
	void HandleMuscleDriveRequested(FGuid MuscleId, bool bContracting);
};
