#include "Assembly/AssemblyComponent.h"
#include "AssemblyFreeRules.h"
#include "AssemblyRules.h"
#include "Components/SceneComponent.h"

#define LOCTEXT_NAMESPACE "AssemblyFreeComponent"

FAssemblyPlacementResult UAssemblyComponent::ValidateJointPartPlacement(FName TypeId, FVector2D Position,
	float Angle, FGuid IgnoreId) const
{
	const FAssemblyBoneDefinition* Definition = FindBoneDefinition(TypeId);
	if (!Definition || Definition->PartKind != EAssemblyRigidPartKind::JointPart)
	{
		FAssemblyPlacementResult Invalid;
		Invalid.Reason = LOCTEXT("NotJointPart", "请选择本关卡提供的独立关节零件。");
		return Invalid;
	}
	return ValidateFreeBonePlacement(TypeId, Position, Angle, IgnoreId);
}

bool UAssemblyComponent::TryInstallJointPart(FName TypeId, FVector2D Position, float Angle, FGuid& OutId, FText& OutReason)
{
	OutId.Invalidate();
	if (!CanMutate(OutReason)) { return false; }
	const FAssemblyPlacementResult Placement = ValidateJointPartPlacement(TypeId, Position, Angle, FGuid());
	OutReason = Placement.Reason;
	return Placement.bValid && TryInstallFreeBone(TypeId, Position, Angle, OutId, OutReason);
}

bool UAssemblyComponent::TryMoveJointPart(FGuid Id, FVector2D Position, float Angle, FText& OutReason)
{
	if (!CanMutate(OutReason)) { return false; }
	if (!IsJointPart(Id)) { OutReason = LOCTEXT("MissingJointPart", "找不到要移动的独立关节零件。"); return false; }
	return TryMoveFreeBone(Id, Position, Angle, OutReason);
}

bool UAssemblyComponent::RemoveJointPart(FGuid Id, FText& OutReason)
{
	if (!CanMutate(OutReason)) { return false; }
	if (!IsJointPart(Id)) { OutReason = LOCTEXT("MissingJointPart", "找不到要拆下的独立关节零件。"); return false; }
	return TryRemoveInstance(Id, OutReason);
}

bool UAssemblyComponent::CanEditFreeBone(FName TypeId, FGuid IgnoreId, FText& OutReason) const
{
	if (IsEndingPlay())
	{
		OutReason = LOCTEXT("EndingFreeAssembly", "组装组件正在结束当前关卡，无法继续编辑。");
		return false;
	}
	if (!CanEdit(OutReason)) { return false; }
	if (RoundPlacementMode != EAssemblyPlacementMode::Free)
	{
		OutReason = LOCTEXT("NotFree", "当前使用网格模式，请先 Configure Free Assembly。");
		return false;
	}
	if (!FindBoneDefinition(TypeId)) { OutReason = LOCTEXT("UnknownBone", "本关卡没有这种骨头。"); return false; }
	if (IgnoreId.IsValid())
	{
		const FAssemblyBoneInstance* Existing = FindBone(IgnoreId);
		if (!Existing || Existing->TypeId != TypeId) { OutReason = LOCTEXT("UnknownMove", "找不到要移动的同类型骨头。"); return false; }
	}
	else if (GetRemainingQuantity(TypeId) <= 0) { OutReason = LOCTEXT("NoStock", "该骨头的剩余数量不足。"); return false; }
	return true;
}

