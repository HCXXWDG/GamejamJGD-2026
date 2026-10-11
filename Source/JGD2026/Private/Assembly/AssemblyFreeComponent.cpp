#include "Assembly/AssemblyComponent.h"
#include "AssemblyFreeRules.h"
#include "AssemblyRules.h"

#define LOCTEXT_NAMESPACE "AssemblyFreeComponent"

FAssemblyPlacementResult UAssemblyComponent::ValidateFreeBonePlacement(FName TypeId, FVector2D Position,
	float Angle, FGuid IgnoreId) const
{
	FAssemblyPlacementResult R;
	if (!CanEdit(R.Reason)) { return R; }
	if (RoundPlacementMode != EAssemblyPlacementMode::Free)
	{
		R.Reason = LOCTEXT("NotFree", "当前使用网格模式，请先 Configure Free Assembly。");
		return R;
	}
	if (!FindBoneDefinition(TypeId)) { R.Reason = LOCTEXT("UnknownBone", "本关卡没有这种骨头。"); return R; }
	if (IgnoreId.IsValid())
	{
		const FAssemblyBoneInstance* Existing = FindBone(IgnoreId);
		if (!Existing || Existing->TypeId != TypeId) { R.Reason = LOCTEXT("UnknownMove", "找不到要移动的同类型骨头。"); return R; }
	}
	else if (GetRemainingQuantity(TypeId) <= 0) { R.Reason = LOCTEXT("NoStock", "该骨头的剩余数量不足。"); return R; }
	R = AssemblyFreeRules::MakePlacement(Position, Angle);
	if (R.bValid) { R.bValid = AssemblyFreeRules::ValidateGeometry(RoundFreeSettings, RoundBoneDefinitions, InstalledBones, TypeId, R, IgnoreId, R.Reason); }
	return R;
}

bool UAssemblyComponent::TryInstallFreeBone(FName TypeId, FVector2D Position, float Angle, FGuid& OutId, FText& OutReason)
{
	OutId.Invalidate();
	if (!CanMutate(OutReason)) { return false; }
	const FAssemblyPlacementResult R = ValidateFreeBonePlacement(TypeId, Position, Angle, FGuid());
	OutReason = R.Reason;
	if (!R.bValid) { return false; }
	FAssemblyBoneInstance Bone;
	Bone.InstanceId = FGuid::NewGuid(); Bone.TypeId = TypeId;
	Bone.LocalPosition = R.LocalPosition; Bone.RotationDegrees = R.RotationDegrees;
	Bone.LocalVisualTransform = R.LocalVisualTransform;
	InstalledBones.Add(Bone);
	--RemainingQuantities.FindChecked(TypeId);
	OutId = Bone.InstanceId;
	RebuildFreeJoints();
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TryMoveFreeBone(FGuid Id, FVector2D Position, float Angle, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason)) { return false; }
	const int32 Index = InstalledBones.IndexOfByPredicate([Id](const FAssemblyBoneInstance& B) { return B.InstanceId == Id; });
	if (Index == INDEX_NONE) { OutReason = LOCTEXT("UnknownMove", "找不到要移动的骨头。"); return false; }
	const FAssemblyPlacementResult R = ValidateFreeBonePlacement(InstalledBones[Index].TypeId, Position, Angle, Id);
	OutReason = R.Reason;
	if (!R.bValid) { return false; }
	FAssemblyBoneInstance& B = InstalledBones[Index];
	if (B.LocalPosition == R.LocalPosition && B.RotationDegrees == R.RotationDegrees) { return true; }
	B.LocalPosition = R.LocalPosition; B.RotationDegrees = R.RotationDegrees; B.LocalVisualTransform = R.LocalVisualTransform;
	RebuildFreeJoints();
	NotifyChanged();
	return true;
}

void UAssemblyComponent::RebuildFreeJoints()
{
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
	if (J->Kind != Kind) { J->Kind = Kind; NotifyChanged(); }
	return true;
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
