#include "AssemblyFreeRules.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Assembly/AssemblySurfaceGeometry.h"

#define LOCTEXT_NAMESPACE "AssemblyFreeRules"

namespace
{
	using AssemblySurfaceGeometry::FShape;
	using AssemblySurfaceGeometry::FContact;
	constexpr double GeometryTolerance = AssemblySurfaceGeometry::Tolerance;
	bool IsFinite(FVector2D P) { return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y); }
	const FAssemblyBoneDefinition* FindDefinition(const TArray<FAssemblyBoneDefinition>& Definitions, FName Id)
	{
		return Definitions.FindByPredicate([Id](const FAssemblyBoneDefinition& D) { return D.TypeId == Id; });
	}
	bool GuidBefore(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}
	bool CanConnect(const FAssemblyBoneDefinition* A, const FAssemblyBoneDefinition* B)
	{
		// nullptr denotes the core, which is itself a connector. Two ordinary bones cannot connect directly.
		return !A || !B || A->PartKind == EAssemblyRigidPartKind::JointPart || B->PartKind == EAssemblyRigidPartKind::JointPart;
	}
	bool ValidSettings(const FAssemblyFreeSettings& S)
	{
		return IsFinite(S.CoreLocalPosition) && FMath::IsFinite(S.CoreRadius) && S.CoreRadius > 0.0f
			&& FMath::IsFinite(S.ConnectionDistance) && S.ConnectionDistance >= 0.0f
			&& FMath::IsFinite(S.SnapReleaseDistance) && S.SnapReleaseDistance >= 0.0f
			&& (!S.bEnableSurfaceSnapping || S.SnapReleaseDistance > S.ConnectionDistance)
			&& AssemblyFreeRules::ValidateJointLimits(S.DefaultJointMinAngle, S.DefaultJointMaxAngle);
	}
	FVector2D LocalPoint(const FTransform& Pose, FVector2D P)
	{
		const FVector Local = Pose.InverseTransformPosition(FVector(P.X, 0.0, P.Y));
		return FVector2D(Local.X, Local.Z);
	}
	FShape Translated(FShape Shape, FVector2D Delta)
	{
		Shape.Center += Delta;
		for (FVector2D& P : Shape.Vertices) P += Delta;
		return Shape;
	}
	struct FSurfaceTarget
	{
		bool bCore = false;
		FGuid Id;
		const FAssemblyBoneDefinition* Definition = nullptr;
		FTransform Pose = FTransform::Identity;
		FShape Shape;
	};
	bool BuildTargets(const FAssemblyFreeSettings& S, const TArray<FAssemblyBoneDefinition>& Definitions,
		const TArray<FAssemblyBoneInstance>& Bones, FGuid IgnoreId, TArray<FSurfaceTarget>& OutTargets, FText& OutReason)
	{
		OutTargets.Reset();
		if (!ValidSettings(S)) { OutReason = LOCTEXT("InvalidGeometrySettings", "核心、表面吸附距离或关节限角配置无效。"); return false; }
		FSurfaceTarget Core;
		Core.bCore = true;
		Core.Shape = AssemblySurfaceGeometry::MakeCircle(S.CoreLocalPosition, S.CoreRadius);
		OutTargets.Add(Core);
		TSet<FGuid> Ids;
		for (const FAssemblyBoneInstance& B : Bones)
		{
			if (!B.InstanceId.IsValid() || Ids.Contains(B.InstanceId))
			{
				OutReason = LOCTEXT("InvalidRigidIdentity", "已有零件实例标识无效或重复，不能进行表面连接。"); return false;
			}
			Ids.Add(B.InstanceId);
			if (B.InstanceId == IgnoreId) continue;
			FSurfaceTarget Target;
			Target.Id = B.InstanceId;
			Target.Definition = FindDefinition(Definitions, B.TypeId);
			Target.Pose = B.LocalVisualTransform;
			if (!Target.Definition || !AssemblySurfaceGeometry::BuildShape(*Target.Definition, Target.Pose, Target.Shape, OutReason))
			{
				if (OutReason.IsEmpty()) OutReason = LOCTEXT("MissingRigidDefinition", "已有零件的定义或表面轮廓无效。");
				return false;
			}
			OutTargets.Add(MoveTemp(Target));
		}
		return true;
	}
	bool ValidateAgainstTargets(const FAssemblyFreeSettings& S, const FShape& Candidate, const TArray<FSurfaceTarget>& Targets, FText& OutReason)
	{
		OutReason = FText::GetEmpty();
		if (S.bAllowBoneOverlap) return true;
		for (const FSurfaceTarget& Target : Targets)
		{
			if (!AssemblySurfaceGeometry::Overlaps(Candidate, Target.Shape)) continue;
			OutReason = Target.bCore ? LOCTEXT("CoreOverlap", "零件不能穿过核心；请将表面接到核心圆周。")
				: LOCTEXT("RigidOverlap", "零件实体轮廓重叠，请调整位置或角度。");
			return false;
		}
		return true;
	}
}

