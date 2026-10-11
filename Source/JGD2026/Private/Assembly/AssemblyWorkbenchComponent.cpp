#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "GameFramework/PlayerController.h"

#define LOCTEXT_NAMESPACE "AssemblyWorkbench"

UAssemblyWorkbenchComponent::UAssemblyWorkbenchComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UAssemblyComponent* UAssemblyWorkbenchComponent::GetAssemblyComponent() const { return Assembly.Get(); }
UAssemblyInteractionComponent* UAssemblyWorkbenchComponent::GetInteractionComponent() const { return Presenter.Get(); }
APlayerController* UAssemblyWorkbenchComponent::GetPlayerController() const { return Player.Get(); }
bool UAssemblyWorkbenchComponent::IsWorkbenchInitialized() const
{
	return Assembly.IsValid() && Assembly->IsInitialized() && Presenter.IsValid() && Player.IsValid()
		&& Presenter->GetAssemblyComponent() == Assembly.Get();
}

bool UAssemblyWorkbenchComponent::InitializeWorkbench(UAssemblyComponent* Model,
	UAssemblyInteractionComponent* Interaction, APlayerController* PC, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!IsValid(Model) || !Model->IsInitialized() || !IsValid(Interaction) || !IsValid(PC)
		|| !GetWorld() || Model->GetWorld() != GetWorld() || Interaction->GetWorld() != GetWorld()
		|| PC->GetWorld() != GetWorld() || Interaction->GetAssemblyComponent() != Model)
	{
		return Fail(LOCTEXT("InvalidBinding", "请提供同一世界的已初始化组装模型、外观组件和玩家控制器。"), OutReason);
	}
	if (Assembly.Get() == Model && Presenter.Get() == Interaction && Player.Get() == PC)
	{
		return true;
	}
	ShutdownWorkbench();
	bShutdown = false;
	Assembly = Model;
	Presenter = Interaction;
	Player = PC;
	Model->OnAssemblyChanged.AddUniqueDynamic(this, &ThisClass::HandleAssemblyChanged);
	Model->OnAssemblyReset.AddUniqueDynamic(this, &ThisClass::HandleAssemblyReset);
	Model->OnPhaseChanged.AddUniqueDynamic(this, &ThisClass::HandlePhaseChanged);
	LastMessage = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

void UAssemblyWorkbenchComponent::ShutdownWorkbench()
{
	bShutdown = true;
	CancelTool();
	ClearSelection();
	if (Assembly.IsValid())
	{
		Assembly->OnAssemblyChanged.RemoveDynamic(this, &ThisClass::HandleAssemblyChanged);
		Assembly->OnAssemblyReset.RemoveDynamic(this, &ThisClass::HandleAssemblyReset);
		Assembly->OnPhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhaseChanged);
	}
	Assembly.Reset();
	Presenter.Reset();
	Player.Reset();
	bHasPointerPixels = false;
	bPointerValid = false;
	bExternalPointerTracking = false;
	bCancelPending = false;
	bResetSelectionPending = false;
	bReconcilePending = false;
}

bool UAssemblyWorkbenchComponent::CanUse(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!IsWorkbenchInitialized())
	{
		OutReason = LOCTEXT("NotReady", "组装操作组件尚未初始化。");
		return false;
	}
	return true;
}

bool UAssemblyWorkbenchComponent::CanEdit(FText& OutReason) const
{
	if (!CanUse(OutReason)) { return false; }
	if (bCancelPending || bResetSelectionPending)
	{
		OutReason = LOCTEXT("LifecyclePending", "组装阶段正在重置或切换，请重新开始本次操作。");
		return false;
	}
	if (Assembly->GetPhase() != EAssemblyPhase::Assembly || Assembly->GetPlacementMode() != EAssemblyPlacementMode::Free)
	{
		OutReason = LOCTEXT("NotEditing", "仅在自由组装阶段允许编辑。");
		return false;
	}
	return true;
}

void UAssemblyWorkbenchComponent::Report(const FText& Message)
{
	LastMessage = Message;
	if (!Message.IsEmpty()) { OnMessage.Broadcast(Message); }
}

bool UAssemblyWorkbenchComponent::Fail(const FText& Reason, FText& OutReason)
{
	OutReason = Reason;
	Report(Reason);
	return false;
}

