#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Assembly/AssemblyTypes.h"
#include "Assembly/AssemblyWorkbenchTypes.h"
#include "AssemblyWorkbenchComponent.generated.h"

class UAssemblyComponent;
class UAssemblyInteractionComponent;
class APlayerController;
class AAssemblyMuscle;
class ANormalBone;

/** Pointer tools and selection. The model owns rules; physics owns physical movement. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblyWorkbenchComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAssemblyWorkbenchComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Selection", meta = (ClampMin = "1"))
	float MusclePickRadiusPixels = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Selection", meta = (ClampMin = "1"))
	float JointPickRadiusPixels = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Drag", meta = (ClampMin = "0"))
	float BoneDragThresholdPixels = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Selection")
	bool bDrawSelectionFeedback = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Selection")
	FColor SelectionColor = FColor(255, 205, 45);
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyWorkbenchChanged OnWorkbenchChanged;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblySelectionChanged OnSelectionChanged;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyWorkbenchMessage OnMessage;

	UFUNCTION(BlueprintCallable, Category = "Assembly|Workbench")
	bool InitializeWorkbench(UAssemblyComponent* Model, UAssemblyInteractionComponent* Interaction, APlayerController* PC, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Workbench")
	void ShutdownWorkbench();
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	bool IsWorkbenchInitialized() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	UAssemblyComponent* GetAssemblyComponent() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	UAssemblyInteractionComponent* GetInteractionComponent() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	APlayerController* GetPlayerController() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	EAssemblyWorkbenchTool GetTool() const { return Tool; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	bool IsRigidPartTool() const { return Tool == EAssemblyWorkbenchTool::Bone || Tool == EAssemblyWorkbenchTool::JointPart; }
	// A drag transport may only operate on the exact tool transaction which created it.
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	FGuid GetToolId() const { return ToolId; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Selection")
	FAssemblySelection GetSelection() const { return Selection; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Workbench")
	FText GetLastMessage() const { return LastMessage; }

	UFUNCTION(BlueprintCallable, Category = "Assembly|Tools")
	bool BeginShelfBoneDrag(FName TypeId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Tools")
	bool BeginShelfJointDrag(FName TypeId, FText& OutReason);
	// Invalid ExistingId installs from stock; a valid id edits endpoints without changing its binding.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Tools")
	bool BeginMusclePlacement(FName TypeId, FGuid ExistingId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Tools")
	bool BeginMoveSelected(FText& OutReason);
	// Viewport pixels, never a UMG local position or a DPI-scaled slate position.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Pointer")
	bool UpdatePointer(FVector2D ViewportPixels, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Pointer")
	bool PointerPressed(FVector2D ViewportPixels, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Pointer")
	bool PointerReleased(FVector2D ViewportPixels, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Tools")
	bool CommitTool(FGuid& OutInstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Tools")
	void CancelTool();
	UFUNCTION(BlueprintCallable, Category = "Assembly|Tools")
	bool RotateTool(float DeltaDegrees, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Pointer")
	void SetExternalPointerTracking(bool bExternal);

	UFUNCTION(BlueprintCallable, Category = "Assembly|Selection")
	bool SelectInstance(FGuid Id, EAssemblySelectionKind Kind, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Selection")
	void ClearSelection();
	UFUNCTION(BlueprintCallable, Category = "Assembly|Selection")
	bool RemoveSelected(FText& OutReason);
	// Rebinds the selected muscle, or selects the binding for new muscles.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Input")
	bool SelectBindingKey(EAssemblyMuscleKey Key, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Joints")
	bool SetSelectedJointKind(EAssemblyJointKind Kind, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Joints")
	bool SetSelectedJointLimits(float Min, float Max, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Joints")
	bool RequestSelectedJointBreak(FText& OutReason);

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyComponent> Assembly;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyInteractionComponent> Presenter;
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> Player;
	UPROPERTY(Transient)
	TObjectPtr<AAssemblyMuscle> MusclePreviewActor;
	UPROPERTY(Transient)
	TWeakObjectPtr<AAssemblyMuscle> HiddenMuscleActor;
	EAssemblyWorkbenchTool Tool = EAssemblyWorkbenchTool::None;
	FGuid ToolId;
	FAssemblySelection Selection;
	FText LastMessage;
	FName ToolTypeId;
	FGuid ToolExistingId;
	FAssemblyMuscleEndpoint FirstEndpoint;
	FAssemblyMuscleEndpoint CandidateEndpoint;
	FAssemblyValidationResult MusclePreview;
	FVector2D PointerPixels = FVector2D::ZeroVector;
	FVector2D PointerLocal = FVector2D::ZeroVector;
	FVector PointerWorld = FVector::ZeroVector;
	FVector2D BoneGrabOffset = FVector2D::ZeroVector;
	FVector2D BonePressPixels = FVector2D::ZeroVector;
	bool bPointerValid = false;
	bool bHasPointerPixels = false;
	bool bExternalPointerTracking = false;
	bool bPointerBoneMove = false;
	bool bBoneMovePassedThreshold = false;
	bool bBoneToolRotated = false;
	bool bHiddenMuscleWasHidden = false;
	bool bCancelPending = false;
	bool bResetSelectionPending = false;
	bool bReconcilePending = false;
	bool bShutdown = false;

	bool CanUse(FText& OutReason) const;
	bool CanEdit(FText& OutReason) const;
	bool Fail(const FText& Reason, FText& OutReason);
	void Report(const FText& Message);
	bool ProjectPointer(FVector2D ViewportPixels, FVector2D& OutLocal, FVector& OutWorld, FText& OutReason) const;
	void InvalidatePointer(const FText& Reason);
	bool BeginBoneMove(FGuid BoneId, FText& OutReason);
	bool BeginShelfRigidDrag(FName TypeId, EAssemblyRigidPartKind PartKind, FText& OutReason);
	bool UpdateMusclePreview(FText& OutReason);
	bool CaptureFirstEndpoint(FText& OutReason);
	bool CommitMuscle(FGuid& OutId, FText& OutReason);
	void DestroyMusclePreview();
	bool SelectionExists(const FAssemblySelection& Value) const;
	bool PickAtPointer(FVector2D ViewportPixels, FAssemblySelection& OutSelection) const;
	bool GetJointWorldAnchors(const FAssemblyJointInstance& Joint, FVector& OutA, FVector& OutB) const;
	void DrawSelectionFeedback() const;
	void ReconcileState();
	UFUNCTION()
	void HandleAssemblyChanged();
	UFUNCTION()
	void HandleAssemblyReset();
	UFUNCTION()
	void HandlePhaseChanged(EAssemblyPhase Phase);
};