bool AssemblyFreeRules::ValidateJointLimits(float Min, float Max)
{
	return FMath::IsFinite(Min) && FMath::IsFinite(Max) && Min <= Max
		&& Min >= -359.0f && Max <= 359.0f && static_cast<double>(Max) - Min < 360.0;
}

bool AssemblyFreeRules::IsJointKindValid(EAssemblyJointKind Kind)
{
	return static_cast<uint8>(Kind) <= static_cast<uint8>(EAssemblyJointKind::BreakableFixed);
}

bool AssemblyFreeRules::IsJointKindAllowed(const FAssemblyBoneDefinition* A, const FAssemblyBoneDefinition* B, EAssemblyJointKind Kind)
{
	if (!IsJointKindValid(Kind) || !CanConnect(A, B)) return false;
	const bool bPartA = A && A->PartKind == EAssemblyRigidPartKind::JointPart;
	const bool bPartB = B && B->PartKind == EAssemblyRigidPartKind::JointPart;
	if (!bPartA && !bPartB) return true; // bone-to-core keeps the existing configurable core constraint.
	const bool bRound = (bPartA && A->JointShape == EAssemblyJointPartShape::Round)
		|| (bPartB && B->JointShape == EAssemblyJointPartShape::Round);
	return bRound ? (Kind == EAssemblyJointKind::Hinge || Kind == EAssemblyJointKind::BreakableHinge)
		: (Kind == EAssemblyJointKind::Fixed || Kind == EAssemblyJointKind::BreakableFixed);
}

bool AssemblyFreeRules::ValidateConfiguration(const FAssemblyFreeSettings& S,
	const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!ValidSettings(S))
	{
		OutReason = LOCTEXT("InvalidSettings", "核心、吸附距离或关节限角配置无效；启用吸附时脱离距离必须大于吸附距离，关节不能无限旋转。"); return false;
	}
	TSet<FName> Types;
	auto CheckType = [&Types, &OutReason](FName Type, int32 Quantity)
	{
		if (Type.IsNone() || Types.Contains(Type) || Quantity < 0)
		{
			OutReason = LOCTEXT("InvalidType", "组件类型必须非空且唯一，初始库存不能为负。"); return false;
		}
		Types.Add(Type);
		return true;
	};
	for (const FAssemblyBoneDefinition& D : Bones)
	{
		FShape Shape;
		if (!CheckType(D.TypeId, D.InitialQuantity) || !AssemblySurfaceGeometry::BuildShape(D, FTransform::Identity, Shape, OutReason)) return false;
	}
	for (const FAssemblyMuscleDefinition& D : Muscles) if (!CheckType(D.TypeId, D.InitialQuantity)) return false;
	return true;
}