bool UAssemblyWorkbenchComponent::ProjectPointer(FVector2D ViewportPixels,
	FVector2D& OutLocal, FVector& OutWorld, FText& OutReason) const
{
	OutLocal = FVector2D::ZeroVector;
	OutWorld = FVector::ZeroVector;
	if (!CanUse(OutReason)) { return false; }
	int32 Width = 0, Height = 0;
	Player->GetViewportSize(Width, Height);
	FVector Origin, Direction;
	if (!FMath::IsFinite(ViewportPixels.X) || !FMath::IsFinite(ViewportPixels.Y)
		|| Width <= 0 || Height <= 0 || ViewportPixels.X < 0.0 || ViewportPixels.Y < 0.0
		|| ViewportPixels.X >= Width || ViewportPixels.Y >= Height
		|| !Player->DeprojectScreenPositionToWorld(ViewportPixels.X, ViewportPixels.Y, Origin, Direction)
		|| !Assembly->RayToAssemblyPlane(Origin, Direction, OutLocal))
	{
		OutReason = LOCTEXT("NoPointerProjection", "指针不在游戏视口内，或无法投射到组装平面。");
		return false;
	}
	OutWorld = Assembly->GetGridFrameTransform().TransformPosition(FVector(OutLocal.X, 0.0, OutLocal.Y));
	if (OutWorld.ContainsNaN())
	{
		OutReason = LOCTEXT("InvalidPointerWorld", "指针投射产生了无效的世界坐标。");
		return false;
	}
	return true;
}

void UAssemblyWorkbenchComponent::InvalidatePointer(const FText& Reason)
{
	bPointerValid = false;
	MusclePreview.bValid = false;
	MusclePreview.Reason = Reason;
	CandidateEndpoint = FAssemblyMuscleEndpoint();
	if (Presenter.IsValid() && Tool == EAssemblyWorkbenchTool::Bone)
	{
		Presenter->InvalidateBoneDragPreview(Reason);
	}
	if (IsValid(MusclePreviewActor)) { MusclePreviewActor->SetPreviewValid(false); }
}

bool UAssemblyWorkbenchComponent::UpdatePointer(FVector2D ViewportPixels, FText& OutReason)
{
	PointerPixels = ViewportPixels;
	bHasPointerPixels = true;
	if (!ProjectPointer(ViewportPixels, PointerLocal, PointerWorld, OutReason))
	{
		InvalidatePointer(OutReason);
		return false;
	}
	bPointerValid = true;
	if (Tool == EAssemblyWorkbenchTool::Bone)
	{
		if (bPointerBoneMove && FVector2D::Distance(BonePressPixels, PointerPixels) >= FMath::Max(0.0f, BoneDragThresholdPixels))
		{
			bBoneMovePassedThreshold = true;
		}
		return Presenter->UpdateBoneDragAtLocalPosition(PointerLocal + BoneGrabOffset, OutReason);
	}
	if (Tool == EAssemblyWorkbenchTool::MuscleFirst || Tool == EAssemblyWorkbenchTool::MuscleSecond)
	{
		return UpdateMusclePreview(OutReason);
	}
	OutReason = FText::GetEmpty();
	return true;
}

