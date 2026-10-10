#include "Assembly/AssemblyComponent.h"
#include "AssemblyRules.h"
#include "AssemblyFreeRules.h"

#define LOCTEXT_NAMESPACE "AssemblyComponent"

UAssemblyComponent::UAssemblyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	FAssemblyBoneDefinition Bone;
	Bone.DisplayName = LOCTEXT("DefaultBone", "骨头");
	DefaultBones.Add(Bone);
	FAssemblyMuscleDefinition Muscle;
	Muscle.DisplayName = LOCTEXT("DefaultMuscle", "肌肉");
	DefaultMuscles.Add(Muscle);
}

void UAssemblyComponent::BeginPlay()
{
	Super::BeginPlay();
	bEndingPlay = false;
	if (bAutoEnterAssembly && !bInitialized)
	{
		FText Reason;
		if (!EnterAssembly(Reason))
		{
			UE_LOG(LogTemp, Error, TEXT("Assembly initialization failed on %s: %s"), *GetPathName(), *Reason.ToString());
		}
	}
}

void UAssemblyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearInputState();
	bEndingPlay = true;
	InstalledBones.Reset();
	InstalledMuscles.Reset();
	InstalledJoints.Reset();
	BonePoseSources.Reset();
	RemainingQuantities.Reset();
	SelectedKey = EAssemblyMuscleKey::None;
	Phase = EAssemblyPhase::Assembly;
	{
		TGuardValue<bool> Guard(bNotifying, true);
		OnAssemblyReset.Broadcast();
	}
	bInitialized = false;
	Super::EndPlay(EndPlayReason);
}

bool UAssemblyComponent::CanMutate(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (bEndingPlay)
	{
		OutReason = LOCTEXT("EndedAssembly", "组装组件已结束生命周期。");
		return false;
	}
	if (bNotifying)
	{
		OutReason = LOCTEXT("ReentrantEdit", "不能在组装通知回调中再次修改规则数据；请在回调结束后发起操作。");
		return false;
	}
	return true;
}

bool UAssemblyComponent::CanEdit(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!bInitialized)
	{
		OutReason = LOCTEXT("NotInitialized", "请先初始化组装组件。");
		return false;
	}
	if (Phase != EAssemblyPhase::Assembly)
	{
		OutReason = LOCTEXT("NotAssembling", "只能在组装阶段修改组件。");
		return false;
	}
	return true;
}

bool UAssemblyComponent::ConfigureAssembly(const FAssemblyGridSettings& Grid,
	const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason)
{
	if (!CanMutate(OutReason) || !AssemblyRules::ValidateConfiguration(Grid, Bones, Muscles, OutReason))
	{
		return false;
	}
	ClearInputState();
	RoundGrid = Grid;
	RoundPlacementMode = EAssemblyPlacementMode::Grid;
	RoundBoneDefinitions = Bones;
	RoundMuscleDefinitions = Muscles;
	bInitialized = true;
	ResetRound();
	return true;
}

bool UAssemblyComponent::ConfigureFreeAssembly(const FAssemblyFreeSettings& Settings,
	const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason)
{
	if (!CanMutate(OutReason) || !AssemblyFreeRules::ValidateConfiguration(Settings, Bones, Muscles, OutReason)) { return false; }
	ClearInputState();
	RoundGrid = DefaultGrid;
	RoundFreeSettings = Settings;
	RoundPlacementMode = EAssemblyPlacementMode::Free;
	RoundBoneDefinitions = Bones;
	RoundMuscleDefinitions = Muscles;
	bInitialized = true;
	ResetRound();
	return true;
}

bool UAssemblyComponent::EnterAssembly(FText& OutReason)
{
	if (!CanMutate(OutReason))
	{
		return false;
	}
	if (!bInitialized)
	{
		if (DefaultPlacementMode == EAssemblyPlacementMode::Free)
		{
			return ConfigureFreeAssembly(DefaultFreeSettings, DefaultBones, DefaultMuscles, OutReason);
		}
		return ConfigureAssembly(DefaultGrid, DefaultBones, DefaultMuscles, OutReason);
	}
	ResetRound();
	return true;
}

bool UAssemblyComponent::RestartAssembly(FText& OutReason)
{
	return EnterAssembly(OutReason);
}

