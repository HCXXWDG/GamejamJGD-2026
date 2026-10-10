#include "AssemblyFreeRules.h"

#define LOCTEXT_NAMESPACE "AssemblyFreeRules"

namespace
{
	constexpr double Tolerance = 0.001;
	bool IsFinite(FVector2D P) { return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y); }
	const FAssemblyBoneDefinition* FindDefinition(const TArray<FAssemblyBoneDefinition>& Definitions, FName Id)
	{
		return Definitions.FindByPredicate([Id](const FAssemblyBoneDefinition& D) { return D.TypeId == Id; });
	}
	struct FRectangle
	{
		FVector2D Center, XAxis, ZAxis, HalfSize;
	};
	FRectangle Rectangle(const FNormalBonePhysicalStats& Stats, const FTransform& Pose)
	{
		const FVector X = Pose.GetRotation().RotateVector(FVector::XAxisVector);
		const FVector Z = Pose.GetRotation().RotateVector(FVector::ZAxisVector);
		return { FVector2D(Pose.GetLocation().X, Pose.GetLocation().Z), FVector2D(X.X, X.Z),
			FVector2D(Z.X, Z.Z), FVector2D(Stats.Length * 0.5, Stats.Thickness * 0.5) };
	}
	double RadiusOn(const FRectangle& R, FVector2D Axis)
	{
		return R.HalfSize.X * FMath::Abs(FVector2D::DotProduct(R.XAxis, Axis))
			+ R.HalfSize.Y * FMath::Abs(FVector2D::DotProduct(R.ZAxis, Axis));
	}
	bool Overlap(const FRectangle& A, const FRectangle& B)
	{
		const FVector2D Offset = B.Center - A.Center;
		for (FVector2D Axis : { A.XAxis, A.ZAxis, B.XAxis, B.ZAxis })
		{
			if (FMath::Abs(FVector2D::DotProduct(Offset, Axis)) >= RadiusOn(A, Axis) + RadiusOn(B, Axis) - Tolerance)
			{
				return false;
			}
		}
		return true;
	}
	bool OverlapCore(const FRectangle& Bone, FVector2D Center, double Radius)
	{
		const FVector2D Delta = Center - Bone.Center;
		const double X = FVector2D::DotProduct(Delta, Bone.XAxis);
		const double Z = FVector2D::DotProduct(Delta, Bone.ZAxis);
		const double DX = X - FMath::Clamp(X, -Bone.HalfSize.X, Bone.HalfSize.X);
		const double DZ = Z - FMath::Clamp(Z, -Bone.HalfSize.Y, Bone.HalfSize.Y);
		return DX * DX + DZ * DZ < FMath::Square(FMath::Max(0.0, Radius - Tolerance));
	}
	FVector2D TransformPoint(const FTransform& Pose, FVector2D P)
	{
		const FVector V = Pose.TransformPosition(FVector(P.X, 0.0, P.Y));
		return FVector2D(V.X, V.Z);
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

bool AssemblyFreeRules::ValidateConfiguration(const FAssemblyFreeSettings& S,
	const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!IsFinite(S.CoreLocalPosition) || !FMath::IsFinite(S.CoreRadius) || S.CoreRadius <= 0.0f
		|| !FMath::IsFinite(S.ConnectionDistance) || S.ConnectionDistance < 0.0f
		|| !ValidateJointLimits(S.DefaultJointMinAngle, S.DefaultJointMaxAngle))
	{
		OutReason = LOCTEXT("InvalidSettings", "核心、连接距离或关节限角配置无效；关节不能无限旋转。");
		return false;
	}
	TSet<FName> Types;
	auto CheckType = [&Types, &OutReason](FName Type, int32 Quantity)
	{
		if (Type.IsNone() || Types.Contains(Type) || Quantity < 0)
		{
			OutReason = LOCTEXT("InvalidType", "组件类型必须非空且唯一，初始库存不能为负。");
			return false;
		}
		Types.Add(Type);
		return true;
	};
	for (const FAssemblyBoneDefinition& D : Bones)
	{
		FNormalBonePhysicalStats Stats;
		if (!CheckType(D.TypeId, D.InitialQuantity) || !NormalBoneMetrics::Evaluate(D.BoneParameters, Stats, OutReason)) { return false; }
	}
	for (const FAssemblyMuscleDefinition& D : Muscles)
	{
		if (!CheckType(D.TypeId, D.InitialQuantity)) { return false; }
	}
	return true;
}

FAssemblyPlacementResult AssemblyFreeRules::MakePlacement(FVector2D Position, float Angle)
{
	FAssemblyPlacementResult R;
	if (!IsFinite(Position) || !FMath::IsFinite(Angle))
	{
		R.Reason = LOCTEXT("InvalidPose", "骨头位置和角度必须是有限数值。");
		return R;
	}
	float Normalized = FMath::Fmod(Angle, 360.0f);
	if (Normalized > 180.0f) { Normalized -= 360.0f; }
	if (Normalized <= -180.0f) { Normalized += 360.0f; }
	R.LocalPosition = Position;
	R.RotationDegrees = Normalized;
	R.LocalVisualTransform = FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Normalized)), FVector(Position.X, 0, Position.Y));
	R.bValid = !R.LocalVisualTransform.ContainsNaN();
	if (!R.bValid) { R.Reason = LOCTEXT("InvalidTransform", "骨头变换超出有效范围。"); }
	return R;
}