bool UAssemblyComponent::BuildEffectiveFreeBoneSnapshot(TArray<FAssemblyBoneInstance>& OutBones, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	OutBones.Reset();
	const FTransform Frame = GetGridFrameTransform();
	const FVector FrameScale = Frame.GetScale3D();
	if (Frame.ContainsNaN() || !Frame.GetRotation().IsNormalized()
		|| FMath::IsNearlyZero(FrameScale.X) || FMath::IsNearlyZero(FrameScale.Y) || FMath::IsNearlyZero(FrameScale.Z)
		|| (RoundFreeSettings.bEnableSurfaceSnapping && (FrameScale.X <= 0.0 || FrameScale.Z <= 0.0
			|| !FMath::IsNearlyEqual(FrameScale.X, FrameScale.Z, UE_KINDA_SMALL_NUMBER))))
	{
		OutReason = LOCTEXT("InvalidSurfaceFrame", "表面吸附需要有效的组装坐标：XZ平面必须采用相同的正缩放，不能使用剪切或拉伸坐标。");
		return false;
	}
	TArray<FAssemblyBoneInstance> Snapshot = InstalledBones;
	for (FAssemblyBoneInstance& Bone : Snapshot)
	{
		const TWeakObjectPtr<USceneComponent>* Source = BonePoseSources.Find(Bone.InstanceId);
		if (!Source) { continue; }
		FTransform WorldPose;
		if (!Source->IsValid() || !GetBoneWorldTransform(Bone.InstanceId, WorldPose))
		{
			OutReason = LOCTEXT("InvalidEffectiveBody", "已安装组件的实际姿态源已失效或不可逆；请恢复物理姿态源后继续组装。");
			return false;
		}
		const FTransform LocalPose = WorldPose.GetRelativeTransform(Frame);
		const FVector LocalX = LocalPose.GetRotation().RotateVector(FVector::XAxisVector);
		const FVector LocalZ = LocalPose.GetRotation().RotateVector(FVector::ZAxisVector);
		if (LocalPose.ContainsNaN() || !LocalPose.GetRotation().IsNormalized()
			|| !LocalPose.GetScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER)
			|| FMath::Abs(LocalX.Y) > UE_KINDA_SMALL_NUMBER || FMath::Abs(LocalZ.Y) > UE_KINDA_SMALL_NUMBER
			|| FMath::Abs(LocalPose.GetLocation().Y) > UE_KINDA_SMALL_NUMBER)
		{
			OutReason = LOCTEXT("UnrepresentableEffectiveBody", "实际组件姿态无法表示为当前XZ组装平面的刚体；请保持组件与坐标参考共面、相同缩放。");
			return false;
		}
		const FVector2D Position(LocalPose.GetLocation().X, LocalPose.GetLocation().Z);
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(-LocalX.Z, LocalX.X));
		const FAssemblyPlacementResult Placement = AssemblyFreeRules::MakePlacement(Position, Angle);
		if (!Placement.bValid || !Placement.LocalVisualTransform.Equals(LocalPose, UE_KINDA_SMALL_NUMBER))
		{
			OutReason = LOCTEXT("UnrepresentableSurfacePose", "实际组件姿态无法转换为组装位置和旋转角度。");
			return false;
		}
		Bone.LocalPosition = Placement.LocalPosition;
		Bone.RotationDegrees = Placement.RotationDegrees;
		Bone.LocalVisualTransform = Placement.LocalVisualTransform;
	}
	OutBones = MoveTemp(Snapshot);
	return true;
}

FAssemblyPlacementResult UAssemblyComponent::ValidateFreeBonePlacement(FName TypeId, FVector2D Position,
	float Angle, FGuid IgnoreId) const
{
	FAssemblyPlacementResult R;
	if (!CanEditFreeBone(TypeId, IgnoreId, R.Reason)) { return R; }
	TArray<FAssemblyBoneInstance> EffectiveBones;
	if (!BuildEffectiveFreeBoneSnapshot(EffectiveBones, R.Reason)) { return R; }
	R = AssemblyFreeRules::MakePlacement(Position, Angle);
	if (R.bValid) { R.bValid = AssemblyFreeRules::ValidateGeometry(RoundFreeSettings, RoundBoneDefinitions, EffectiveBones, TypeId, R, IgnoreId, R.Reason); }
	return R;
}

FAssemblyPlacementResult UAssemblyComponent::ResolveFreeBoneDragPlacement(FName TypeId, FVector2D RawPosition,
	float Angle, FGuid IgnoreId, const FAssemblySurfaceSnapState& PreviousSnap) const
{
	FAssemblyPlacementResult R;
	if (!CanEditFreeBone(TypeId, IgnoreId, R.Reason)) { return R; }
	TArray<FAssemblyBoneInstance> EffectiveBones;
	if (!BuildEffectiveFreeBoneSnapshot(EffectiveBones, R.Reason)) { return R; }
	// A raw pose may penetrate the target slightly. The resolver first finds legal surface contact,
	// then runs the same geometry validator used by the exact install/move transactions.
	return AssemblyFreeRules::ResolveSurfacePlacement(RoundFreeSettings, RoundBoneDefinitions,
		EffectiveBones, TypeId, RawPosition, Angle, IgnoreId, PreviousSnap);
}