FAssemblyPlacementResult AssemblyFreeRules::MakePlacement(FVector2D Position, float Angle)
{
	FAssemblyPlacementResult R;
	if (!IsFinite(Position) || !FMath::IsFinite(Angle))
	{
		R.Reason = LOCTEXT("InvalidPose", "零件位置和角度必须是有限数值。"); return R;
	}
	float Normalized = FMath::Fmod(Angle, 360.0f);
	if (Normalized > 180.0f) Normalized -= 360.0f;
	if (Normalized <= -180.0f) Normalized += 360.0f;
	R.LocalPosition = Position;
	R.RotationDegrees = Normalized;
	R.LocalVisualTransform = FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Normalized)), FVector(Position.X, 0, Position.Y));
	R.bValid = !R.LocalVisualTransform.ContainsNaN();
	if (!R.bValid) R.Reason = LOCTEXT("InvalidTransform", "零件变换超出有效范围。");
	return R;
}

bool AssemblyFreeRules::IsPointOnBone(const FNormalBoneParameters& Parameters, FVector2D Point)
{
	FAssemblyBoneDefinition D;
	D.BoneParameters = Parameters;
	return IsPointOnRigidPart(D, Point);
}

bool AssemblyFreeRules::IsPointOnRigidPart(const FAssemblyBoneDefinition& D, FVector2D Point)
{
	FShape Shape;
	FText Reason;
	return IsFinite(Point) && AssemblySurfaceGeometry::BuildShape(D, FTransform::Identity, Shape, Reason)
		&& AssemblySurfaceGeometry::ContainsPoint(Shape, Point);
}

bool AssemblyFreeRules::ValidateGeometry(const FAssemblyFreeSettings& S,
	const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyBoneInstance>& Bones,
	FName TypeId, const FAssemblyPlacementResult& Placement, FGuid IgnoreId, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const FAssemblyBoneDefinition* D = FindDefinition(Definitions, TypeId);
	FShape Shape;
	if (!Placement.bValid || !D || !AssemblySurfaceGeometry::BuildShape(*D, Placement.LocalVisualTransform, Shape, OutReason))
	{
		if (OutReason.IsEmpty()) OutReason = Placement.Reason.IsEmpty() ? LOCTEXT("UnknownRigidPart", "零件类型或摆放变换无效。") : Placement.Reason;
		return false;
	}
	TArray<FSurfaceTarget> Targets;
	return BuildTargets(S, Definitions, Bones, IgnoreId, Targets, OutReason) && ValidateAgainstTargets(S, Shape, Targets, OutReason);
}