bool UAssemblyWorkbenchComponent::BeginShelfBoneDrag(FName TypeId, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	CancelTool();
	ClearSelection();
	if (!Presenter->BeginBoneDrag(TypeId, FGuid(), OutReason)) { return Fail(OutReason, OutReason); }
	Tool = EAssemblyWorkbenchTool::Bone;
	ToolId = FGuid::NewGuid();
	ToolTypeId = TypeId;
	LastMessage = FText::GetEmpty();
	// The new tool must receive a fresh pointer. Never reuse a previous drop's valid preview.
	bPointerValid = false;
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::BeginBoneMove(FGuid BoneId, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	FAssemblyBoneInstance Bone;
	if (!Assembly->GetBone(BoneId, Bone)) { return Fail(LOCTEXT("MissingBone", "找不到选中的骨头。"), OutReason); }
	CancelTool();
	if (!Presenter->BeginBoneDrag(Bone.TypeId, BoneId, OutReason)) { return Fail(OutReason, OutReason); }
	Tool = EAssemblyWorkbenchTool::Bone;
	ToolId = FGuid::NewGuid();
	ToolTypeId = Bone.TypeId;
	ToolExistingId = BoneId;
	bPointerValid = false;
	LastMessage = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::BeginMoveSelected(FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	if (Selection.Kind == EAssemblySelectionKind::Bone)
	{
		return BeginBoneMove(Selection.InstanceId, OutReason);
	}
	if (Selection.Kind == EAssemblySelectionKind::Muscle)
	{
		FAssemblyMuscleInstance Muscle;
		if (Assembly->GetMuscle(Selection.InstanceId, Muscle))
		{
			return BeginMusclePlacement(Muscle.TypeId, Muscle.InstanceId, OutReason);
		}
	}
	return Fail(LOCTEXT("NothingToMove", "请先选择要移动的骨头或肌肉。"), OutReason);
}

bool UAssemblyWorkbenchComponent::PointerPressed(FVector2D ViewportPixels, FText& OutReason)
{
	if (!CanUse(OutReason)) { return Fail(OutReason, OutReason); }
	const bool bCandidateValid = UpdatePointer(ViewportPixels, OutReason);
	if (!bPointerValid) { return Fail(OutReason, OutReason); }
	if (Tool == EAssemblyWorkbenchTool::Bone) { return bCandidateValid; }
	if (Tool == EAssemblyWorkbenchTool::MuscleFirst) { return CaptureFirstEndpoint(OutReason); }
	if (Tool == EAssemblyWorkbenchTool::MuscleSecond)
	{
		FGuid Id;
		return CommitTool(Id, OutReason);
	}
	FAssemblySelection Picked;
	if (!PickAtPointer(ViewportPixels, Picked))
	{
		ClearSelection();
		OutReason = FText::GetEmpty();
		return true;
	}
	if (!SelectInstance(Picked.InstanceId, Picked.Kind, OutReason)) { return false; }
	if (Picked.Kind == EAssemblySelectionKind::Bone && Assembly->GetPhase() == EAssemblyPhase::Assembly)
	{
		FAssemblyBoneInstance Bone;
		if (!Assembly->GetBone(Picked.InstanceId, Bone)) { return false; }
		const FVector2D ClickLocal = PointerLocal;
		if (!BeginBoneMove(Picked.InstanceId, OutReason)) { return false; }
		BoneGrabOffset = Bone.LocalPosition - ClickLocal;
		BonePressPixels = ViewportPixels;
		bPointerBoneMove = true;
		return UpdatePointer(ViewportPixels, OutReason);
	}
	OutReason = FText::GetEmpty();
	return true;
}

bool UAssemblyWorkbenchComponent::PointerReleased(FVector2D ViewportPixels, FText& OutReason)
{
	if (Tool != EAssemblyWorkbenchTool::Bone)
	{
		OutReason = FText::GetEmpty();
		return true; // Muscle anchors are confirmed on two presses, never on the first release.
	}
	UpdatePointer(ViewportPixels, OutReason);
	if (bPointerBoneMove && !bBoneMovePassedThreshold && !bBoneToolRotated)
	{
		CancelTool();
		OutReason = FText::GetEmpty();
		return true;
	}
	FGuid Id;
	return CommitTool(Id, OutReason);
}

bool UAssemblyWorkbenchComponent::CommitTool(FGuid& OutInstanceId, FText& OutReason)
{
	OutInstanceId.Invalidate();
	if (!CanEdit(OutReason)) { CancelTool(); return Fail(OutReason, OutReason); }
	if (Tool == EAssemblyWorkbenchTool::None) { return Fail(LOCTEXT("NoTool", "没有正在放置的组件。"), OutReason); }
	if (!bPointerValid)
	{
		const FText Reason = LOCTEXT("NoFreshPointer", "放置前需要有效的当前指针位置。");
		if (Tool == EAssemblyWorkbenchTool::Bone) { CancelTool(); }
		return Fail(Reason, OutReason);
	}
	if (Tool == EAssemblyWorkbenchTool::Bone)
	{
		const bool bSuccess = Presenter->CommitBoneDrag(OutInstanceId, OutReason);
		CancelTool();
		if (!bSuccess) { return Fail(OutReason, OutReason); }
		SelectInstance(OutInstanceId, EAssemblySelectionKind::Bone, OutReason);
		return true;
	}
	if (Tool == EAssemblyWorkbenchTool::MuscleFirst) { return CaptureFirstEndpoint(OutReason); }
	if (Tool == EAssemblyWorkbenchTool::MuscleSecond) { return CommitMuscle(OutInstanceId, OutReason); }
	return Fail(LOCTEXT("NoTool", "没有正在放置的组件。"), OutReason);
}

void UAssemblyWorkbenchComponent::CancelTool()
{
	const bool bHadTool = Tool != EAssemblyWorkbenchTool::None || IsValid(MusclePreviewActor);
	if (Presenter.IsValid() && Tool == EAssemblyWorkbenchTool::Bone) { Presenter->CancelBoneDrag(); }
	DestroyMusclePreview();
	if (HiddenMuscleActor.IsValid()) { HiddenMuscleActor->SetActorHiddenInGame(bHiddenMuscleWasHidden); }
	HiddenMuscleActor.Reset();
	Tool = EAssemblyWorkbenchTool::None;
	ToolId.Invalidate();
	bExternalPointerTracking = false;
	ToolTypeId = NAME_None;
	ToolExistingId.Invalidate();
	FirstEndpoint = FAssemblyMuscleEndpoint();
	CandidateEndpoint = FAssemblyMuscleEndpoint();
	MusclePreview = FAssemblyValidationResult();
	BoneGrabOffset = FVector2D::ZeroVector;
	bPointerValid = false;
	bPointerBoneMove = false;
	bBoneMovePassedThreshold = false;
	bBoneToolRotated = false;
	bCancelPending = false;
	bExternalPointerTracking = false;
	if (bHadTool && !bShutdown) { OnWorkbenchChanged.Broadcast(); }
}

bool UAssemblyWorkbenchComponent::RotateTool(float DeltaDegrees, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	if (Tool != EAssemblyWorkbenchTool::Bone) { return Fail(LOCTEXT("RotateBoneOnly", "请先拖动骨头再旋转。"), OutReason); }
	if (!FMath::IsFinite(DeltaDegrees)) { return Fail(LOCTEXT("InvalidRotation", "旋转角度必须是有限值。"), OutReason); }
	bBoneToolRotated |= !FMath::IsNearlyZero(DeltaDegrees);
	const bool bValid = Presenter->RotateBoneDrag(DeltaDegrees, OutReason);
	if (!bPointerValid) { Presenter->InvalidateBoneDragPreview(LOCTEXT("RotateNeedsPointer", "旋转已更新，放置前仍需要有效的当前指针位置。")); }
	OnWorkbenchChanged.Broadcast();
	if (!bValid && !OutReason.IsEmpty()) { Report(OutReason); }
	return bValid;
}

void UAssemblyWorkbenchComponent::SetExternalPointerTracking(bool bExternal)
{
	bExternalPointerTracking = bExternal;
	if (bExternal)
	{
		// Slate is now the source of pointer pixels; an old native mouse sample cannot be committed.
		bHasPointerPixels = false;
		InvalidatePointer(LOCTEXT("WaitingForPointer", "等待拖放指针位置。"));
	}
}

void UAssemblyWorkbenchComponent::HandleAssemblyChanged() { bReconcilePending = true; }
void UAssemblyWorkbenchComponent::HandleAssemblyReset() { bCancelPending = true; bResetSelectionPending = true; bReconcilePending = true; }
void UAssemblyWorkbenchComponent::HandlePhaseChanged(EAssemblyPhase Phase)
{
	if (Phase != EAssemblyPhase::Assembly) { bCancelPending = true; }
	bReconcilePending = true;
}

void UAssemblyWorkbenchComponent::ReconcileState()
{
	if (bCancelPending) { CancelTool(); }
	if (bResetSelectionPending) { ClearSelection(); bResetSelectionPending = false; }
	bReconcilePending = false;
	if (!SelectionExists(Selection)) { ClearSelection(); }
	if (ToolExistingId.IsValid())
	{
		FAssemblySelection Existing;
		Existing.InstanceId = ToolExistingId;
		Existing.Kind = Tool == EAssemblyWorkbenchTool::Bone ? EAssemblySelectionKind::Bone : EAssemblySelectionKind::Muscle;
		if (!SelectionExists(Existing)) { CancelTool(); }
	}
	if (Tool == EAssemblyWorkbenchTool::MuscleSecond && Assembly.IsValid())
	{
		FVector WorldA;
		if (!Assembly->GetEndpointWorldPosition(FirstEndpoint, WorldA)) { CancelTool(); }
	}
	OnWorkbenchChanged.Broadcast();
}

void UAssemblyWorkbenchComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bCancelPending || bReconcilePending) { ReconcileState(); }
	if (!IsWorkbenchInitialized())
	{
		if (Tool != EAssemblyWorkbenchTool::None) { CancelTool(); }
		ClearSelection();
		return;
	}
	if (Tool != EAssemblyWorkbenchTool::None && !bExternalPointerTracking)
	{
		float X = 0.0f, Y = 0.0f;
		FText Reason;
		if (Player->GetMousePosition(X, Y)) { UpdatePointer(FVector2D(X, Y), Reason); }
		else { InvalidatePointer(LOCTEXT("MouseUnavailable", "当前无法读取游戏视口指针。")); }
	}
	else if (Tool == EAssemblyWorkbenchTool::MuscleSecond && bExternalPointerTracking && bHasPointerPixels)
	{
		// Keep a saved bone-local first anchor attached while Slate drives the second anchor.
		FText Reason;
		UpdatePointer(PointerPixels, Reason);
	}
	DrawSelectionFeedback();
}

void UAssemblyWorkbenchComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownWorkbench();
	Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
