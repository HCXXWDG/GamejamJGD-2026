#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "PaperSpriteComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AssemblyInteraction"

UAssemblyInteractionComponent::UAssemblyInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	BoneActorClass = ANormalBone::StaticClass();
	MuscleActorClass = AAssemblyMuscle::StaticClass();
	PhysicsBridgeClass = UAssemblyPhysicsBridgeComponent::StaticClass();
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CorePlaceholderMesh = Sphere.Object;
}

UAssemblyComponent* UAssemblyInteractionComponent::GetAssemblyComponent() const { return BoundAssembly.Get(); }
bool UAssemblyInteractionComponent::IsInteractionInitialized() const { return BoundAssembly.IsValid(); }
UAssemblyPhysicsBridgeComponent* UAssemblyInteractionComponent::GetPhysicsBridge() const { return BoundBridge.Get(); }
UPrimitiveComponent* UAssemblyInteractionComponent::GetCoreBody() const
{
	return IsValid(ExistingCoreBody) ? ExistingCoreBody.Get() : CreatedCoreBody.Get();
}

void UAssemblyInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoInitialize && GetOwner())
	{
		UAssemblyComponent* Assembly = GetOwner()->FindComponentByClass<UAssemblyComponent>();
		if (IsValid(Assembly) && Assembly->IsInitialized())
		{
			FText Reason;
			bAutoInitializeAttempted = true;
			if (!InitializeInteraction(Assembly, Reason)) { OnInteractionMessage.Broadcast(Reason); }
		}
	}
}

void UAssemblyInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Component BeginPlay ordering is unspecified; wait for the model instead of resetting it here.
	if (bAutoInitialize && !bAutoInitializeAttempted && !BoundAssembly.IsValid() && GetOwner())
	{
		UAssemblyComponent* Assembly = GetOwner()->FindComponentByClass<UAssemblyComponent>();
		if (IsValid(Assembly) && Assembly->IsInitialized())
		{
			FText Reason;
			bAutoInitializeAttempted = true;
			if (!InitializeInteraction(Assembly, Reason)) { OnInteractionMessage.Broadcast(Reason); }
		}
	}
	UpdateCorePlaceholder();
	// Re-evaluate when Space/physics moves the assembly frame; do not quantize the cursor to cells.
	if (bDragging && bHasPointerPosition && BoundAssembly.IsValid())
	{
		DragPreview = BoundAssembly->ValidateFreeBonePlacement(DragTypeId, DragPreview.LocalPosition, DragPreview.RotationDegrees, DragExistingId);
		ApplyPreview();
	}
}

bool UAssemblyInteractionComponent::CanEdit(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!BoundAssembly.IsValid() || !BoundAssembly->IsInitialized())
	{
		OutReason = LOCTEXT("NotInitialized", "请先初始化 AssemblyInteractionComponent 和 AssemblyComponent。");
		return false;
	}
	if (BoundAssembly->GetPhase() != EAssemblyPhase::Assembly || BoundAssembly->GetPlacementMode() != EAssemblyPlacementMode::Free)
	{
		OutReason = LOCTEXT("NotFreeAssembly", "仅在自由组装阶段允许拖动和编辑。");
		return false;
	}
	return true;
}

bool UAssemblyInteractionComponent::CreateCore(FText& OutReason)
{
	if (GetCoreBody() || !bCreateCorePlaceholder) { return true; }
	if (!GetOwner() || !CorePlaceholderMesh)
	{
		OutReason = LOCTEXT("CoreUnavailable", "无法创建核心白模，请配置已有的 CoreBody。");
		return false;
	}
	CreatedCoreBody = NewObject<USphereComponent>(GetOwner(), NAME_None);
	GetOwner()->AddInstanceComponent(CreatedCoreBody);
	CreatedCoreBody->SetMobility(EComponentMobility::Movable);
	CreatedCoreBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CreatedCoreBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	CreatedCoreBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CreatedCoreBody->SetGenerateOverlapEvents(false);
	if (GetOwner()->GetRootComponent()) { CreatedCoreBody->SetupAttachment(GetOwner()->GetRootComponent()); }
	CreatedCoreBody->RegisterComponent();
	CreatedCoreVisual = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None);
	GetOwner()->AddInstanceComponent(CreatedCoreVisual);
	CreatedCoreVisual->SetupAttachment(CreatedCoreBody);
	CreatedCoreVisual->SetStaticMesh(CorePlaceholderMesh);
	CreatedCoreVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CreatedCoreVisual->SetCastShadow(false);
	CreatedCoreVisual->RegisterComponent();
	UpdateCorePlaceholder();
	return true;
}