FAssemblyPlacementResult AssemblyFreeRules::ResolveSurfacePlacement(const FAssemblyFreeSettings& S,
	const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyBoneInstance>& Bones,
	FName TypeId, FVector2D RawPosition, float Angle, FGuid IgnoreId, const FAssemblySurfaceSnapState& PreviousSnap)
{
	FAssemblyPlacementResult Raw = MakePlacement(RawPosition, Angle);
	if (!Raw.bValid) return Raw;
	const FAssemblyBoneDefinition* Definition = FindDefinition(Definitions, TypeId);
	FShape MovingShape;
	TArray<FSurfaceTarget> Targets;
	if (!Definition || !AssemblySurfaceGeometry::BuildShape(*Definition, Raw.LocalVisualTransform, MovingShape, Raw.Reason)
		|| !BuildTargets(S, Definitions, Bones, IgnoreId, Targets, Raw.Reason))
	{
		Raw.bValid = false;
		if (Raw.Reason.IsEmpty()) Raw.Reason = LOCTEXT("UnknownSnapPart", "零件类型或表面轮廓无效。");
		return Raw;
	}
	if (S.bEnableSurfaceSnapping)
	{
		struct FCandidate
		{
			FAssemblyPlacementResult Placement;
			double DistanceSquared = 0.0;
			bool bCore = false;
			FGuid Target;
		};
		auto Collect = [&](const FSurfaceTarget& Target, double Threshold, TArray<FCandidate>& Out)
		{
			if (!CanConnect(Definition, Target.Definition)) return;
			TArray<FContact> Contacts;
			AssemblySurfaceGeometry::GetContactCandidates(MovingShape, Target.Shape, Contacts);
			for (const FContact& Contact : Contacts)
			{
				if (Contact.DistanceSquared > FMath::Square(Threshold + GeometryTolerance)) continue;
				const FVector2D Translation = Contact.PointB - Contact.PointA;
				FAssemblyPlacementResult Result = MakePlacement(RawPosition + Translation, Angle);
				if (!Result.bValid) continue;
				const FShape ResolvedShape = Translated(MovingShape, Translation);
				// Even an overlap-enabled sandbox must not call penetration a snapped surface contact.
				if (AssemblySurfaceGeometry::Overlaps(ResolvedShape, Target.Shape)
					|| !ValidateAgainstTargets(S, ResolvedShape, Targets, Result.Reason)) continue;
				FContact ConfirmedContact;
				if (!AssemblySurfaceGeometry::ClosestContact(ResolvedShape, Target.Shape, ConfirmedContact)
					|| ConfirmedContact.DistanceSquared > GeometryTolerance * GeometryTolerance * 4.0) continue;
				Result.SurfaceSnap.bSnapped = true;
				Result.SurfaceSnap.bCore = Target.bCore;
				Result.SurfaceSnap.TargetId = Target.Id;
				Result.SurfaceSnap.SourceLocalAnchor = LocalPoint(Result.LocalVisualTransform, ConfirmedContact.PointA);
				Result.SurfaceSnap.TargetLocalAnchor = Target.bCore ? ConfirmedContact.PointB - S.CoreLocalPosition : LocalPoint(Target.Pose, ConfirmedContact.PointB);
				Out.Add({ MoveTemp(Result), Contact.DistanceSquared, Target.bCore, Target.Id });
			}
		};
		auto Sort = [](TArray<FCandidate>& Candidates)
		{
			Candidates.Sort([](const FCandidate& A, const FCandidate& B)
			{
				if (A.DistanceSquared != B.DistanceSquared) return A.DistanceSquared < B.DistanceSquared;
				if (A.bCore != B.bCore) return A.bCore;
				if (A.Target != B.Target) return GuidBefore(A.Target, B.Target);
				const FVector2D PA = A.Placement.LocalPosition, PB = B.Placement.LocalPosition;
				if (PA.X != PB.X) return PA.X < PB.X;
				return PA.Y < PB.Y;
			});
		};
		const FSurfaceTarget* PreviousTarget = PreviousSnap.bSnapped ? Targets.FindByPredicate([&](const FSurfaceTarget& T)
		{
			return PreviousSnap.bCore ? T.bCore : (!T.bCore && PreviousSnap.TargetId.IsValid() && T.Id == PreviousSnap.TargetId);
		}) : nullptr;
		TArray<FCandidate> Candidates;
		if (PreviousTarget)
		{
			Collect(*PreviousTarget, S.SnapReleaseDistance, Candidates);
			Sort(Candidates);
			if (!Candidates.IsEmpty()) return MoveTemp(Candidates[0].Placement);
		}
		// A released target cannot immediately reacquire on the same update. Other nearby surfaces remain usable.
		for (const FSurfaceTarget& Target : Targets)
		{
			if (&Target != PreviousTarget) Collect(Target, S.ConnectionDistance, Candidates);
		}
		Sort(Candidates);
		if (!Candidates.IsEmpty()) return MoveTemp(Candidates[0].Placement);
	}
	Raw.bValid = ValidateAgainstTargets(S, MovingShape, Targets, Raw.Reason);
	return Raw;
}