bool UAssemblyComponent::TryInstallFreeBone(FName TypeId, FVector2D Position, float Angle, FGuid& OutId, FText& OutReason)
{
	OutId.Invalidate();
	if (!CanMutate(OutReason) || !CanEditFreeBone(TypeId, FGuid(), OutReason)) { return false; }
	TArray<FAssemblyBoneInstance> EffectiveBones;
	if (!BuildEffectiveFreeBoneSnapshot(EffectiveBones, OutReason)) { return false; }
	FAssemblyPlacementResult R = AssemblyFreeRules::MakePlacement(Position, Angle);
	if (R.bValid) R.bValid = AssemblyFreeRules::ValidateGeometry(RoundFreeSettings, RoundBoneDefinitions, EffectiveBones, TypeId, R, FGuid(), R.Reason);
	OutReason = R.Reason;
	if (!R.bValid) { return false; }
	FAssemblyBoneInstance Bone;
	Bone.InstanceId = FGuid::NewGuid(); Bone.TypeId = TypeId;
	Bone.LocalPosition = R.LocalPosition; Bone.RotationDegrees = R.RotationDegrees;
	Bone.LocalVisualTransform = R.LocalVisualTransform;
	EffectiveBones.Add(Bone);
	InstalledBones = MoveTemp(EffectiveBones);
	--RemainingQuantities.FindChecked(TypeId);
	OutId = Bone.InstanceId;
	RebuildFreeJoints(&InstalledBones);
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TryMoveFreeBone(FGuid Id, FVector2D Position, float Angle, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason)) { return false; }
	const int32 Index = InstalledBones.IndexOfByPredicate([Id](const FAssemblyBoneInstance& B) { return B.InstanceId == Id; });
	if (Index == INDEX_NONE) { OutReason = LOCTEXT("UnknownMove", "找不到要移动的骨头。"); return false; }
	const FName TypeId = InstalledBones[Index].TypeId;
	if (!CanEditFreeBone(TypeId, Id, OutReason)) { return false; }
	TArray<FAssemblyBoneInstance> EffectiveBones;
	if (!BuildEffectiveFreeBoneSnapshot(EffectiveBones, OutReason)) { return false; }
	FAssemblyPlacementResult R = AssemblyFreeRules::MakePlacement(Position, Angle);
	if (R.bValid) R.bValid = AssemblyFreeRules::ValidateGeometry(RoundFreeSettings, RoundBoneDefinitions, EffectiveBones, TypeId, R, Id, R.Reason);
	OutReason = R.Reason;
	if (!R.bValid) { return false; }
	FAssemblyBoneInstance& B = EffectiveBones[Index];
	const bool bUnchanged = B.LocalPosition.Equals(R.LocalPosition, UE_KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(B.RotationDegrees, R.RotationDegrees, UE_KINDA_SMALL_NUMBER)
		&& !EffectiveBones.ContainsByPredicate([this](const FAssemblyBoneInstance& Part)
		{
			const FAssemblyBoneInstance* Stored = FindBone(Part.InstanceId);
			return !Stored || !Part.LocalVisualTransform.Equals(Stored->LocalVisualTransform, UE_KINDA_SMALL_NUMBER);
		});
	if (bUnchanged) { return true; }
	B.LocalPosition = R.LocalPosition; B.RotationDegrees = R.RotationDegrees; B.LocalVisualTransform = R.LocalVisualTransform;
	InstalledBones = MoveTemp(EffectiveBones);
	// The moved view still has its old physical pose until NotifyChanged. Use the committed snapshot.
	RebuildFreeJoints(&InstalledBones);
	NotifyChanged();
	return true;
}