void UAssemblyInteractionComponent::UpdateCorePlaceholder()
{
	if (!IsValid(CreatedCoreBody) || !BoundAssembly.IsValid() || CreatedCoreBody->IsSimulatingPhysics()) { return; }
	const FAssemblyFreeSettings Settings = BoundAssembly->GetFreeSettings();
	const FTransform Frame = BoundAssembly->GetGridFrameTransform();
	CreatedCoreBody->SetSphereRadius(Settings.CoreRadius, false);
	CreatedCoreBody->SetWorldLocationAndRotation(Frame.TransformPosition(FVector(Settings.CoreLocalPosition.X, 0.0, Settings.CoreLocalPosition.Y)), Frame.GetRotation());
	if (IsValid(CreatedCoreVisual)) { CreatedCoreVisual->SetRelativeScale3D(FVector(Settings.CoreRadius / 50.0f)); }
}

bool UAssemblyInteractionComponent::InitializeInteraction(UAssemblyComponent* Assembly, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!IsValid(Assembly) || !Assembly->IsInitialized() || !GetWorld() || !GetOwner()
		|| Assembly->GetWorld() != GetWorld()
		|| (IsValid(ExistingCoreBody) && ExistingCoreBody->GetWorld() != GetWorld()))
	{
		OutReason = LOCTEXT("InvalidModel", "请在有世界的角色组件上提供已初始化的 AssemblyComponent。");
		return false;
	}
	if (BoundAssembly.Get() == Assembly) { return SynchronizeViews(OutReason); }
	ClearInteraction();
	BoundAssembly = Assembly;
	SpawnOwnerTransform = GetOwner()->GetActorTransform();
	if (!CreateCore(OutReason)) { ClearInteraction(); return false; }
	Assembly->OnAssemblyChanged.AddUniqueDynamic(this, &UAssemblyInteractionComponent::HandleAssemblyChanged);
	Assembly->OnAssemblyReset.AddUniqueDynamic(this, &UAssemblyInteractionComponent::HandleAssemblyReset);
	Assembly->OnPhaseChanged.AddUniqueDynamic(this, &UAssemblyInteractionComponent::HandlePhaseChanged);
	if (bAutoCreatePhysicsBridge)
	{
		UAssemblyPhysicsBridgeComponent* Bridge = GetOwner()->FindComponentByClass<UAssemblyPhysicsBridgeComponent>();
		if (!Bridge && PhysicsBridgeClass)
		{
			Bridge = NewObject<UAssemblyPhysicsBridgeComponent>(GetOwner(), PhysicsBridgeClass);
			GetOwner()->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			bOwnsBridge = true;
		}
		BoundBridge = Bridge;
		if (Bridge && Bridge->IsBridgeInitialized() && Bridge->GetPhysicsContext().Assembly != Assembly)
		{
			OutReason = LOCTEXT("BridgeDifferentModel", "已有物理桥绑定其他组装模型，请使用独立角色组件。");
			ClearInteraction();
			return false;
		}
		if (Bridge && !Bridge->InitializeBridge(Assembly, GetCoreBody(), OutReason))
		{
			ClearInteraction();
			return false;
		}
	}
	if (!SynchronizeViews(OutReason))
	{
		ClearInteraction();
		return false;
	}
	return true;
}

ANormalBone* UAssemblyInteractionComponent::GetBoneActor(FGuid InstanceId) const
{
	const TObjectPtr<ANormalBone>* Actor = BoneViews.Find(InstanceId);
	return Actor && IsValid(Actor->Get()) ? Actor->Get() : nullptr;
}

AAssemblyMuscle* UAssemblyInteractionComponent::GetMuscleActor(FGuid InstanceId) const
{
	const TObjectPtr<AAssemblyMuscle>* Actor = MuscleViews.Find(InstanceId);
	return Actor && IsValid(Actor->Get()) ? Actor->Get() : nullptr;
}

