#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Assembly/AssemblyTypes.h"
#include "AssemblyInteractionComponent.generated.h"

class UAssemblyComponent;
class ANormalBone;
class AAssemblyMuscle;
class APlayerController;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UAssemblyPhysicsBridgeComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAssemblyInteractionChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAssemblyInteractionMessage, FText, Message);

/** World-space presentation and transactional drag operations. UMG owns input and shelf widgets. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblyInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAssemblyInteractionComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Interaction")
	bool bAutoInitialize = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Visual")
	TSubclassOf<ANormalBone> BoneActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Visual")
	TSubclassOf<AAssemblyMuscle> MuscleActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Visual")
	FLinearColor ValidPreviewColor = FLinearColor(0.15f, 0.85f, 0.3f, 0.65f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Visual")
	FLinearColor InvalidPreviewColor = FLinearColor(0.95f, 0.15f, 0.1f, 0.65f);
	// Uses a body already supplied by the character/physics program when configured.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Core")
	TObjectPtr<UPrimitiveComponent> ExistingCoreBody;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Core")
	bool bCreateCorePlaceholder = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Physics")
	bool bAutoCreatePhysicsBridge = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|Physics")
	TSubclassOf<UAssemblyPhysicsBridgeComponent> PhysicsBridgeClass;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyInteractionChanged OnBoneDragPreviewChanged;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyInteractionChanged OnViewsChanged;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyInteractionMessage OnInteractionMessage;

	// Call after EnterAssembly. Repeated calls with the same component never duplicate views or bindings.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Interaction")
	bool InitializeInteraction(UAssemblyComponent* Assembly, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Interaction")
	void ClearInteraction();
	UFUNCTION(BlueprintPure, Category = "Assembly|Interaction")
	UAssemblyComponent* GetAssemblyComponent() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Interaction")
	bool IsInteractionInitialized() const;
	// Invalid ExistingId starts a shelf drag; a valid id moves an installed bone without consuming stock.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Drag")
	bool BeginBoneDrag(FName TypeId, FGuid ExistingId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Drag")
	bool UpdateBoneDragFromMouse(APlayerController* PlayerController, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Drag")
	bool UpdateBoneDragAtLocalPosition(FVector2D LocalPosition, FText& OutReason);
	// Any finite angle is allowed. A Blueprint R event may supply 90, or a free-rotation control 15, etc.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Drag")
	bool RotateBoneDrag(float DeltaDegrees, FText& OutReason);
	// Invalid release automatically cancels and restores the original actor/stock.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Drag")
	bool CommitBoneDrag(FGuid& OutInstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Drag")
	void CancelBoneDrag();
	UFUNCTION(BlueprintPure, Category = "Assembly|Drag")
	bool IsBoneDragActive() const { return bDragging; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Drag")
	FAssemblyPlacementResult GetBoneDragPreview() const { return DragPreview; }
	UFUNCTION(BlueprintPure, Category = "Assembly|Visual")
	ANormalBone* GetBoneActor(FGuid InstanceId) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Visual")
	AAssemblyMuscle* GetMuscleActor(FGuid InstanceId) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Core")
	UPrimitiveComponent* GetCoreBody() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Physics")
	UAssemblyPhysicsBridgeComponent* GetPhysicsBridge() const;

	// Two clicks on world bone surfaces. Only muscles require the model's selected W/A/S/D key.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Muscles")
	bool InstallMuscleAtWorldPoints(FName TypeId, FVector WorldA, FVector WorldB, FGuid& OutInstanceId, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Muscles")
	bool MoveMuscleAtWorldPoints(FGuid InstanceId, FVector WorldA, FVector WorldB, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Interaction")
	bool RemoveInstance(FGuid InstanceId, FText& OutReason);
	// For explicit external model edits; automatically called on OnAssemblyChanged.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Visual")
	bool SynchronizeViews(FText& OutReason);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyComponent> BoundAssembly;
	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<ANormalBone>> BoneViews;
	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<AAssemblyMuscle>> MuscleViews;
	UPROPERTY(Transient)
	TObjectPtr<ANormalBone> PreviewActor;
	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> CreatedCoreBody;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CreatedCoreVisual;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CorePlaceholderMesh;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyPhysicsBridgeComponent> BoundBridge;
	bool bOwnsBridge = false;
	bool bDragging = false;
	bool bSyncing = false;
	bool bSuppressSynchronization = false;
	bool bDraggedActorWasHidden = false;
	bool bHasPointerPosition = false;
	bool bAutoInitializeAttempted = false;
	FName DragTypeId;
	FGuid DragExistingId;
	FAssemblyPlacementResult DragPreview;
	FTransform SpawnOwnerTransform = FTransform::Identity;

	bool CanEdit(FText& OutReason) const;
	bool CreateCore(FText& OutReason);
	void UpdateCorePlaceholder();
	ANormalBone* SpawnBoneView(FGuid InstanceId, FText& OutReason);
	AAssemblyMuscle* SpawnMuscleView(FGuid InstanceId, FText& OutReason);
	void ApplyPreview();
	void DestroyViews();
	void RequestPhysicsSnapshot();
	UFUNCTION()
	void HandleAssemblyChanged();
	UFUNCTION()
	void HandleAssemblyReset();
	UFUNCTION()
	void HandlePhaseChanged(EAssemblyPhase Phase);
};