bool AssemblyFreeRules::IsPointOnBone(const FNormalBoneParameters& P, FVector2D LocalPoint)
{
	FNormalBonePhysicalStats Stats;
	FText Reason;
	return IsFinite(LocalPoint) && NormalBoneMetrics::Evaluate(P, Stats, Reason)
		&& FMath::Abs(LocalPoint.X) <= Stats.Length * 0.5 + Tolerance
		&& FMath::Abs(LocalPoint.Y) <= Stats.Thickness * 0.5 + Tolerance;
}

bool AssemblyFreeRules::ValidateGeometry(const FAssemblyFreeSettings& Settings,
	const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyBoneInstance>& Bones,
	FName TypeId, const FAssemblyPlacementResult& Placement, FGuid IgnoreId, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const FAssemblyBoneDefinition* D = FindDefinition(Definitions, TypeId);
	FNormalBonePhysicalStats Stats;
	if (!Placement.bValid || !D || !NormalBoneMetrics::Evaluate(D->BoneParameters, Stats, OutReason))
	{
		if (OutReason.IsEmpty()) { OutReason = Placement.Reason.IsEmpty() ? LOCTEXT("UnknownBone", "骨头类型或摆放变换无效。"): Placement.Reason; }
		return false;
	}
	if (Settings.bAllowBoneOverlap) { return true; }
	const FRectangle Candidate = Rectangle(Stats, Placement.LocalVisualTransform);
	if (OverlapCore(Candidate, Settings.CoreLocalPosition, Settings.CoreRadius))
	{
		OutReason = LOCTEXT("CoreOverlap", "骨头不能穿过核心；可以将端点接到核心圆周。");
		return false;
	}
	for (const FAssemblyBoneInstance& B : Bones)
	{
		if (B.InstanceId == IgnoreId) { continue; }
		const FAssemblyBoneDefinition* Existing = FindDefinition(Definitions, B.TypeId);
		FNormalBonePhysicalStats Other;
		if (!Existing || !NormalBoneMetrics::Evaluate(Existing->BoneParameters, Other, OutReason)) { return false; }
		if (Overlap(Candidate, Rectangle(Other, B.LocalVisualTransform)))
		{
			OutReason = LOCTEXT("BoneOverlap", "骨头实体轮廓重叠，请调整位置或角度。");
			return false;
		}
	}
	return true;
}

TArray<FAssemblyJointInstance> AssemblyFreeRules::BuildJoints(const FAssemblyFreeSettings& S,
	const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyBoneInstance>& Bones,
	const TArray<FAssemblyJointInstance>& Previous)
{
	TArray<FAssemblyJointInstance> Result;
	auto Add = [&Result, &Previous, &S](FGuid A, FGuid B, FVector2D AnchorA, FVector2D AnchorB)
	{
		FAssemblyJointInstance Joint;
		const FAssemblyJointInstance* Old = Previous.FindByPredicate([&](const FAssemblyJointInstance& J)
		{
			// The core contact can slide around its circumference without changing joint identity.
			return J.BoneA == A && J.BoneB == B && (!A.IsValid() || J.LocalAnchorA.Equals(AnchorA, Tolerance))
				&& J.LocalAnchorB.Equals(AnchorB, Tolerance);
		});
		if (Old) { Joint = *Old; }
		else
		{
			Joint.InstanceId = FGuid::NewGuid();
			Joint.MinAngleDegrees = S.DefaultJointMinAngle;
			Joint.MaxAngleDegrees = S.DefaultJointMaxAngle;
		}
		Joint.BoneA = A; Joint.BoneB = B; Joint.LocalAnchorA = AnchorA; Joint.LocalAnchorB = AnchorB;
		Result.Add(Joint);
	};
	const double Threshold = S.ConnectionDistance + Tolerance;
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		const FAssemblyBoneInstance& A = Bones[I];
		const FAssemblyBoneDefinition* DA = FindDefinition(Definitions, A.TypeId);
		FNormalBonePhysicalStats SA;
		FText Reason;
		if (!DA || !NormalBoneMetrics::Evaluate(DA->BoneParameters, SA, Reason)) { continue; }
		for (double SignA : { -1.0, 1.0 })
		{
			const FVector2D AnchorA(SignA * SA.Length * 0.5, 0.0);
			const FVector2D PointA = TransformPoint(A.LocalVisualTransform, AnchorA);
			const FVector2D CoreDelta = PointA - S.CoreLocalPosition;
			const double Distance = CoreDelta.Size();
			if (FMath::IsFinite(Distance) && FMath::Abs(Distance - S.CoreRadius) <= Threshold)
			{
				const FVector2D Direction = Distance > Tolerance ? CoreDelta / Distance : FVector2D(1.0, 0.0);
				Add(FGuid(), A.InstanceId, Direction * S.CoreRadius, AnchorA);
			}
			for (int32 K = I + 1; K < Bones.Num(); ++K)
			{
				const FAssemblyBoneInstance& B = Bones[K];
				const FAssemblyBoneDefinition* DB = FindDefinition(Definitions, B.TypeId);
				FNormalBonePhysicalStats SB;
				if (!DB || !NormalBoneMetrics::Evaluate(DB->BoneParameters, SB, Reason)) { continue; }
				for (double SignB : { -1.0, 1.0 })
				{
					const FVector2D AnchorB(SignB * SB.Length * 0.5, 0.0);
					if (FVector2D::Distance(PointA, TransformPoint(B.LocalVisualTransform, AnchorB)) <= Threshold)
					{
						Add(A.InstanceId, B.InstanceId, AnchorA, AnchorB);
					}
				}
			}
		}
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