void UAssemblyComponent::ResetRound()
{
	ClearInputState();
	InstalledBones.Reset();
	InstalledMuscles.Reset();
	InstalledJoints.Reset();
	BonePoseSources.Reset();
	RemainingQuantities.Reset();
	for (const FAssemblyBoneDefinition& Bone : RoundBoneDefinitions)
	{
		RemainingQuantities.Add(Bone.TypeId, Bone.InitialQuantity);
	}
	for (const FAssemblyMuscleDefinition& Muscle : RoundMuscleDefinitions)
	{
		RemainingQuantities.Add(Muscle.TypeId, Muscle.InitialQuantity);
	}
	SelectedKey = EAssemblyMuscleKey::None;
	Phase = EAssemblyPhase::Assembly;
	{
		TGuardValue<bool> Guard(bNotifying, true);
		OnAssemblyReset.Broadcast();
	}
	NotifyPhase();
	NotifyChanged();
}

bool UAssemblyComponent::TryStartPlaying(FText& OutReason)
{
	if (!CanMutate(OutReason))
	{
		return false;
	}
	const FAssemblyValidationResult Validation = ValidateAssembly();
	OutReason = Validation.Reason;
	if (!Validation.bValid)
	{
		return false;
	}
	if (Phase == EAssemblyPhase::Playing)
	{
		return true;
	}
	// Space may stay held across this transition; only muscle input becomes available.
	Phase = EAssemblyPhase::Playing;
	NotifyPhase();
	return true;
}

FAssemblyGridSettings UAssemblyComponent::GetGridSettings() const
{
	return bInitialized ? RoundGrid : DefaultGrid;
}

TArray<FAssemblyBoneDefinition> UAssemblyComponent::GetBoneDefinitions() const
{
	return bInitialized ? RoundBoneDefinitions : DefaultBones;
}

TArray<FAssemblyMuscleDefinition> UAssemblyComponent::GetMuscleDefinitions() const
{
	return bInitialized ? RoundMuscleDefinitions : DefaultMuscles;
}

int32 UAssemblyComponent::GetRemainingQuantity(FName TypeId) const
{
	const int32* Quantity = RemainingQuantities.Find(TypeId);
	return Quantity ? *Quantity : 0;
}

const FAssemblyBoneDefinition* UAssemblyComponent::FindBoneDefinition(FName TypeId) const
{
	return RoundBoneDefinitions.FindByPredicate([TypeId](const FAssemblyBoneDefinition& Definition) { return Definition.TypeId == TypeId; });
}

const FAssemblyMuscleDefinition* UAssemblyComponent::FindMuscleDefinition(FName TypeId) const
{
	return RoundMuscleDefinitions.FindByPredicate([TypeId](const FAssemblyMuscleDefinition& Definition) { return Definition.TypeId == TypeId; });
}

const FAssemblyBoneInstance* UAssemblyComponent::FindBone(FGuid Id) const
{
	return InstalledBones.FindByPredicate([Id](const FAssemblyBoneInstance& Bone) { return Bone.InstanceId == Id; });
}

const FAssemblyMuscleInstance* UAssemblyComponent::FindMuscle(FGuid Id) const
{
	return InstalledMuscles.FindByPredicate([Id](const FAssemblyMuscleInstance& Muscle) { return Muscle.InstanceId == Id; });
}

bool UAssemblyComponent::GetBone(FGuid InstanceId, FAssemblyBoneInstance& OutBone) const
{
	const FAssemblyBoneInstance* Bone = FindBone(InstanceId);
	OutBone = Bone ? *Bone : FAssemblyBoneInstance();
	return Bone != nullptr;
}

bool UAssemblyComponent::GetMuscle(FGuid InstanceId, FAssemblyMuscleInstance& OutMuscle) const
{
	const FAssemblyMuscleInstance* Muscle = FindMuscle(InstanceId);
	OutMuscle = Muscle ? *Muscle : FAssemblyMuscleInstance();
	return Muscle != nullptr;
}

bool UAssemblyComponent::SelectMuscleKey(EAssemblyMuscleKey Key, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	if (!AssemblyRules::IsValidKey(Key, true))
	{
		OutReason = LOCTEXT("InvalidKey", "请选择 W、A、S 或 D。");
		return false;
	}
	if (SelectedKey != Key)
	{
		SelectedKey = Key;
		NotifyChanged();
	}
	return true;
}