ANormalBone* UAssemblyInteractionComponent::SpawnBoneView(FGuid InstanceId, FText& OutReason)
{
	if (ANormalBone* Existing = GetBoneActor(InstanceId)) { return Existing; }
	if (!BoneActorClass || !BoundAssembly.IsValid() || !GetWorld())
	{
		OutReason = LOCTEXT("MissingBoneClass", "没有可用的骨头 Actor 类。");
		return nullptr;
	}
	if (BoundAssembly->GetBonePoseSource(InstanceId))
	{
		OutReason = LOCTEXT("ExternalBody", "此实例已有物理程序提供的骨头姿态源，不能生成重复骨头。");
		return nullptr;
	}
	FAssemblyBoneInstance Instance;
	if (!BoundAssembly->GetBone(InstanceId, Instance)) { return nullptr; }
	const FTransform Pose = Instance.LocalVisualTransform * BoundAssembly->GetGridFrameTransform();
	ANormalBone* Bone = GetWorld()->SpawnActorDeferred<ANormalBone>(BoneActorClass, Pose, GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Bone) { OutReason = LOCTEXT("BoneSpawnFailed", "创建骨头外观失败，安装库存已恢复。"); return nullptr; }
	Bone->FinishSpawning(Pose);
	if (!Bone->InitializeBone(BoundAssembly.Get(), InstanceId, OutReason)) { Bone->Destroy(); return nullptr; }
	BoneViews.Add(InstanceId, Bone);
	return Bone;
}

AAssemblyMuscle* UAssemblyInteractionComponent::SpawnMuscleView(FGuid InstanceId, FText& OutReason)
{
	if (AAssemblyMuscle* Existing = GetMuscleActor(InstanceId)) { return Existing; }
	if (!MuscleActorClass || !BoundAssembly.IsValid() || !GetWorld())
	{
		OutReason = LOCTEXT("MissingMuscleClass", "没有可用的肌肉曲线 Actor 类。");
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAssemblyMuscle* Muscle = GetWorld()->SpawnActor<AAssemblyMuscle>(MuscleActorClass, FTransform::Identity, Params);
	if (!Muscle) { OutReason = LOCTEXT("MuscleSpawnFailed", "创建肌肉曲线失败，安装库存已恢复。"); return nullptr; }
	if (!Muscle->InitializeMuscle(BoundAssembly.Get(), InstanceId, OutReason)) { Muscle->Destroy(); return nullptr; }
	MuscleViews.Add(InstanceId, Muscle);
	return Muscle;
}

bool UAssemblyInteractionComponent::SynchronizeViews(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!BoundAssembly.IsValid()) { OutReason = LOCTEXT("MissingModel", "尚未绑定组装模型。"); return false; }
	if (bSyncing) { return true; }
	TGuardValue<bool> Guard(bSyncing, true);
	bool bSuccess = true;
	for (auto It = BoneViews.CreateIterator(); It; ++It)
	{
		FAssemblyBoneInstance Instance;
		if (!BoundAssembly->GetBone(It.Key(), Instance) || !IsValid(It.Value()))
		{
			if (IsValid(It.Value())) { It.Value()->ClearAssemblyBinding(); It.Value()->Destroy(); }
			It.RemoveCurrent();
		}
	}
	for (auto It = MuscleViews.CreateIterator(); It; ++It)
	{
		FAssemblyMuscleInstance Instance;
		if (!BoundAssembly->GetMuscle(It.Key(), Instance) || !IsValid(It.Value()))
		{
			if (IsValid(It.Value())) { It.Value()->ClearAssemblyBinding(); It.Value()->Destroy(); }
			It.RemoveCurrent();
		}
	}
	for (const FAssemblyBoneInstance& Instance : BoundAssembly->GetBones())
	{
		// External physical bodies are authoritative and are intentionally not duplicated or moved.
		if (!GetBoneActor(Instance.InstanceId) && !BoundAssembly->GetBonePoseSource(Instance.InstanceId)
			&& !SpawnBoneView(Instance.InstanceId, OutReason)) { bSuccess = false; }
	}
	for (const FAssemblyMuscleInstance& Instance : BoundAssembly->GetMuscles())
	{
		if (!GetMuscleActor(Instance.InstanceId) && !SpawnMuscleView(Instance.InstanceId, OutReason)) { bSuccess = false; }
	}
	if (bDragging && DragExistingId.IsValid())
	{
		FAssemblyBoneInstance Instance;
		if (!BoundAssembly->GetBone(DragExistingId, Instance)) { CancelBoneDrag(); }
	}
	UpdateCorePlaceholder();
	RequestPhysicsSnapshot();
	OnViewsChanged.Broadcast();
	return bSuccess;
}