TArray<FAssemblyJointInstance> AssemblyFreeRules::BuildJoints(const FAssemblyFreeSettings& S,
	const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyBoneInstance>& Bones,
	const TArray<FAssemblyJointInstance>& Previous)
{
	TArray<FSurfaceTarget> Targets;
	FText Reason;
	if (!BuildTargets(S, Definitions, Bones, FGuid(), Targets, Reason)) return {};
	struct FConnection
	{
		const FSurfaceTarget* A = nullptr;
		const FSurfaceTarget* B = nullptr;
		FContact Contact;
	};
	TArray<FConnection> Connections;
	const double ThresholdSquared = FMath::Square(S.ConnectionDistance + GeometryTolerance);
	for (int32 I = 0; I < Targets.Num(); ++I)
	{
		for (int32 K = I + 1; K < Targets.Num(); ++K)
		{
			const FSurfaceTarget* A = &Targets[I];
			const FSurfaceTarget* B = &Targets[K];
			if (!CanConnect(A->Definition, B->Definition) || AssemblySurfaceGeometry::Overlaps(A->Shape, B->Shape)) continue;
			// Always evaluate the same pair orientation, independent of installed-array order.
			if (!A->bCore && GuidBefore(B->Id, A->Id)) Swap(A, B);
			FContact Contact;
			if (AssemblySurfaceGeometry::ClosestContact(A->Shape, B->Shape, Contact) && Contact.DistanceSquared <= ThresholdSquared)
			{
				Connections.Add({ A, B, Contact });
			}
		}
	}
	Connections.Sort([](const FConnection& A, const FConnection& B)
	{
		if (A.Contact.DistanceSquared != B.Contact.DistanceSquared) return A.Contact.DistanceSquared < B.Contact.DistanceSquared;
		if (A.A->Id != B.A->Id) return GuidBefore(A.A->Id, B.A->Id);
		return GuidBefore(A.B->Id, B.B->Id);
	});
	TArray<FAssemblyJointInstance> Result;
	for (const FConnection& C : Connections)
	{
		FGuid IdA = C.A->Id, IdB = C.B->Id;
		FVector2D AnchorA = C.A->bCore ? C.Contact.PointA - S.CoreLocalPosition : LocalPoint(C.A->Pose, C.Contact.PointA);
		FVector2D AnchorB = LocalPoint(C.B->Pose, C.Contact.PointB);
		const FAssemblyJointInstance* Old = Previous.FindByPredicate([&](const FAssemblyJointInstance& J)
		{
			const bool bPair = (J.BoneA == IdA && J.BoneB == IdB) || (IdA.IsValid() && J.BoneA == IdB && J.BoneB == IdA);
			return bPair && J.InstanceId.IsValid() && IsJointKindAllowed(C.A->Definition, C.B->Definition, J.Kind)
				&& ValidateJointLimits(J.MinAngleDegrees, J.MaxAngleDegrees);
		});
		FAssemblyJointInstance Joint;
		if (Old)
		{
			Joint = *Old;
			if (Joint.BoneA != IdA) { Swap(IdA, IdB); Swap(AnchorA, AnchorB); }
		}
		else
		{
			Joint.InstanceId = FGuid::NewGuid();
			Joint.MinAngleDegrees = S.DefaultJointMinAngle;
			Joint.MaxAngleDegrees = S.DefaultJointMaxAngle;
			const bool bPartA = C.A->Definition && C.A->Definition->PartKind == EAssemblyRigidPartKind::JointPart;
			const bool bPartB = C.B->Definition && C.B->Definition->PartKind == EAssemblyRigidPartKind::JointPart;
			if (bPartA && bPartB)
			{
				const bool bRoundA = C.A->Definition->JointShape == EAssemblyJointPartShape::Round;
				const bool bRoundB = C.B->Definition->JointShape == EAssemblyJointPartShape::Round;
				const FAssemblyBoneDefinition* KindSource = bRoundA != bRoundB ? (bRoundA ? C.A->Definition : C.B->Definition) : C.A->Definition;
				Joint.Kind = KindSource->DefaultJointKind;
			}
			else if (bPartA) Joint.Kind = C.A->Definition->DefaultJointKind;
			else if (bPartB) Joint.Kind = C.B->Definition->DefaultJointKind;
		}
		Joint.BoneA = IdA;
		Joint.BoneB = IdB;
		Joint.LocalAnchorA = AnchorA;
		Joint.LocalAnchorB = AnchorB;
		Result.Add(Joint);
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
