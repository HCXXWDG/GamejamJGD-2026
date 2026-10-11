#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "AssemblyDragDropOperation.generated.h"

class UAssemblyWorkbenchComponent;

/** UMG transport only. The workbench retains ownership of inventory and the preview transaction. */
UCLASS(BlueprintType, Blueprintable)
class JGD2026_API UAssemblyDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void InitializeDrag(UAssemblyWorkbenchComponent* InWorkbench, bool bInMuscle);
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	bool FinishAtViewportPosition(FVector2D ViewportPixels, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void CancelAssemblyDrag();
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	UAssemblyWorkbenchComponent* GetWorkbench() const { return Workbench.Get(); }
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	bool IsMuscleDrag() const { return bMuscle; }
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	bool IsCurrentDrag() const { return OwnsCurrentTool(); }

	virtual void Dragged_Implementation(const FPointerEvent& PointerEvent) override;
	virtual void DragCancelled_Implementation(const FPointerEvent& PointerEvent) override;
	virtual void Drop_Implementation(const FPointerEvent& PointerEvent) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyWorkbenchComponent> Workbench;
	bool bMuscle = false;
	bool bFinished = false;
	FGuid ToolId;
	bool OwnsCurrentTool() const;
};