void UAssemblyComponent::RebuildFreeJoints(const TArray<FAssemblyBoneInstance>* EffectiveBones)
{
	if (EffectiveBones)
	{
		InstalledJoints = AssemblyFreeRules::BuildJoints(RoundFreeSettings, RoundBoneDefinitions, *EffectiveBones, InstalledJoints);
		return;
	}
	TArray<FAssemblyBoneInstance> Snapshot;
	FText Reason;
	if (!BuildEffectiveFreeBoneSnapshot(Snapshot, Reason)) { return; }
	InstalledBones = MoveTemp(Snapshot);
	InstalledJoints = AssemblyFreeRules::BuildJoints(RoundFreeSettings, RoundBoneDefinitions, InstalledBones, InstalledJoints);
}

TSet<FGuid> UAssemblyComponent::GetFreeCoreReachable(const TArray<FAssemblyBoneInstance>& Bones,
	const TArray<FAssemblyMuscleInstance>& Muscles, const TArray<FAssemblyJointInstance>& Joints) const
{
	TSet<FGuid> ValidBones, Reached;
	for (const FAssemblyBoneInstance& B : Bones) { ValidBones.Add(B.InstanceId); }
	Reached.Add(FGuid());
	bool bAdded;
	do
	{
		bAdded = false;
		auto Visit = [&](FGuid A, FGuid B)
		{
			if ((A.IsValid() && !ValidBones.Contains(A)) || !B.IsValid() || !ValidBones.Contains(B)) { return; }
			if (Reached.Contains(A) && !Reached.Contains(B)) { Reached.Add(B); bAdded = true; }
			if (Reached.Contains(B) && !Reached.Contains(A)) { Reached.Add(A); bAdded = true; }
		};
		for (const FAssemblyJointInstance& J : Joints) { Visit(J.BoneA, J.BoneB); }
		for (const FAssemblyMuscleInstance& M : Muscles) { Visit(M.EndpointA.BoneId, M.EndpointB.BoneId); }
	} while (bAdded);
	return Reached;
}

bool UAssemblyComponent::IsFreeConnected(const TArray<FAssemblyBoneInstance>& Bones,
	const TArray<FAssemblyMuscleInstance>& Muscles, const TArray<FAssemblyJointInstance>& Joints) const
{
	return GetFreeCoreReachable(Bones, Muscles, Joints).Num() == Bones.Num() + 1;
}

bool UAssemblyComponent::PreservesFreeCoreConnections(const TArray<FAssemblyBoneInstance>& Bones,
	const TArray<FAssemblyMuscleInstance>& Muscles, const TArray<FAssemblyJointInstance>& Joints, FGuid RemovedBone) const
{
	const TSet<FGuid> Before = GetFreeCoreReachable(InstalledBones, InstalledMuscles, InstalledJoints);
	const TSet<FGuid> After = GetFreeCoreReachable(Bones, Muscles, Joints);
	for (FGuid Id : Before)
	{
		if (Id != RemovedBone && !After.Contains(Id)) { return false; }
	}
	return true;
}

bool UAssemblyComponent::IsStructureConnected() const
{
	if (!bInitialized) { return false; }
	if (RoundPlacementMode == EAssemblyPlacementMode::Free) { return IsFreeConnected(InstalledBones, InstalledMuscles, InstalledJoints); }
	FText Reason;
	return AssemblyRules::ValidateLayout(RoundGrid, InstalledBones, nullptr, FGuid(), Reason);
}

bool UAssemblyComponent::TrySetJointKind(FGuid Id, EAssemblyJointKind Kind, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason)) { return false; }
	FAssemblyJointInstance* J = InstalledJoints.FindByPredicate([Id](const FAssemblyJointInstance& V) { return V.InstanceId == Id; });
	if (!J || !AssemblyFreeRules::IsJointKindValid(Kind)) { OutReason = LOCTEXT("InvalidJoint", "关节标识或种类无效。"); return false; }
	if (!IsJointKindAllowed(Id, Kind))
	{
		OutReason = LOCTEXT("JointKindRequiresReplacement", "紫色圆形关节保持活动连接，黄色关节保持固定连接；仅可切换各自的可断版本，改变活动性质需要更换零件。");
		return false;
	}
	if (J->Kind != Kind) { J->Kind = Kind; NotifyChanged(); }
	return true;
}

