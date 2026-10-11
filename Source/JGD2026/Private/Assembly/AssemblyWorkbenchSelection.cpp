#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Assembly/NormalBone.h"
#include "Components/SplineComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"

#define LOCTEXT_NAMESPACE "AssemblyWorkbenchSelection"

namespace
{
	double SegmentDistanceSquared(FVector2D Point, FVector2D A, FVector2D B)
	{
		const FVector2D Delta = B - A;
		const double LengthSquared = Delta.SizeSquared();
		const double Alpha = LengthSquared > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp(FVector2D::DotProduct(Point - A, Delta) / LengthSquared, 0.0, 1.0) : 0.0;
		return (Point - (A + Delta * Alpha)).SizeSquared();
	}
}

bool UAssemblyWorkbenchComponent::SelectionExists(const FAssemblySelection& Value) const
{
	if (Value.Kind == EAssemblySelectionKind::None) { return true; }
	if (!Assembly.IsValid() || !Value.InstanceId.IsValid()) { return false; }
	switch (Value.Kind)
	{
	case EAssemblySelectionKind::Bone:
	{
		FAssemblyBoneInstance Bone;
		return Assembly->GetBone(Value.InstanceId, Bone);
	}
	case EAssemblySelectionKind::Muscle:
	{
		FAssemblyMuscleInstance Muscle;
		return Assembly->GetMuscle(Value.InstanceId, Muscle);
	}
	case EAssemblySelectionKind::Joint:
		return Assembly->GetJoints().ContainsByPredicate([&Value](const FAssemblyJointInstance& Joint) { return Joint.InstanceId == Value.InstanceId; });
	default:
		return false;
	}
}

bool UAssemblyWorkbenchComponent::SelectInstance(FGuid Id, EAssemblySelectionKind Kind, FText& OutReason)
{
	if (!CanUse(OutReason)) { return Fail(OutReason, OutReason); }
	if (Kind == EAssemblySelectionKind::None) { ClearSelection(); return true; }
	FAssemblySelection Candidate;
	Candidate.Kind = Kind;
	Candidate.InstanceId = Id;
	if (!SelectionExists(Candidate)) { return Fail(LOCTEXT("InvalidSelection", "要选择的骨头、肌肉或关节不存在。"), OutReason); }
	if (Selection.Kind != Kind || Selection.InstanceId != Id)
	{
		Selection = Candidate;
		OnSelectionChanged.Broadcast(Selection);
		OnWorkbenchChanged.Broadcast();
	}
	OutReason = FText::GetEmpty();
	return true;
}

void UAssemblyWorkbenchComponent::ClearSelection()
{
	if (Selection.Kind == EAssemblySelectionKind::None && !Selection.InstanceId.IsValid()) { return; }
	Selection = FAssemblySelection();
	OnSelectionChanged.Broadcast(Selection);
	if (!bShutdown) { OnWorkbenchChanged.Broadcast(); }
}

