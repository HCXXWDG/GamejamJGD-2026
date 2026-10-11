#include "Assembly/AssemblyDragDropOperation.h"

#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Blueprint/SlateBlueprintLibrary.h"

void UAssemblyDragDropOperation::InitializeDrag(UAssemblyWorkbenchComponent* InWorkbench, bool bInMuscle)
{
	CancelAssemblyDrag();
	Workbench = InWorkbench;
	bMuscle = bInMuscle;
	bFinished = false;
	ToolId = InWorkbench ? InWorkbench->GetToolId() : FGuid();
	if (OwnsCurrentTool()) InWorkbench->SetExternalPointerTracking(true);
	else bFinished = true;
}

bool UAssemblyDragDropOperation::OwnsCurrentTool() const
{
	const UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get();
	return !bFinished && ToolId.IsValid() && ActiveWorkbench && ActiveWorkbench->GetToolId() == ToolId
		&& ActiveWorkbench->GetTool() == (bMuscle ? EAssemblyWorkbenchTool::MuscleFirst : EAssemblyWorkbenchTool::Bone);
}

bool UAssemblyDragDropOperation::FinishAtViewportPosition(FVector2D ViewportPixels, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get();
	if (!OwnsCurrentTool())
	{
		bFinished = true;
		OutReason = FText::FromString(TEXT("拖拽已经结束。"));
		return false;
	}
	// Refresh on release: a failed projection must never commit the previous valid preview.
	const bool bPointerValid = ActiveWorkbench->UpdatePointer(ViewportPixels, OutReason);
	if (!OwnsCurrentTool())
	{
		bFinished = true;
		OutReason = FText::FromString(TEXT("拖拽已取消或被另一操作替换。"));
		return false;
	}
	bFinished = true;
	if (!bPointerValid)
	{
		ActiveWorkbench->SetExternalPointerTracking(false);
		ActiveWorkbench->CancelTool();
		return false;
	}
	bool bSucceeded = false;
	if (bMuscle)
	{
		// The first drop anchors the curve. A subsequent world click selects its second end.
		bSucceeded = ActiveWorkbench->PointerPressed(ViewportPixels, OutReason);
	}
	else
	{
		FGuid InstalledId;
		bSucceeded = ActiveWorkbench->CommitTool(InstalledId, OutReason);
	}
	// A successful commit can notify Blueprint code which starts a new tool. Never alter it.
	if (ActiveWorkbench->GetToolId() == ToolId)
	{
		ActiveWorkbench->SetExternalPointerTracking(false);
		if (!bSucceeded) ActiveWorkbench->CancelTool();
	}
	return bSucceeded;
}

void UAssemblyDragDropOperation::CancelAssemblyDrag()
{
	if (bFinished) return;
	const bool bOwnsTool = OwnsCurrentTool();
	bFinished = true;
	if (bOwnsTool)
	{
		UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get();
		ActiveWorkbench->SetExternalPointerTracking(false);
		ActiveWorkbench->CancelTool();
	}
}

void UAssemblyDragDropOperation::Dragged_Implementation(const FPointerEvent& PointerEvent)
{
	if (OwnsCurrentTool())
	{
		if (UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get())
		{
			FVector2D PixelPosition, ViewportPosition;
			USlateBlueprintLibrary::AbsoluteToViewport(ActiveWorkbench, PointerEvent.GetScreenSpacePosition(), PixelPosition, ViewportPosition);
			FText Reason;
			ActiveWorkbench->UpdatePointer(PixelPosition, Reason);
		}
	}
	Super::Dragged_Implementation(PointerEvent);
}

void UAssemblyDragDropOperation::DragCancelled_Implementation(const FPointerEvent& PointerEvent)
{
	CancelAssemblyDrag();
	Super::DragCancelled_Implementation(PointerEvent);
}

void UAssemblyDragDropOperation::Drop_Implementation(const FPointerEvent& PointerEvent)
{
	// Only the world drop target may finish this transaction; unrelated UI drops roll it back.
	CancelAssemblyDrag();
	Super::Drop_Implementation(PointerEvent);
}