bool UAssemblyComponent::IsJointKindAllowed(FGuid Id, EAssemblyJointKind Kind) const
{
	const FAssemblyJointInstance* Joint = InstalledJoints.FindByPredicate([Id](const FAssemblyJointInstance& J) { return J.InstanceId == Id; });
	if (!bInitialized || !Joint) { return false; }
	const FAssemblyBoneInstance* A = Joint->BoneA.IsValid() ? FindBone(Joint->BoneA) : nullptr;
	const FAssemblyBoneInstance* B = FindBone(Joint->BoneB);
	const FAssemblyBoneDefinition* DA = A ? FindBoneDefinition(A->TypeId) : nullptr;
	const FAssemblyBoneDefinition* DB = B ? FindBoneDefinition(B->TypeId) : nullptr;
	if (!DB || (Joint->BoneA.IsValid() && !DA)) { return false; }
	return AssemblyFreeRules::IsJointKindAllowed(DA, DB, Kind);
}

bool UAssemblyComponent::TrySetJointLimits(FGuid Id, float Min, float Max, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason)) { return false; }
	FAssemblyJointInstance* J = InstalledJoints.FindByPredicate([Id](const FAssemblyJointInstance& V) { return V.InstanceId == Id; });
	if (!J || !AssemblyFreeRules::ValidateJointLimits(Min, Max))
	{
		OutReason = LOCTEXT("InvalidJointLimits", "关节上下限必须有效且总范围小于360度。");
		return false;
	}
	if (J->MinAngleDegrees != Min || J->MaxAngleDegrees != Max) { J->MinAngleDegrees = Min; J->MaxAngleDegrees = Max; NotifyChanged(); }
	return true;
}

bool UAssemblyComponent::RequestJointBreak(FGuid Id, FText& OutReason)
{
	if (!CanMutate(OutReason)) { return false; }
	const FAssemblyJointInstance* J = InstalledJoints.FindByPredicate([Id](const FAssemblyJointInstance& V) { return V.InstanceId == Id; });
	if (!bInitialized || Phase != EAssemblyPhase::Playing || !J
		|| (J->Kind != EAssemblyJointKind::BreakableHinge && J->Kind != EAssemblyJointKind::BreakableFixed))
	{
		OutReason = LOCTEXT("NotBreakable", "只有操控阶段的可断关节可以向物理系统请求断开。");
		return false;
	}
	TGuardValue<bool> Guard(bNotifying, true);
	OnJointBreakRequested.Broadcast(Id);
	return true;
}

bool UAssemblyComponent::ConfirmJointBroken(FGuid Id, FText& OutReason)
{
	if (!CanMutate(OutReason)) { return false; }
	const int32 Index = InstalledJoints.IndexOfByPredicate([Id](const FAssemblyJointInstance& J) { return J.InstanceId == Id; });
	if (!bInitialized || Phase != EAssemblyPhase::Playing || Index == INDEX_NONE
		|| (InstalledJoints[Index].Kind != EAssemblyJointKind::BreakableHinge && InstalledJoints[Index].Kind != EAssemblyJointKind::BreakableFixed))
	{
		OutReason = LOCTEXT("InvalidBreakConfirmation", "物理确认对应的可断关节已失效或当前不是操控阶段。");
		return false;
	}
	InstalledJoints.RemoveAt(Index);
	NotifyChanged();
	return true;
}

void UAssemblyComponent::RollbackNewInstance(FGuid Id)
{
	const int32 Bone = InstalledBones.IndexOfByPredicate([Id](const FAssemblyBoneInstance& B) { return B.InstanceId == Id; });
	const int32 Muscle = InstalledMuscles.IndexOfByPredicate([Id](const FAssemblyMuscleInstance& M) { return M.InstanceId == Id; });
	if (Bone != INDEX_NONE)
	{
		++RemainingQuantities.FindChecked(InstalledBones[Bone].TypeId);
		InstalledBones.RemoveAt(Bone); BonePoseSources.Remove(Id);
		if (RoundPlacementMode == EAssemblyPlacementMode::Free) { RebuildFreeJoints(); }
	}
	else if (Muscle != INDEX_NONE)
	{
		++RemainingQuantities.FindChecked(InstalledMuscles[Muscle].TypeId);
		InstalledMuscles.RemoveAt(Muscle);
	}
	if (Bone != INDEX_NONE || Muscle != INDEX_NONE) { NotifyChanged(); }
}

#undef LOCTEXT_NAMESPACE