bool UAssemblyInteractionComponent::BeginBoneDrag(FName TypeId, FGuid ExistingId, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return false; }
	CancelBoneDrag();
	const TArray<FAssemblyBoneDefinition> Definitions = BoundAssembly->GetBoneDefinitions();
	const FAssemblyBoneDefinition* Definition = Definitions.FindByPredicate([TypeId](const FAssemblyBoneDefinition& Item) { return Item.TypeId == TypeId; });
	if (!Definition || (!ExistingId.IsValid() && BoundAssembly->GetRemainingQuantity(TypeId) <= 0))
	{
		OutReason = LOCTEXT("NoBoneAvailable", "骨头类型不存在或展示栏数量已用完。");
		return false;
	}
	FAssemblyBoneInstance Existing;
	if (ExistingId.IsValid())
	{
		ANormalBone* Bone = GetBoneActor(ExistingId);
		if (!BoundAssembly->GetBone(ExistingId, Existing) || Existing.TypeId != TypeId || !Bone
			|| Bone->GetBoneBody()->IsSimulatingPhysics() || BoundAssembly->GetBonePoseSource(ExistingId) != Bone->GetBoneBody())
		{
			OutReason = LOCTEXT("CannotMoveBone", "无法拖动此骨头：实例类型不匹配，或姿态已由物理程序接管。");
			return false;
		}
	}
	if (!BoneActorClass || !GetWorld()) { OutReason = LOCTEXT("NoPreviewClass", "请配置骨头预览 Actor 类。"); return false; }
	PreviewActor = GetWorld()->SpawnActorDeferred<ANormalBone>(BoneActorClass, FTransform::Identity, GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!PreviewActor) { OutReason = LOCTEXT("NoPreview", "无法创建骨头预览。"); return false; }
	PreviewActor->PreviewParameters = Definition->BoneParameters;
	PreviewActor->FinishSpawning(FTransform::Identity);
	PreviewActor->SetActorEnableCollision(false);
	if (!PreviewActor->GetBoneSprite()->GetSprite() || !PreviewActor->GetPhysicalStats().bValid)
	{
		PreviewActor->Destroy(); PreviewActor = nullptr;
		OutReason = LOCTEXT("InvalidPreview", "骨头预览 Sprite 或参数无效，请检查 BoneActorClass。");
		return false;
	}
	bDragging = true;
	bHasPointerPosition = ExistingId.IsValid();
	DragTypeId = TypeId;
	DragExistingId = ExistingId;
	if (ANormalBone* Bone = GetBoneActor(ExistingId))
	{
		bDraggedActorWasHidden = Bone->IsHidden();
		Bone->SetActorHiddenInGame(true);
	}
	const FVector2D Position = ExistingId.IsValid() ? Existing.LocalPosition : BoundAssembly->GetFreeSettings().CoreLocalPosition;
	DragPreview = BoundAssembly->ValidateFreeBonePlacement(TypeId, Position, ExistingId.IsValid() ? Existing.RotationDegrees : 0.0f, ExistingId);
	ApplyPreview();
	OnBoneDragPreviewChanged.Broadcast();
	return true;
}

void UAssemblyInteractionComponent::ApplyPreview()
{
	if (!IsValid(PreviewActor) || !BoundAssembly.IsValid()) { return; }
	PreviewActor->SetActorTransform(DragPreview.LocalVisualTransform * BoundAssembly->GetGridFrameTransform());
	PreviewActor->GetBoneSprite()->SetSpriteColor(DragPreview.bValid ? ValidPreviewColor : InvalidPreviewColor);
}

bool UAssemblyInteractionComponent::UpdateBoneDragFromMouse(APlayerController* PlayerController, FText& OutReason)
{
	FVector Origin, Direction;
	FVector2D LocalPosition;
	if (!CanEdit(OutReason)) { return false; }
	if (!IsValid(PlayerController) || !PlayerController->DeprojectMousePositionToWorld(Origin, Direction)
		|| !BoundAssembly->RayToAssemblyPlane(Origin, Direction, LocalPosition))
	{
		OutReason = LOCTEXT("MouseOffPlane", "鼠标无法投射到当前组装平面。");
		bHasPointerPosition = false;
		if (bDragging) { DragPreview.bValid = false; DragPreview.Reason = OutReason; ApplyPreview(); OnBoneDragPreviewChanged.Broadcast(); }
		return false;
	}
	return UpdateBoneDragAtLocalPosition(LocalPosition, OutReason);
}