bool UAssemblyWorkbenchComponent::RemoveSelected(FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	if (Selection.Kind != EAssemblySelectionKind::Bone && Selection.Kind != EAssemblySelectionKind::Muscle)
	{
		return Fail(LOCTEXT("NoRemovableSelection", "请选择要拆下的骨头或肌肉；核心不能拆卸。"), OutReason);
	}
	const FGuid Id = Selection.InstanceId;
	CancelTool();
	if (!Presenter->RemoveInstance(Id, OutReason)) { return Fail(OutReason, OutReason); }
	ClearSelection();
	LastMessage = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::SelectBindingKey(EAssemblyMuscleKey Key, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	if (Selection.Kind == EAssemblySelectionKind::Muscle
		&& !Assembly->TrySetMuscleKey(Selection.InstanceId, Key, OutReason))
	{
		return Fail(OutReason, OutReason);
	}
	// One button means the same key for the selected muscle and the next shelf installation.
	if (!Assembly->SelectMuscleKey(Key, OutReason)) { return Fail(OutReason, OutReason); }
	LastMessage = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::SetSelectedJointKind(EAssemblyJointKind Kind, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	if (Selection.Kind != EAssemblySelectionKind::Joint) { return Fail(LOCTEXT("SelectJointKind", "请先选择关节再修改种类。"), OutReason); }
	if (!Assembly->TrySetJointKind(Selection.InstanceId, Kind, OutReason)) { return Fail(OutReason, OutReason); }
	LastMessage = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::SetSelectedJointLimits(float Min, float Max, FText& OutReason)
{
	if (!CanEdit(OutReason)) { return Fail(OutReason, OutReason); }
	if (Selection.Kind != EAssemblySelectionKind::Joint) { return Fail(LOCTEXT("SelectJointLimits", "请先选择关节再修改限角。"), OutReason); }
	if (!Assembly->TrySetJointLimits(Selection.InstanceId, Min, Max, OutReason)) { return Fail(OutReason, OutReason); }
	LastMessage = FText::GetEmpty();
	OnWorkbenchChanged.Broadcast();
	return true;
}

bool UAssemblyWorkbenchComponent::RequestSelectedJointBreak(FText& OutReason)
{
	if (!CanUse(OutReason)) { return Fail(OutReason, OutReason); }
	if (Selection.Kind != EAssemblySelectionKind::Joint) { return Fail(LOCTEXT("SelectBreakableJoint", "请先选择要断开的可断关节。"), OutReason); }
	if (!Assembly->RequestJointBreak(Selection.InstanceId, OutReason)) { return Fail(OutReason, OutReason); }
	Report(LOCTEXT("JointBreakRequested", "已请求物理系统断开关节，等待物理确认。"));
	return true;
}

bool UAssemblyWorkbenchComponent::GetJointWorldAnchors(const FAssemblyJointInstance& Joint, FVector& OutA, FVector& OutB) const
{
	return Presenter.IsValid() && Presenter->GetJointWorldAnchors(Joint, OutA, OutB);
}

bool UAssemblyWorkbenchComponent::PickAtPointer(FVector2D ViewportPixels, FAssemblySelection& OutSelection) const
{
	OutSelection = FAssemblySelection();
	if (!IsWorkbenchInitialized() || !bPointerValid) { return false; }
	double BestJointDistance = FMath::Square(static_cast<double>(FMath::Max(1.0f, JointPickRadiusPixels)));
	for (const FAssemblyJointInstance& Joint : Assembly->GetJoints())
	{
		FVector A, B;
		FVector2D Screen;
		if (GetJointWorldAnchors(Joint, A, B) && Player->ProjectWorldLocationToScreen((A + B) * 0.5, Screen, false))
		{
			const double Distance = (Screen - ViewportPixels).SizeSquared();
			if (Distance <= BestJointDistance)
			{
				BestJointDistance = Distance;
				OutSelection.Kind = EAssemblySelectionKind::Joint;
				OutSelection.InstanceId = Joint.InstanceId;
			}
		}
	}
	if (OutSelection.Kind != EAssemblySelectionKind::None) { return true; }
	double BestMuscleDistance = FMath::Square(static_cast<double>(FMath::Max(1.0f, MusclePickRadiusPixels)));
	for (const FAssemblyMuscleInstance& Muscle : Assembly->GetMuscles())
	{
		const AAssemblyMuscle* Actor = Presenter->GetMuscleActor(Muscle.InstanceId);
		USplineComponent* Spline = IsValid(Actor) ? Actor->GetCurveSpline() : nullptr;
		if (!Spline || Actor->IsHidden() || Spline->GetNumberOfSplinePoints() < 2) { continue; }
		const float Length = Spline->GetSplineLength();
		if (!FMath::IsFinite(Length) || Length <= UE_SMALL_NUMBER) { continue; }
		const int32 Samples = FMath::Clamp(Actor->CurveSegments, 8, 64) * 2;
		FVector2D Previous;
		bool bPreviousProjected = Player->ProjectWorldLocationToScreen(Spline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World), Previous, false);
		for (int32 Index = 1; Index <= Samples; ++Index)
		{
			FVector2D Current;
			const bool bCurrentProjected = Player->ProjectWorldLocationToScreen(
				Spline->GetLocationAtDistanceAlongSpline(Length * Index / Samples, ESplineCoordinateSpace::World), Current, false);
			if (bPreviousProjected && bCurrentProjected)
			{
				const double Distance = SegmentDistanceSquared(ViewportPixels, Previous, Current);
				if (Distance <= BestMuscleDistance)
				{
					BestMuscleDistance = Distance;
					OutSelection.Kind = EAssemblySelectionKind::Muscle;
					OutSelection.InstanceId = Muscle.InstanceId;
				}
			}
			Previous = Current;
			bPreviousProjected = bCurrentProjected;
		}
	}
	if (OutSelection.Kind != EAssemblySelectionKind::None) { return true; }
	FAssemblyMuscleEndpoint BonePoint;
	if (Assembly->FindBoneAtWorldPosition(PointerWorld, BonePoint))
	{
		OutSelection.Kind = EAssemblySelectionKind::Bone;
		OutSelection.InstanceId = BonePoint.BoneId;
		return true;
	}
	return false;
}

void UAssemblyWorkbenchComponent::DrawSelectionFeedback() const
{
	if (!bDrawSelectionFeedback || !IsWorkbenchInitialized() || Assembly->GetPhase() != EAssemblyPhase::Assembly) { return; }
	for (const FAssemblyJointInstance& Joint : Assembly->GetJoints())
	{
		FVector A, B;
		if (GetJointWorldAnchors(Joint, A, B))
		{
			const bool bSelected = Selection.Kind == EAssemblySelectionKind::Joint && Selection.InstanceId == Joint.InstanceId;
			if (bSelected || !Presenter->HasJointArt(Joint.InstanceId))
			{
				DrawDebugPoint(GetWorld(), (A + B) * 0.5, bSelected ? 12.0f : 7.0f, bSelected ? SelectionColor : FColor(90, 200, 235), false, 0.0f, 0);
			}
			if (bSelected) { DrawDebugLine(GetWorld(), A, B, SelectionColor, false, 0.0f, 0, 2.0f); }
		}
	}
	if (Selection.Kind == EAssemblySelectionKind::Bone)
	{
		const ANormalBone* Actor = Presenter->GetBoneActor(Selection.InstanceId);
		FTransform Pose;
		if (IsValid(Actor) && !Actor->IsHidden() && Assembly->GetBoneWorldTransform(Selection.InstanceId, Pose))
		{
			const FNormalBonePhysicalStats Stats = Actor->GetPhysicalStats();
			if (Stats.bValid)
			{
				const FVector Scale = Pose.GetScale3D().GetAbs();
				DrawDebugBox(GetWorld(), Pose.GetLocation(), FVector(Stats.Length * 0.5f, Actor->BodyHalfThickness, Stats.Thickness * 0.5f) * Scale,
					Pose.GetRotation(), SelectionColor, false, 0.0f, 0, 1.5f);
			}
		}
	}
	else if (Selection.Kind == EAssemblySelectionKind::Muscle)
	{
		const AAssemblyMuscle* Actor = Presenter->GetMuscleActor(Selection.InstanceId);
		USplineComponent* Spline = IsValid(Actor) ? Actor->GetCurveSpline() : nullptr;
		if (Spline && !Actor->IsHidden() && Spline->GetNumberOfSplinePoints() >= 2)
		{
			const float Length = Spline->GetSplineLength();
			FVector Previous = Spline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
			for (int32 Index = 1; Index <= 32; ++Index)
			{
				const FVector Current = Spline->GetLocationAtDistanceAlongSpline(Length * Index / 32.0f, ESplineCoordinateSpace::World);
				DrawDebugLine(GetWorld(), Previous, Current, SelectionColor, false, 0.0f, 0, 1.5f);
				Previous = Current;
			}
		}
	}
	if (Tool == EAssemblyWorkbenchTool::MuscleFirst && bPointerValid)
	{
		DrawDebugPoint(GetWorld(), PointerWorld, 9.0f, CandidateEndpoint.BoneId.IsValid() ? FColor::Green : FColor::Red, false, 0.0f, 0);
	}
	else if (Tool == EAssemblyWorkbenchTool::MuscleSecond)
	{
		FVector WorldA;
		if (Assembly->GetEndpointWorldPosition(FirstEndpoint, WorldA)) { DrawDebugPoint(GetWorld(), WorldA, 10.0f, SelectionColor, false, 0.0f, 0); }
	}
}

#undef LOCTEXT_NAMESPACE
