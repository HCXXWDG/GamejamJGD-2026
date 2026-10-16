#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Assembly/AssemblyWorkbenchTypes.h"
#include "AssemblyShelfCardWidget.generated.h"

class UAssemblyWorkbenchComponent;
class UTextBlock;
class UBorder;
class UAssemblySpriteImage;
class UPaperSprite;

/** A replaceable 64-unit shelf card. It never changes stock before a successful world drop. */
UCLASS(BlueprintType, Blueprintable)
class JGD2026_API UAssemblyShelfCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	bool bBuildPlaceholderUI = true;
	// Optional shelf-only artwork. Unset uses the configured bone/muscle actor's sprite.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	TObjectPtr<UPaperSprite> IconSprite;
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void InitializeCard(UAssemblyWorkbenchComponent* InWorkbench, FName InTypeId, FText InDisplayName, bool bInMuscle);
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void InitializePartCard(UAssemblyWorkbenchComponent* InWorkbench, FName InTypeId, FText InDisplayName, EAssemblyShelfPartKind InPartKind);
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void RefreshQuantity();
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	FName GetTypeId() const { return TypeId; }
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	EAssemblyShelfPartKind GetPartKind() const { return PartKind; }
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	int32 GetRemainingQuantity() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	UPaperSprite* GetPresentationSprite() const;
	UFUNCTION(BlueprintImplementableEvent, Category = "Assembly|UI")
	void OnCardPresentationChanged(const FText& InDisplayName, int32 RemainingQuantity, bool bIsMuscle);
	UFUNCTION(BlueprintImplementableEvent, Category = "Assembly|UI")
	void OnPartCardPresentationChanged(const FText& InDisplayName, int32 RemainingQuantity, EAssemblyShelfPartKind InPartKind);

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, UDragDropOperation* Operation) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyWorkbenchComponent> Workbench;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Caption;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Background;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblySpriteImage> IconImage;
	UPROPERTY(Transient)
	TObjectPtr<UPaperSprite> AppliedIconSprite;
	FName TypeId;
	FText DisplayName;
	EAssemblyShelfPartKind PartKind = EAssemblyShelfPartKind::Bone;
	void BuildPlaceholder();
};