bool UAssemblyInteractionComponent::UpdateBoneDragAtLocalPosition(FVector2D LocalPosition, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return false; }
	if (!bDragging) { OutReason = LOCTEXT("NoActiveDrag", "请先从组件展示栏开始拖动骨头。"); return false; }
	bHasPointerPosition = FMath::IsFinite(LocalPosition.X) && FMath::IsFinite(LocalPosition.Y);
	DragPreview = BoundAssembly->ValidateFreeBonePlacement(DragTypeId, LocalPosition, DragPreview.RotationDegrees, DragExistingId);
	OutReason = DragPreview.Reason;
	ApplyPreview();
	OnBoneDragPreviewChanged.Broadcast();
	return DragPreview.bValid;
}

bool UAssemblyInteractionComponent::RotateBoneDrag(float DeltaDegrees, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return false; }
	if (!bDragging) { OutReason = LOCTEXT("NoRotateDrag", "请先开始拖动骨头再旋转。"); return false; }
	if (!FMath::IsFinite(DeltaDegrees)) { OutReason = LOCTEXT("InvalidAngle", "旋转角度必须是有限值。"); return false; }
	DragPreview = BoundAssembly->ValidateFreeBonePlacement(DragTypeId, DragPreview.LocalPosition, DragPreview.RotationDegrees + DeltaDegrees, DragExistingId);
	OutReason = DragPreview.Reason;
	ApplyPreview();
	OnBoneDragPreviewChanged.Broadcast();
	return DragPreview.bValid;
}

bool UAssemblyInteractionComponent::CommitBoneDrag(FGuid& OutInstanceId, FText& OutReason)
{
	OutInstanceId.Invalidate();
	if (!CanEdit(OutReason) || !bDragging) { CancelBoneDrag(); return false; }
	// A failed mouse deprojection must not commit the previous valid cursor position.
	if (!bHasPointerPosition || !DragPreview.bValid) { OutReason = DragPreview.Reason; CancelBoneDrag(); return false; }
	bool bSuccess;
	{
		TGuardValue<bool> Guard(bSuppressSynchronization, true);
		if (DragExistingId.IsValid())
		{
			bSuccess = BoundAssembly->TryMoveFreeBone(DragExistingId, DragPreview.LocalPosition, DragPreview.RotationDegrees, OutReason);
			if (bSuccess) { OutInstanceId = DragExistingId; }
		}
		else
		{
			bSuccess = BoundAssembly->TryInstallFreeBone(DragTypeId, DragPreview.LocalPosition, DragPreview.RotationDegrees, OutInstanceId, OutReason);
			if (bSuccess && !SpawnBoneView(OutInstanceId, OutReason))
			{
				BoundAssembly->RollbackNewInstance(OutInstanceId);
				OutInstanceId.Invalidate();
				bSuccess = false;
			}
		}
	}
	CancelBoneDrag();
	FText SyncReason;
	SynchronizeViews(SyncReason);
	return bSuccess;
}

void UAssemblyInteractionComponent::CancelBoneDrag()
{
	const bool bWasDragging = bDragging;
	if (ANormalBone* Bone = GetBoneActor(DragExistingId)) { Bone->SetActorHiddenInGame(bDraggedActorWasHidden); }
	if (IsValid(PreviewActor)) { PreviewActor->Destroy(); }
	PreviewActor = nullptr;
	bDragging = false;
	bHasPointerPosition = false;
	DragTypeId = NAME_None;
	DragExistingId.Invalidate();
	DragPreview = FAssemblyPlacementResult();
	if (bWasDragging) { OnBoneDragPreviewChanged.Broadcast(); }
}

bool UAssemblyInteractionComponent::InstallMuscleAtWorldPoints(FName TypeId, FVector WorldA, FVector WorldB, FGuid& OutInstanceId, FText& OutReason)
{
	OutInstanceId.Invalidate();
	if (!CanEdit(OutReason)) { return false; }
	FAssemblyMuscleEndpoint EndpointA, EndpointB;
	if (!BoundAssembly->FindBoneAtWorldPosition(WorldA, EndpointA) || !BoundAssembly->FindBoneAtWorldPosition(WorldB, EndpointB))
	{
		OutReason = LOCTEXT("MuscleOffBone", "肌肉的两端都必须落在骨头上，圆球核心不能作为肌肉连接点。");
		return false;
	}
	{
		TGuardValue<bool> Guard(bSuppressSynchronization, true);
		if (!BoundAssembly->TryInstallMuscle(TypeId, EndpointA, EndpointB, OutInstanceId, OutReason)) { return false; }
		if (!SpawnMuscleView(OutInstanceId, OutReason))
		{
			BoundAssembly->RollbackNewInstance(OutInstanceId);
			OutInstanceId.Invalidate();
			return false;
		}
	}
	FText SyncReason;
	SynchronizeViews(SyncReason);
	return true;
}