FAssemblyPlacementResult UAssemblyComponent::ValidateBonePlacement(FName TypeId, FIntPoint AnchorCell,
	int32 QuarterTurns, FGuid IgnoreId) const
{
	FAssemblyPlacementResult Result;
	if (GetPlacementMode() != EAssemblyPlacementMode::Grid)
	{
		Result.Reason = LOCTEXT("UseFreePlacement", "自由组装请使用 Validate/Try Install Free Bone，位置和角度不经过格坐标。");
		return Result;
	}
	if (!CanEdit(Result.Reason))
	{
		return Result;
	}
	const FAssemblyBoneDefinition* Definition = FindBoneDefinition(TypeId);
	if (!Definition)
	{
		Result.Reason = LOCTEXT("UnknownBone", "本关卡没有这种骨头。");
		return Result;
	}
	Result = AssemblyRules::MakePlacement(RoundGrid, Definition->Footprint, AnchorCell, QuarterTurns);
	if (!Result.Reason.IsEmpty())
	{
		return Result;
	}
	if (IgnoreId.IsValid())
	{
		const FAssemblyBoneInstance* Existing = FindBone(IgnoreId);
		if (!Existing || Existing->TypeId != TypeId)
		{
			Result.Reason = LOCTEXT("InvalidMove", "找不到要移动的同类型骨头。");
			return Result;
		}
	}
	else if (GetRemainingQuantity(TypeId) <= 0)
	{
		Result.Reason = LOCTEXT("NoStock", "该组件的剩余数量不足。");
		return Result;
	}
	Result.bValid = AssemblyRules::ValidateLayout(RoundGrid, InstalledBones, &Result.OccupiedCells, IgnoreId, Result.Reason);
	return Result;
}

bool UAssemblyComponent::TryInstallBone(FName TypeId, FIntPoint AnchorCell, int32 QuarterTurns,
	FGuid& OutInstanceId, FText& OutReason)
{
	OutInstanceId.Invalidate();
	if (!CanMutate(OutReason))
	{
		return false;
	}
	const FAssemblyPlacementResult Result = ValidateBonePlacement(TypeId, AnchorCell, QuarterTurns, FGuid());
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	FAssemblyBoneInstance Bone;
	Bone.InstanceId = FGuid::NewGuid();
	Bone.TypeId = TypeId;
	Bone.AnchorCell = Result.AnchorCell;
	Bone.QuarterTurns = Result.QuarterTurns;
	Bone.OccupiedCells = Result.OccupiedCells;
	Bone.LocalVisualTransform = Result.LocalVisualTransform;
	InstalledBones.Add(Bone);
	--RemainingQuantities.FindChecked(TypeId);
	OutInstanceId = Bone.InstanceId;
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TryMoveBone(FGuid InstanceId, FIntPoint AnchorCell, int32 QuarterTurns, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	FAssemblyBoneInstance* Bone = InstalledBones.FindByPredicate([InstanceId](const FAssemblyBoneInstance& B) { return B.InstanceId == InstanceId; });
	if (!Bone)
	{
		OutReason = LOCTEXT("MissingBone", "找不到这块骨头。");
		return false;
	}
	const FAssemblyPlacementResult Result = ValidateBonePlacement(Bone->TypeId, AnchorCell, QuarterTurns, InstanceId);
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	if (Bone->AnchorCell != Result.AnchorCell || Bone->QuarterTurns != Result.QuarterTurns)
	{
		Bone->AnchorCell = Result.AnchorCell;
		Bone->QuarterTurns = Result.QuarterTurns;
		Bone->OccupiedCells = Result.OccupiedCells;
		Bone->LocalVisualTransform = Result.LocalVisualTransform;
		NotifyChanged();
	}
	return true;
}

bool UAssemblyComponent::ValidateMuscleEndpoints(const FAssemblyMuscleEndpoint& A, const FAssemblyMuscleEndpoint& B,
	EAssemblyMuscleKey Key, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!AssemblyRules::IsValidKey(Key))
	{
		OutReason = LOCTEXT("SelectKeyFirst", "安装肌肉前请先选择 W、A、S 或 D。");
		return false;
	}
	for (const FAssemblyMuscleEndpoint* Endpoint : { &A, &B })
	{
		const FAssemblyBoneInstance* Bone = FindBone(Endpoint->BoneId);
		const FAssemblyBoneDefinition* Definition = Bone ? FindBoneDefinition(Bone->TypeId) : nullptr;
		if (!Definition)
		{
			OutReason = LOCTEXT("MissingEndpointBone", "肌肉两端必须连接已安装的骨头；核心不能作为连接点。");
			return false;
		}
		const bool bOnBone = RoundPlacementMode == EAssemblyPlacementMode::Free
			? AssemblyFreeRules::IsPointOnBone(Definition->BoneParameters, Endpoint->BoneLocalPoint)
			: AssemblyRules::IsPointOnBone(Definition->Footprint, RoundGrid.CellSize, Endpoint->BoneLocalPoint);
		if (!bOnBone)
		{
			OutReason = LOCTEXT("PointOutsideBone", "肌肉连接点必须位于对应骨头的实际轮廓内。");
			return false;
		}
	}
	if (A.BoneId == B.BoneId && A.BoneLocalPoint.Equals(B.BoneLocalPoint, UE_KINDA_SMALL_NUMBER))
	{
		OutReason = LOCTEXT("SameEndpoint", "允许连接同一块骨头，但必须选择两个不同的位置。");
		return false;
	}
	return true;
}

