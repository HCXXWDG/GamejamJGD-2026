#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Engine/World.h"
#include "PaperSprite.h"

#define LOCTEXT_NAMESPACE "AssemblyWorkbenchMuscles"

bool UAssemblyWorkbenchComponent::BeginMusclePlacement(FName TypeId, FGuid ExistingId, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	const TArray<FAssemblyMuscleDefinition> Definitions = Assembly->GetMuscleDefinitions();
	if (!Definitions.ContainsByPredicate([TypeId](const FAssemblyMuscleDefinition& Value) { return Value.TypeId == TypeId; }))
	{
		return Fail(LOCTEXT("UnknownMuscle", "本关卡没有这种肌肉。"), OutReason);
	}
	FAssemblyMuscleInstance Existing;
	AAssemblyMuscle* ExistingActor = nullptr;
	if (ExistingId.IsValid())
	{
		ExistingActor = Presenter->GetMuscleActor(ExistingId);
		if (!Assembly->GetMuscle(ExistingId, Existing) || Existing.TypeId != TypeId || !IsValid(ExistingActor))
		{
			return Fail(LOCTEXT("MissingMovingMuscle", "找不到要移动的同类型肌肉及其曲线。"), OutReason);
		}
	}
	else if (Assembly->GetRemainingQuantity(TypeId) <= 0)
	{
		return Fail(LOCTEXT("NoMuscleStock", "该肌肉的剩余数量不足。"), OutReason);
	}
	else if (Assembly->GetSelectedMuscleKey() == EAssemblyMuscleKey::None)
	{
		OnMuscleBindingRequired.Broadcast();
		return Fail(LOCTEXT("ChooseBindingFirst", "请先选择 W、A、S 或 D 作为新肌肉的绑定键，再开始放置。"), OutReason);
	}
	if (!Presenter->MuscleActorClass || !GetWorld())
	{
		return Fail(LOCTEXT("NoMuscleClass", "请先配置肌肉预览 Actor 类。"), OutReason);
	}
	CancelTool();
	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MusclePreviewActor = GetWorld()->SpawnActor<AAssemblyMuscle>(Presenter->MuscleActorClass, FTransform::Identity, Params);
	if (!IsValid(MusclePreviewActor))
	{
		return Fail(LOCTEXT("MusclePreviewSpawnFailed", "无法创建肌肉预览，库存和已有肌肉未改变。"), OutReason);
	}
	const UPaperSprite* Sprite = MusclePreviewActor->CurveSprite.Get();
	const FBoxSphereBounds Bounds = IsValid(Sprite) ? Sprite->GetRenderBounds() : FBoxSphereBounds(EForceInit::ForceInit);
	if (!IsValid(Sprite) || Bounds.Origin.ContainsNaN() || Bounds.BoxExtent.ContainsNaN()
		|| Bounds.BoxExtent.X <= UE_SMALL_NUMBER || Bounds.BoxExtent.Z <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(MusclePreviewActor->LineWidth) || MusclePreviewActor->LineWidth <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(MusclePreviewActor->BendRatio))
	{
		DestroyMusclePreview();
		return Fail(LOCTEXT("MusclePreviewInvalid", "肌肉预览 Sprite、线宽或弯曲参数无效。"), OutReason);
	}
	MusclePreviewActor->SetActorEnableCollision(false);
	MusclePreviewActor->SetActorHiddenInGame(true);
	Tool = EAssemblyWorkbenchTool::MuscleFirst;
	ToolId = FGuid::NewGuid();
	ToolTypeId = TypeId;
	ToolExistingId = ExistingId;
	if (ExistingActor)
	{
		HiddenMuscleActor = ExistingActor;
		bHiddenMuscleWasHidden = ExistingActor->IsHidden();
		ExistingActor->SetActorHiddenInGame(true);
	}
	else
	{
		ClearSelection();
	}
	LastMessage = LOCTEXT("ChooseFirstAnchor", "点击骨头内侧或外侧的第一连接点；靠近轮廓可吸附。Esc 或右键取消。");
	OutReason = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::UpdateMusclePreview(FText& OutReason)
{
	CandidateEndpoint = FAssemblyMuscleEndpoint();
	MusclePreview = FAssemblyValidationResult();
	if (!CanEdit(OutReason) || !bPointerValid)
	{
		MusclePreview.Reason = OutReason;
		if (IsValid(MusclePreviewActor)) { MusclePreviewActor->SetPreviewValid(false); }
		return false;
	}
	const bool bOnBone = Assembly->FindMuscleEndpointAtWorldPosition(
		PointerWorld, CandidateEndpoint, MuscleEndpointSnapDistance);
	if (!bOnBone) { CandidateEndpoint = FAssemblyMuscleEndpoint(); }
	if (Tool == EAssemblyWorkbenchTool::MuscleFirst)
	{
		MusclePreview.bValid = bOnBone;
		if (!bOnBone) { MusclePreview.Reason = LOCTEXT("FirstOffBone", "第一连接点必须位于骨头上，核心和关节组件不能连接肌肉。"); }
		OutReason = MusclePreview.Reason;
		return bOnBone;
	}
	FVector WorldA;
	if (!Assembly->GetEndpointWorldPosition(FirstEndpoint, WorldA))
	{
		OutReason = LOCTEXT("FirstAnchorGone", "第一连接点的骨头已失效，请取消并重新选择。");
		MusclePreview.Reason = OutReason;
		if (IsValid(MusclePreviewActor)) { MusclePreviewActor->SetPreviewValid(false); }
		return false;
	}
	EAssemblyMuscleKey Key = Assembly->GetSelectedMuscleKey();
	if (ToolExistingId.IsValid())
	{
		FAssemblyMuscleInstance Existing;
		if (!Assembly->GetMuscle(ToolExistingId, Existing))
		{
			OutReason = LOCTEXT("MovingMuscleGone", "要移动的肌肉已失效。");
			MusclePreview.Reason = OutReason;
			if (IsValid(MusclePreviewActor)) { MusclePreviewActor->SetPreviewValid(false); }
			return false;
		}
		Key = Existing.BindingKey;
	}
	if (bOnBone)
	{
		MusclePreview = Assembly->ValidateMusclePlacement(ToolTypeId, FirstEndpoint, CandidateEndpoint, Key, ToolExistingId);
	}
	else
	{
		MusclePreview.Reason = LOCTEXT("SecondOffBone", "第二连接点必须位于骨头上，核心和关节组件不能连接肌肉。");
	}
	if (IsValid(MusclePreviewActor))
	{
		// Equal anchors are invalid. A small visible stub shows that first-click state without
		// changing either stored endpoint or the model's validation result.
		FVector WorldB = PointerWorld;
		if (bOnBone && !Assembly->GetEndpointWorldPosition(CandidateEndpoint, WorldB))
		{
			WorldB = PointerWorld;
			MusclePreview.bValid = false;
			MusclePreview.Reason = LOCTEXT("SecondAnchorGone", "第二连接点的骨头已失效，请重新选择。");
		}
		if (FVector::DistSquared(WorldA, WorldB) <= UE_KINDA_SMALL_NUMBER)
		{
			WorldB = WorldA + Assembly->GetGridFrameTransform().TransformVectorNoScale(FVector::XAxisVector).GetSafeNormal()
				* FMath::Max(1.0f, MusclePreviewActor->LineWidth);
		}
		FText CurveReason;
		if (!MusclePreviewActor->SetPreviewEndpoints(WorldA, WorldB, CurveReason))
		{
			MusclePreview.bValid = false;
			MusclePreview.Reason = CurveReason;
		}
		MusclePreviewActor->SetPreviewValid(MusclePreview.bValid);
		MusclePreviewActor->SetActorHiddenInGame(false);
	}
	else
	{
		MusclePreview.bValid = false;
		MusclePreview.Reason = LOCTEXT("PreviewGone", "肌肉预览已失效，请重新开始放置。");
	}
	OutReason = MusclePreview.Reason;
	return MusclePreview.bValid;
}

bool UAssemblyWorkbenchComponent::CaptureFirstEndpoint(FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	if (!bPointerValid || !Assembly->FindMuscleEndpointAtWorldPosition(
		PointerWorld, FirstEndpoint, MuscleEndpointSnapDistance))
	{
		FirstEndpoint = FAssemblyMuscleEndpoint();
		return Fail(LOCTEXT("CannotCaptureFirst", "请在骨头上点击第一连接点，核心和关节组件不能连接肌肉。"), OutReason);
	}
	Tool = EAssemblyWorkbenchTool::MuscleSecond;
	LastMessage = LOCTEXT("ChooseSecondAnchor", "点击骨头内侧或外侧的第二连接点；同一对骨头可连接多条肌肉，同一骨头可选两个不同位置。Esc 或右键取消。");
	FText PreviewReason;
	UpdateMusclePreview(PreviewReason);
	OutReason = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::CommitMuscle(FGuid& OutId, FText& OutReason)
{
	OutId.Invalidate();
	if (!UpdateMusclePreview(OutReason)) { return Fail(OutReason, OutReason); }
	bool bSuccess = false;
	if (ToolExistingId.IsValid())
	{
		bSuccess = Assembly->TryMoveMuscle(ToolExistingId, FirstEndpoint, CandidateEndpoint, OutReason);
		if (bSuccess) { OutId = ToolExistingId; }
	}
	else
	{
		// Preserve the bone-local first anchor; never re-pick it from a stale world point.
		bSuccess = Presenter->InstallMuscleAtEndpoints(ToolTypeId, FirstEndpoint, CandidateEndpoint, OutId, OutReason);
	}
	if (!bSuccess) { return Fail(OutReason, OutReason); }
	CancelTool();
	LastMessage = FText::GetEmpty();
	SelectInstance(OutId, EAssemblySelectionKind::Muscle, OutReason);
	return true;
}

void UAssemblyWorkbenchComponent::DestroyMusclePreview()
{
	if (IsValid(MusclePreviewActor)) { MusclePreviewActor->Destroy(); }
	MusclePreviewActor = nullptr;
}

#undef LOCTEXT_NAMESPACE