bool UAssemblyInteractionComponent::MoveMuscleAtWorldPoints(FGuid InstanceId, FVector WorldA, FVector WorldB, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return false; }
	FAssemblyMuscleEndpoint EndpointA, EndpointB;
	if (!BoundAssembly->FindBoneAtWorldPosition(WorldA, EndpointA) || !BoundAssembly->FindBoneAtWorldPosition(WorldB, EndpointB))
	{
		OutReason = LOCTEXT("MoveMuscleOffBone", "移动失败：肌肉的两端都必须落在骨头上。");
		return false;
	}
	return BoundAssembly->TryMoveMuscle(InstanceId, EndpointA, EndpointB, OutReason);
}

bool UAssemblyInteractionComponent::RemoveInstance(FGuid InstanceId, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return false; }
	return BoundAssembly->TryRemoveInstance(InstanceId, OutReason);
}

void UAssemblyInteractionComponent::RequestPhysicsSnapshot()
{
	if (BoundBridge.IsValid()) { BoundBridge->RequestPhysicsRebuild(); }
}

void UAssemblyInteractionComponent::HandleAssemblyChanged()
{
	if (bSuppressSynchronization) { return; }
	FText Reason;
	if (!SynchronizeViews(Reason)) { OnInteractionMessage.Broadcast(Reason); }
}

void UAssemblyInteractionComponent::DestroyViews()
{
	for (const auto& Item : MuscleViews) { if (IsValid(Item.Value)) { Item.Value->ClearAssemblyBinding(); Item.Value->Destroy(); } }
	MuscleViews.Reset();
	for (const auto& Item : BoneViews) { if (IsValid(Item.Value)) { Item.Value->ClearAssemblyBinding(); Item.Value->Destroy(); } }
	BoneViews.Reset();
}

void UAssemblyInteractionComponent::HandleAssemblyReset()
{
	CancelBoneDrag();
	DestroyViews();
	if (GetOwner() && !GetOwner()->IsActorBeingDestroyed())
	{
		const UPrimitiveComponent* RootBody = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
		if ((!RootBody || !RootBody->IsSimulatingPhysics()) && (!GetCoreBody() || !GetCoreBody()->IsSimulatingPhysics()))
		{
			GetOwner()->SetActorTransform(SpawnOwnerTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	UpdateCorePlaceholder();
	RequestPhysicsSnapshot();
	OnViewsChanged.Broadcast();
}

void UAssemblyInteractionComponent::HandlePhaseChanged(EAssemblyPhase Phase)
{
	if (Phase != EAssemblyPhase::Assembly) { CancelBoneDrag(); }
}

void UAssemblyInteractionComponent::ClearInteraction()
{
	CancelBoneDrag();
	if (BoundAssembly.IsValid())
	{
		BoundAssembly->OnAssemblyChanged.RemoveDynamic(this, &UAssemblyInteractionComponent::HandleAssemblyChanged);
		BoundAssembly->OnAssemblyReset.RemoveDynamic(this, &UAssemblyInteractionComponent::HandleAssemblyReset);
		BoundAssembly->OnPhaseChanged.RemoveDynamic(this, &UAssemblyInteractionComponent::HandlePhaseChanged);
	}
	DestroyViews();
	if (bOwnsBridge && BoundBridge.IsValid()) { BoundBridge->ClearBridge(); BoundBridge->DestroyComponent(); }
	else if (BoundBridge.IsValid())
	{
		if (CreatedCoreBody) { BoundBridge->SetCoreBody(IsValid(ExistingCoreBody) ? ExistingCoreBody.Get() : nullptr); }
		BoundBridge->RequestPhysicsRebuild();
	}
	BoundBridge.Reset();
	bOwnsBridge = false;
	if (IsValid(CreatedCoreVisual)) { CreatedCoreVisual->DestroyComponent(); }
	CreatedCoreVisual = nullptr;
	if (IsValid(CreatedCoreBody)) { CreatedCoreBody->DestroyComponent(); }
	CreatedCoreBody = nullptr;
	BoundAssembly.Reset();
}

void UAssemblyInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearInteraction();
	Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