FAssemblyValidationResult UAssemblyComponent::ValidateMusclePlacement(FName TypeId,
	const FAssemblyMuscleEndpoint& EndpointA, const FAssemblyMuscleEndpoint& EndpointB,
	EAssemblyMuscleKey Key, FGuid IgnoreId) const
{
	FAssemblyValidationResult Result;
	if (!CanEdit(Result.Reason))
	{
		return Result;
	}
	if (!FindMuscleDefinition(TypeId))
	{
		Result.Reason = LOCTEXT("UnknownMuscle", "本关卡没有这种肌肉。");
		return Result;
	}
	if (IgnoreId.IsValid())
	{
		const FAssemblyMuscleInstance* Existing = FindMuscle(IgnoreId);
		if (!Existing || Existing->TypeId != TypeId)
		{
			Result.Reason = LOCTEXT("InvalidMuscleMove", "找不到要调整的同类型肌肉。");
			return Result;
		}
	}
	else if (GetRemainingQuantity(TypeId) <= 0)
	{
		Result.Reason = LOCTEXT("NoStock", "该组件的剩余数量不足。");
		return Result;
	}
	Result.bValid = ValidateMuscleEndpoints(EndpointA, EndpointB, Key, Result.Reason);
	return Result;
}

bool UAssemblyComponent::TryInstallMuscle(FName TypeId, const FAssemblyMuscleEndpoint& EndpointA,
	const FAssemblyMuscleEndpoint& EndpointB, FGuid& OutInstanceId, FText& OutReason)
{
	OutInstanceId.Invalidate();
	if (!CanMutate(OutReason))
	{
		return false;
	}
	const FAssemblyValidationResult Result = ValidateMusclePlacement(TypeId, EndpointA, EndpointB, SelectedKey, FGuid());
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	FAssemblyMuscleInstance Muscle;
	Muscle.InstanceId = FGuid::NewGuid();
	Muscle.TypeId = TypeId;
	Muscle.EndpointA = EndpointA;
	Muscle.EndpointB = EndpointB;
	Muscle.BindingKey = SelectedKey;
	InstalledMuscles.Add(Muscle);
	--RemainingQuantities.FindChecked(TypeId);
	OutInstanceId = Muscle.InstanceId;
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TryMoveMuscle(FGuid InstanceId, const FAssemblyMuscleEndpoint& EndpointA,
	const FAssemblyMuscleEndpoint& EndpointB, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	FAssemblyMuscleInstance* Muscle = InstalledMuscles.FindByPredicate([InstanceId](const FAssemblyMuscleInstance& M) { return M.InstanceId == InstanceId; });
	if (!Muscle)
	{
		OutReason = LOCTEXT("MissingMuscle", "找不到这条肌肉。");
		return false;
	}
	const FAssemblyValidationResult Result = ValidateMusclePlacement(Muscle->TypeId, EndpointA, EndpointB, Muscle->BindingKey, InstanceId);
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	Muscle->EndpointA = EndpointA;
	Muscle->EndpointB = EndpointB;
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TrySetMuscleKey(FGuid InstanceId, EAssemblyMuscleKey Key, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	FAssemblyMuscleInstance* Muscle = InstalledMuscles.FindByPredicate([InstanceId](const FAssemblyMuscleInstance& M) { return M.InstanceId == InstanceId; });
	if (!Muscle)
	{
		OutReason = LOCTEXT("MissingMuscle", "找不到这条肌肉。");
		return false;
	}
	if (!ValidateMuscleEndpoints(Muscle->EndpointA, Muscle->EndpointB, Key, OutReason))
	{
		return false;
	}
	if (Muscle->BindingKey != Key)
	{
		Muscle->BindingKey = Key;
		NotifyChanged();
	}
	return true;
}

bool UAssemblyComponent::TryRemoveInstance(FGuid InstanceId, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	const int32 MuscleIndex = InstalledMuscles.IndexOfByPredicate([InstanceId](const FAssemblyMuscleInstance& M) { return M.InstanceId == InstanceId; });
	if (MuscleIndex != INDEX_NONE)
	{
		if (RoundPlacementMode == EAssemblyPlacementMode::Free && RoundFreeSettings.bRejectDisconnectingRemoval)
		{
			TArray<FAssemblyMuscleInstance> Remaining = InstalledMuscles;
			Remaining.RemoveAt(MuscleIndex);
			if (!PreservesFreeCoreConnections(InstalledBones, Remaining, InstalledJoints))
			{
				OutReason = LOCTEXT("DisconnectingMuscle", "拆下这条肌肉会使其他骨头与核心断开。");
				return false;
			}
		}
		++RemainingQuantities.FindChecked(InstalledMuscles[MuscleIndex].TypeId);
		InstalledMuscles.RemoveAt(MuscleIndex);
		NotifyChanged();
		return true;
	}
	const int32 BoneIndex = InstalledBones.IndexOfByPredicate([InstanceId](const FAssemblyBoneInstance& B) { return B.InstanceId == InstanceId; });
	if (BoneIndex == INDEX_NONE)
	{
		OutReason = LOCTEXT("MissingInstance", "找不到该安装实例；核心不可拆卸。");
		return false;
	}
	if (InstalledMuscles.ContainsByPredicate([InstanceId](const FAssemblyMuscleInstance& M)
		{ return M.EndpointA.BoneId == InstanceId || M.EndpointB.BoneId == InstanceId; }))
	{
		OutReason = LOCTEXT("BoneHasMuscles", "请先拆下连接在这块骨头上的肌肉。");
		return false;
	}
	if (RoundPlacementMode == EAssemblyPlacementMode::Grid
		&& !AssemblyRules::ValidateLayout(RoundGrid, InstalledBones, nullptr, InstanceId, OutReason))
	{
		return false;
	}
	if (RoundPlacementMode == EAssemblyPlacementMode::Free && RoundFreeSettings.bRejectDisconnectingRemoval)
	{
		TArray<FAssemblyBoneInstance> RemainingBones = InstalledBones;
		RemainingBones.RemoveAt(BoneIndex);
		TArray<FAssemblyJointInstance> RemainingJoints = InstalledJoints;
		RemainingJoints.RemoveAll([InstanceId](const FAssemblyJointInstance& J) { return J.BoneA == InstanceId || J.BoneB == InstanceId; });
		if (!PreservesFreeCoreConnections(RemainingBones, InstalledMuscles, RemainingJoints, InstanceId))
		{
			OutReason = LOCTEXT("DisconnectingBone", "拆下这块骨头会使其他骨头与核心断开。");
			return false;
		}
	}
	++RemainingQuantities.FindChecked(InstalledBones[BoneIndex].TypeId);
	InstalledBones.RemoveAt(BoneIndex);
	BonePoseSources.Remove(InstanceId);
	if (RoundPlacementMode == EAssemblyPlacementMode::Free) { RebuildFreeJoints(); }
	NotifyChanged();
	return true;
}

FAssemblyValidationResult UAssemblyComponent::ValidateAssembly() const
{
	FAssemblyValidationResult Result;
	if (!bInitialized)
	{
		Result.Reason = LOCTEXT("NotInitialized", "请先初始化组装组件。");
		return Result;
	}
	const bool bConfigValid = RoundPlacementMode == EAssemblyPlacementMode::Free
		? AssemblyFreeRules::ValidateConfiguration(RoundFreeSettings, RoundBoneDefinitions, RoundMuscleDefinitions, Result.Reason)
		: AssemblyRules::ValidateConfiguration(RoundGrid, RoundBoneDefinitions, RoundMuscleDefinitions, Result.Reason);
	if (!bConfigValid || (RoundPlacementMode == EAssemblyPlacementMode::Grid
		&& !AssemblyRules::ValidateLayout(RoundGrid, InstalledBones, nullptr, FGuid(), Result.Reason)))
	{
		return Result;
	}
	TSet<FGuid> Ids;
	TMap<FName, int32> Used;
	for (const FAssemblyBoneInstance& Bone : InstalledBones)
	{
		const FAssemblyBoneDefinition* Definition = FindBoneDefinition(Bone.TypeId);
		if (!Definition || !Bone.InstanceId.IsValid() || Ids.Contains(Bone.InstanceId))
		{
			Result.Reason = LOCTEXT("InvalidInstance", "安装实例标识或类型无效。");
			return Result;
		}
		const FAssemblyPlacementResult Expected = RoundPlacementMode == EAssemblyPlacementMode::Free
			? AssemblyFreeRules::MakePlacement(Bone.LocalPosition, Bone.RotationDegrees)
			: AssemblyRules::MakePlacement(RoundGrid, Definition->Footprint, Bone.AnchorCell, Bone.QuarterTurns);
		if (!Expected.Reason.IsEmpty() || (RoundPlacementMode == EAssemblyPlacementMode::Grid
			&& (Expected.QuarterTurns != Bone.QuarterTurns || Expected.OccupiedCells != Bone.OccupiedCells))
			|| !Expected.LocalVisualTransform.Equals(Bone.LocalVisualTransform))
		{
			Result.Reason = LOCTEXT("InconsistentGeometry", "骨头旋转、占格和显示变换不一致。");
			return Result;
		}
		if (RoundPlacementMode == EAssemblyPlacementMode::Free
			&& !AssemblyFreeRules::ValidateGeometry(RoundFreeSettings, RoundBoneDefinitions, InstalledBones, Bone.TypeId, Expected, Bone.InstanceId, Result.Reason))
		{
			return Result;
		}
		Ids.Add(Bone.InstanceId);
		++Used.FindOrAdd(Bone.TypeId);
	}
	for (const FAssemblyMuscleInstance& Muscle : InstalledMuscles)
	{
		if (!FindMuscleDefinition(Muscle.TypeId) || !Muscle.InstanceId.IsValid() || Ids.Contains(Muscle.InstanceId))
		{
			Result.Reason = LOCTEXT("InvalidInstance", "安装实例标识或类型无效。");
			return Result;
		}
		if (!ValidateMuscleEndpoints(Muscle.EndpointA, Muscle.EndpointB, Muscle.BindingKey, Result.Reason))
		{
			return Result;
		}
		if (Phase == EAssemblyPhase::Assembly && Muscle.bContracting)
		{
			Result.Reason = LOCTEXT("AssemblyDrive", "组装阶段不能驱动肌肉。");
			return Result;
		}
		Ids.Add(Muscle.InstanceId);
		++Used.FindOrAdd(Muscle.TypeId);
	}
	for (const FAssemblyJointInstance& Joint : InstalledJoints)
	{
		const FAssemblyBoneInstance* B = FindBone(Joint.BoneB);
		const FAssemblyBoneInstance* A = Joint.BoneA.IsValid() ? FindBone(Joint.BoneA) : nullptr;
		const FAssemblyBoneDefinition* DB = B ? FindBoneDefinition(B->TypeId) : nullptr;
		const FAssemblyBoneDefinition* DA = A ? FindBoneDefinition(A->TypeId) : nullptr;
		if (!Joint.InstanceId.IsValid() || Ids.Contains(Joint.InstanceId) || !DB || (Joint.BoneA.IsValid() && !DA)
			|| Joint.BoneA == Joint.BoneB || !AssemblyFreeRules::IsJointKindValid(Joint.Kind)
			|| !AssemblyFreeRules::ValidateJointLimits(Joint.MinAngleDegrees, Joint.MaxAngleDegrees)
			|| !AssemblyFreeRules::IsPointOnBone(DB->BoneParameters, Joint.LocalAnchorB)
			|| (DA && !AssemblyFreeRules::IsPointOnBone(DA->BoneParameters, Joint.LocalAnchorA))
			|| (!DA && (!FMath::IsFinite(Joint.LocalAnchorA.X) || !FMath::IsFinite(Joint.LocalAnchorA.Y)
				|| !FMath::IsNearlyEqual(Joint.LocalAnchorA.Size(), static_cast<double>(RoundFreeSettings.CoreRadius), 0.001))))
		{
			Result.Reason = LOCTEXT("InvalidJoint", "关节标识、连接点、种类或限角无效。");
			return Result;
		}
		Ids.Add(Joint.InstanceId);
	}
	auto CheckQuantity = [this, &Used](FName TypeId, int32 Initial)
	{
		const int32* Remaining = RemainingQuantities.Find(TypeId);
		return Remaining && *Remaining >= 0 && static_cast<int64>(*Remaining) + Used.FindRef(TypeId) == Initial;
	};
	if (RemainingQuantities.Num() != RoundBoneDefinitions.Num() + RoundMuscleDefinitions.Num()
		|| RoundBoneDefinitions.ContainsByPredicate([&CheckQuantity](const FAssemblyBoneDefinition& D) { return !CheckQuantity(D.TypeId, D.InitialQuantity); })
		|| RoundMuscleDefinitions.ContainsByPredicate([&CheckQuantity](const FAssemblyMuscleDefinition& D) { return !CheckQuantity(D.TypeId, D.InitialQuantity); }))
	{
		Result.Reason = LOCTEXT("InvalidInventory", "库存不一致：剩余数量加安装数量必须等于初始数量。");
		return Result;
	}
	if (RoundPlacementMode == EAssemblyPlacementMode::Free && Phase == EAssemblyPhase::Assembly && !IsStructureConnected())
	{
		Result.Reason = LOCTEXT("DisconnectedStructure", "开始操控前，每块骨头必须通过关节或肌肉连接到核心。");
		return Result;
	}
	Result.bValid = true;
	return Result;
}

bool UAssemblyComponent::SetMuscleInput(EAssemblyMuscleKey Key, bool bPressed)
{
	if (bNotifying || !CanControlMuscles() || !AssemblyRules::IsValidKey(Key))
	{
		return false;
	}
	TArray<FGuid> Changed;
	for (FAssemblyMuscleInstance& Muscle : InstalledMuscles)
	{
		if (Muscle.BindingKey == Key && Muscle.bContracting != bPressed)
		{
			Muscle.bContracting = bPressed;
			Changed.Add(Muscle.InstanceId);
		}
	}
	for (const FGuid Id : Changed)
	{
		RequestMuscleDrive(Id, bPressed);
	}
	return true;
}

bool UAssemblyComponent::SetCoreRotationInput(bool bHeld)
{
	if (bNotifying || !bInitialized)
	{
		return false;
	}
	if (bCoreRotationHeld != bHeld)
	{
		bCoreRotationHeld = bHeld;
		RequestCoreRotation(bHeld);
	}
	return true;
}

void UAssemblyComponent::ClearInputState()
{
	if (bNotifying)
	{
		return;
	}
	TArray<FGuid> Active;
	for (FAssemblyMuscleInstance& Muscle : InstalledMuscles)
	{
		if (Muscle.bContracting)
		{
			Muscle.bContracting = false;
			Active.Add(Muscle.InstanceId);
		}
	}
	const bool bWasRotating = bCoreRotationHeld;
	bCoreRotationHeld = false;
	for (const FGuid Id : Active)
	{
		RequestMuscleDrive(Id, false);
	}
	if (bWasRotating)
	{
		RequestCoreRotation(false);
	}
}

void UAssemblyComponent::NotifyChanged()
{
	TGuardValue<bool> Guard(bNotifying, true);
	OnAssemblyChanged.Broadcast();
}

void UAssemblyComponent::NotifyPhase()
{
	TGuardValue<bool> Guard(bNotifying, true);
	OnPhaseChanged.Broadcast(Phase);
}

void UAssemblyComponent::RequestMuscleDrive(FGuid Id, bool bContracting)
{
	TGuardValue<bool> Guard(bNotifying, true);
	OnMuscleDriveRequestedNative.Broadcast(Id, bContracting);
	OnMuscleDriveRequested.Broadcast(Id, bContracting);
}

void UAssemblyComponent::RequestCoreRotation(bool bHeld)
{
	TGuardValue<bool> Guard(bNotifying, true);
	OnCoreRotationRequestedNative.Broadcast(bHeld);
	OnCoreRotationRequested.Broadcast(bHeld);
}

#undef LOCTEXT_NAMESPACE
