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
		// The spherical core is a connector too. Only bone-to-connector pairs may form an edge.
		const bool bBoneA = A && A->PartKind == EAssemblyRigidPartKind::Bone;
		const bool bBoneB = B && B->PartKind == EAssemblyRigidPartKind::Bone;
		const bool bConnectorA = !A || A->PartKind == EAssemblyRigidPartKind::JointPart;
		const bool bConnectorB = !B || B->PartKind == EAssemblyRigidPartKind::JointPart;
		return (bBoneA && bConnectorB) || (bBoneB && bConnectorA);
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
	// The outermost actual boundary on a ray, including collinear polygon edges. A custom
	// outline need not pass through its nominal bounding-box end, so no stats-only anchors are used.
	bool RayBoundary(const FShape& Shape, FVector2D Direction, FVector2D& OutPoint)
	{
		if (!IsFinite(Direction) || !FMath::IsNearlyEqual(Direction.SizeSquared(), 1.0, UE_KINDA_SMALL_NUMBER)) return false;
		if (Shape.bCircle)
		{
			OutPoint = Shape.Center + Direction * Shape.Radius;
			return IsFinite(OutPoint) && AssemblySurfaceGeometry::IsOnBoundary(Shape, OutPoint);
		}
		double Farthest = TNumericLimits<double>::Lowest();
		bool bFound = false;
		auto Consider = [&](double Distance)
		{
			if (FMath::IsFinite(Distance) && Distance > GeometryTolerance && (!bFound || Distance > Farthest))
			{
				Farthest = Distance;
				bFound = true;
			}
		};
		for (int32 I = 0; I < Shape.Vertices.Num(); ++I)
		{
			const FVector2D A = Shape.Vertices[I] - Shape.Center;
			const FVector2D B = Shape.Vertices[(I + 1) % Shape.Vertices.Num()] - Shape.Center;
			const FVector2D Edge = B - A;
			const double Denominator = FVector2D::CrossProduct(Direction, Edge);
			if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
			{
				if (FMath::Abs(FVector2D::CrossProduct(Direction, A)) <= GeometryTolerance)
				{
					Consider(FVector2D::DotProduct(Direction, A));
					Consider(FVector2D::DotProduct(Direction, B));
				}
				continue;
			}
			const double AlongEdge = FVector2D::CrossProduct(A, Direction) / Denominator;
			if (AlongEdge >= 0.0 && AlongEdge <= 1.0)
				Consider(FVector2D::CrossProduct(A, Edge) / Denominator);
		}
		if (!bFound) return false;
		OutPoint = Shape.Center + Direction * Farthest;
		return IsFinite(OutPoint) && AssemblySurfaceGeometry::IsOnBoundary(Shape, OutPoint);
	}
	struct FBoneEnds
	{
		FVector2D Points[2]; // negative X, positive X
		FVector2D Axis;
	};
	bool BuildBoneEnds(const FAssemblyBoneDefinition& Definition, const FTransform& Pose, FBoneEnds& OutEnds)
	{
		if (Definition.PartKind != EAssemblyRigidPartKind::Bone) return false;
		TArray<AssemblyRigidPartMetrics::FConnectionPort> Ports;
		FText Reason;
		if (!AssemblyRigidPartMetrics::GetConnectionPorts(Definition, Ports, Reason) || Ports.Num() != 2) return false;
		const FVector Axis = Pose.TransformVector(FVector::XAxisVector);
		OutEnds.Axis = FVector2D(Axis.X, Axis.Z).GetSafeNormal();
		if (!IsFinite(OutEnds.Axis) || !FMath::IsNearlyEqual(OutEnds.Axis.SizeSquared(), 1.0, UE_KINDA_SMALL_NUMBER)) return false;
		for (int32 End = 0; End < 2; ++End)
		{
			const FVector Point = Pose.TransformPosition(FVector(Ports[End].Point.X, 0, Ports[End].Point.Y));
			OutEnds.Points[End] = FVector2D(Point.X, Point.Z);
			if (!IsFinite(OutEnds.Points[End])) return false;
		}
		return true;
	}
	int32 PreviousEnd(const FBoneEnds& Ends, const FTransform& Pose, FVector2D LocalAnchor)
	{
		if (!IsFinite(LocalAnchor)) return INDEX_NONE;
		for (int32 End = 0; End < 2; ++End)
		{
			if (LocalPoint(Pose, Ends.Points[End]).Equals(LocalAnchor, GeometryTolerance * 2.0)) return End;
		}
		return INDEX_NONE;
	}
	bool WorldPort(const AssemblyRigidPartMetrics::FConnectionPort& Port, const FTransform& Pose,
		FVector2D& OutPoint, FVector2D& OutAxis)
	{
		const FVector Point = Pose.TransformPosition(FVector(Port.Point.X, 0.0, Port.Point.Y));
		const FVector Scale = Pose.GetScale3D();
		if (Scale.X <= 0.0 || Scale.Z <= 0.0) return false;
		// End-face normals use inverse-transpose scaling, including custom diagonal end faces.
		const FVector Axis = Pose.GetRotation().RotateVector(FVector(Port.OutwardAxis.X / Scale.X, 0.0, Port.OutwardAxis.Y / Scale.Z));
		OutPoint = FVector2D(Point.X, Point.Z);
		OutAxis = FVector2D(Axis.X, Axis.Z).GetSafeNormal();
		return IsFinite(OutPoint) && IsFinite(OutAxis) && !OutAxis.IsNearlyZero();
	}
	float AxisAngle(FVector2D Axis)
	{
		// In the established XZ plane, positive rotation about +Y turns +X towards -Z.
		return FMath::RadiansToDegrees(FMath::Atan2(-Axis.Y, Axis.X));
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

bool AssemblyFreeRules::GetEndpointConnection(const FAssemblyFreeSettings& S,
	const FAssemblyBoneDefinition* A, const FTransform& PoseA,
	const FAssemblyBoneDefinition* B, const FTransform& PoseB, FContact& OutContact)
{
	OutContact = FContact();
	if (!ValidSettings(S) || !CanConnect(A, B)) return false;
	FShape ShapeA, ShapeB;
	FText Reason;
	if (A)
	{
		if (!AssemblySurfaceGeometry::BuildShape(*A, PoseA, ShapeA, Reason)) return false;
	}
	else ShapeA = AssemblySurfaceGeometry::MakeCircle(S.CoreLocalPosition, S.CoreRadius);
	if (B)
	{
		if (!AssemblySurfaceGeometry::BuildShape(*B, PoseB, ShapeB, Reason)) return false;
	}
	else ShapeB = AssemblySurfaceGeometry::MakeCircle(S.CoreLocalPosition, S.CoreRadius);
	if (AssemblySurfaceGeometry::Overlaps(ShapeA, ShapeB)) return false;
	const bool bBoneA = A && A->PartKind == EAssemblyRigidPartKind::Bone;
	const FAssemblyBoneDefinition& BoneDefinition = bBoneA ? *A : *B;
	const FTransform& BonePose = bBoneA ? PoseA : PoseB;
	const FShape& BoneShape = bBoneA ? ShapeA : ShapeB;
	const FShape& ConnectorShape = bBoneA ? ShapeB : ShapeA;
	FBoneEnds Ends;
	if (!BuildBoneEnds(BoneDefinition, BonePose, Ends)) return false;
	if (!ConnectorShape.bCircle)
	{
		const FAssemblyBoneDefinition* ConnectorDefinition = bBoneA ? B : A;
		const FTransform& ConnectorPose = bBoneA ? PoseB : PoseA;
		TArray<AssemblyRigidPartMetrics::FConnectionPort> Ports;
		if (!ConnectorDefinition || !AssemblyRigidPartMetrics::GetConnectionPorts(*ConnectorDefinition, Ports, Reason)) return false;
		bool bFound = false;
		for (const AssemblyRigidPartMetrics::FConnectionPort& Port : Ports)
		{
			FVector2D ConnectorPoint, ConnectorAxis;
			if (!WorldPort(Port, ConnectorPose, ConnectorPoint, ConnectorAxis)) continue;
			for (int32 End = 0; End < 2; ++End)
			{
				const FVector2D Outward = Ends.Axis * (End == 0 ? -1.0 : 1.0);
				if (FVector2D::DotProduct(Outward, ConnectorAxis) >= 0.0
					|| FMath::Abs(FVector2D::CrossProduct(Outward, ConnectorAxis)) > UE_KINDA_SMALL_NUMBER) continue;
				const FVector2D BonePoint = Ends.Points[End], Gap = ConnectorPoint - BonePoint;
				const double DistanceSquared = Gap.SizeSquared();
				if (!FMath::IsFinite(DistanceSquared) || FMath::Abs(FVector2D::CrossProduct(Outward, Gap)) > GeometryTolerance
					|| FVector2D::DotProduct(Gap, Outward) < -GeometryTolerance
					|| DistanceSquared > FMath::Square(S.ConnectionDistance + GeometryTolerance)
					|| !AssemblySurfaceGeometry::IsOnBoundary(BoneShape, BonePoint)) continue;
				if (bFound && DistanceSquared >= OutContact.DistanceSquared) continue;
				OutContact.PointA = bBoneA ? BonePoint : ConnectorPoint;
				OutContact.PointB = bBoneA ? ConnectorPoint : BonePoint;
				OutContact.DistanceSquared = DistanceSquared;
				bFound = true;
			}
		}
		return bFound;
	}
	const FVector2D ToConnector = ConnectorShape.Center - BoneShape.Center;
	if (FMath::Abs(FVector2D::CrossProduct(Ends.Axis, ToConnector)) > GeometryTolerance) return false;
	const int32 End = FVector2D::DotProduct(ToConnector, Ends.Axis) > 0.0 ? 1 : 0;
	const FVector2D Outward = Ends.Axis * (End == 0 ? -1.0 : 1.0);
	const FVector2D BonePoint = Ends.Points[End];
	if (FVector2D::DotProduct(ConnectorShape.Center - BonePoint, Outward) <= GeometryTolerance) return false;
	FVector2D ConnectorPoint;
	if (!RayBoundary(ConnectorShape, -Outward, ConnectorPoint)
		|| !AssemblySurfaceGeometry::IsOnBoundary(BoneShape, BonePoint)) return false;
	const FVector2D Gap = ConnectorPoint - BonePoint;
	const double DistanceSquared = Gap.SizeSquared();
	if (!FMath::IsFinite(DistanceSquared) || FVector2D::DotProduct(Gap, Outward) < -GeometryTolerance
		|| DistanceSquared > FMath::Square(S.ConnectionDistance + GeometryTolerance)) return false;
	OutContact.PointA = bBoneA ? BonePoint : ConnectorPoint;
	OutContact.PointB = bBoneA ? ConnectorPoint : BonePoint;
	OutContact.DistanceSquared = DistanceSquared;
	return true;
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
		TArray<AssemblyRigidPartMetrics::FConnectionPort> Ports;
		if (!AssemblyRigidPartMetrics::GetConnectionPorts(D, Ports, OutReason)) return false;
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
	FText SnapFailure;
	if (S.bEnableSurfaceSnapping)
	{
		struct FCandidate
		{
			FAssemblyPlacementResult Placement;
			double DistanceSquared = 0.0;
			double AngleDelta = 0.0;
			bool bCore = false;
			FGuid Target;
		};
		auto Collect = [&](const FSurfaceTarget& Target, double Threshold, TArray<FCandidate>& Out)
		{
			if (!CanConnect(Definition, Target.Definition)) return;
			const bool bSameTarget = PreviousSnap.bSnapped && (PreviousSnap.bCore ? Target.bCore
				: (!Target.bCore && PreviousSnap.TargetId.IsValid() && PreviousSnap.TargetId == Target.Id));
			auto RejectSnap = [&](const FText& Reason)
			{
				SnapFailure = Reason.IsEmpty()
					? LOCTEXT("EndpointSnapBlocked", "端点吸附后的轮廓被阻挡，或骨头轴线没有对齐关节端口；请调整位置或角度。") : Reason;
			};
			auto Append = [&](FAssemblyPlacementResult& Result, double DistanceSquared, const FShape& ResolvedShape)
			{
				FContact Contact;
				if (AssemblySurfaceGeometry::Overlaps(ResolvedShape, Target.Shape)
					|| !ValidateAgainstTargets(S, ResolvedShape, Targets, Result.Reason)
					|| !GetEndpointConnection(S, Definition, Result.LocalVisualTransform, Target.Definition, Target.Pose, Contact)
					|| Contact.DistanceSquared > GeometryTolerance * GeometryTolerance * 4.0)
				{
					RejectSnap(Result.Reason);
					return false;
				}
				Result.SurfaceSnap.bSnapped = true;
				Result.SurfaceSnap.bCore = Target.bCore;
				Result.SurfaceSnap.TargetId = Target.Id;
				Result.SurfaceSnap.SourceLocalAnchor = LocalPoint(Result.LocalVisualTransform, Contact.PointA);
				Result.SurfaceSnap.TargetLocalAnchor = Target.bCore ? Contact.PointB - S.CoreLocalPosition : LocalPoint(Target.Pose, Contact.PointB);
				const double AngleDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(Raw.RotationDegrees, Result.RotationDegrees));
				Out.Add({ MoveTemp(Result), DistanceSquared, AngleDelta, Target.bCore, Target.Id });
				return true;
			};
			if (Definition->PartKind == EAssemblyRigidPartKind::Bone && Target.Shape.bCircle)
			{
				const FVector2D FromCenter = RawPosition - Target.Shape.Center;
				const double RawRadius = FromCenter.Size();
				if (!FMath::IsFinite(RawRadius) || RawRadius <= GeometryTolerance)
				{
					SnapFailure = LOCTEXT("RadialDirectionUndefined", "骨头不能在圆心位置确定朝向，请将鼠标移到圆周附近。");
					return;
				}
				const FVector2D Outward = FromCenter / RawRadius;
				const bool bRetainRadial = bSameTarget && PreviousSnap.bRadialAligned;
				double RetainedDistanceSquared = 0.0;
				if (bRetainRadial)
				{
					// Measure the original pointer, calibrated when contact was acquired. Reusing the
					// old horizontal contour here would drop long bones while sliding to the circle top.
					const double ContactRadius = Target.Shape.Radius + PreviousSnap.RadialPointerOffset;
					if (!FMath::IsFinite(PreviousSnap.RadialPointerOffset) || !FMath::IsFinite(PreviousSnap.RadialInputAngle)
						|| !FMath::IsFinite(ContactRadius) || ContactRadius < 0.0) return;
					const double RadialGap = RawRadius - ContactRadius;
					if (FMath::Abs(RadialGap) > Threshold + GeometryTolerance) return;
					RetainedDistanceSquared = FMath::Square(RadialGap);
				}
				double CandidateDistanceSquared = RetainedDistanceSquared;
				double PointerOffset = PreviousSnap.RadialPointerOffset;
				if (!bRetainRadial)
				{
					bool bAcquired = false;
					TArray<FContact> RawContacts;
					AssemblySurfaceGeometry::GetContactCandidates(MovingShape, Target.Shape, RawContacts);
					for (const FContact& Contact : RawContacts)
					{
						// Acquisition uses the original requested angle, so even side-first contact rotates in.
						if (Contact.DistanceSquared > FMath::Square(Threshold + GeometryTolerance)) continue;
						const FShape OriginalContactShape = Translated(MovingShape, Contact.PointB - Contact.PointA);
						FContact OriginalContact;
						if (AssemblySurfaceGeometry::Overlaps(OriginalContactShape, Target.Shape)
							|| !AssemblySurfaceGeometry::ClosestContact(OriginalContactShape, Target.Shape, OriginalContact)
							|| OriginalContact.DistanceSquared > GeometryTolerance * GeometryTolerance * 4.0) continue;
						const double ContactRadius = FVector2D::Distance(OriginalContactShape.Center, Target.Shape.Center);
						if (!FMath::IsFinite(ContactRadius)) continue;
						CandidateDistanceSquared = Contact.DistanceSquared;
						PointerOffset = ContactRadius - Target.Shape.Radius;
						bAcquired = true;
						break; // Raw contacts are sorted by distance and contact coordinates.
					}
					if (!bAcquired) return;
				}
				// The existing game plane is XZ; a positive angle around +Y turns +X towards -Z.
				const float RadialAngle = FMath::RadiansToDegrees(FMath::Atan2(-Outward.Y, Outward.X));
				const bool bManualAngleUnchanged = bRetainRadial
					&& FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousSnap.RadialInputAngle, Raw.RotationDegrees)) <= UE_KINDA_SMALL_NUMBER;
				const bool bXOutward = bManualAngleUnchanged ? PreviousSnap.bRadialXOutward
					: FMath::Abs(FMath::FindDeltaAngleDegrees(Raw.RotationDegrees, RadialAngle))
						<= FMath::Abs(FMath::FindDeltaAngleDegrees(Raw.RotationDegrees, RadialAngle + 180.0f));
				const float ResolvedAngle = RadialAngle + (bXOutward ? 0.0f : 180.0f);
				const FAssemblyPlacementResult AtOrigin = MakePlacement(FVector2D::ZeroVector, ResolvedAngle);
				FShape RotatedShape;
				FText ShapeReason;
				FBoneEnds Ends;
				if (!AtOrigin.bValid || !AssemblySurfaceGeometry::BuildShape(*Definition, AtOrigin.LocalVisualTransform, RotatedShape, ShapeReason)
					|| !BuildBoneEnds(*Definition, AtOrigin.LocalVisualTransform, Ends))
				{
					RejectSnap(ShapeReason);
					return;
				}
				const FVector2D EndOffset = Ends.Points[bXOutward ? 0 : 1];
				FAssemblyPlacementResult Result = MakePlacement(Target.Shape.Center + Outward * Target.Shape.Radius - EndOffset, ResolvedAngle);
				if (!Result.bValid) { RejectSnap(Result.Reason); return; }
				const FShape ResolvedShape = Translated(RotatedShape, Result.LocalPosition);
				Result.SurfaceSnap.bRadialAligned = true;
				Result.SurfaceSnap.RadialPointerOffset = PointerOffset;
				Result.SurfaceSnap.RadialInputAngle = Raw.RotationDegrees;
				Result.SurfaceSnap.bRadialXOutward = bXOutward;
				Append(Result, CandidateDistanceSquared, ResolvedShape);
				return; // A blocked or distant radial candidate never falls back to horizontal circle snapping.
			}
			const bool bMovingBone = Definition->PartKind == EAssemblyRigidPartKind::Bone;
			const FAssemblyBoneDefinition& BoneDefinition = bMovingBone ? *Definition : *Target.Definition;
			const FShape& ConnectorShape = bMovingBone ? Target.Shape : MovingShape;
			if (!ConnectorShape.bCircle)
			{
				const FAssemblyBoneDefinition& ConnectorDefinition = bMovingBone ? *Target.Definition : *Definition;
				TArray<AssemblyRigidPartMetrics::FConnectionPort> Ports, BonePorts;
				FText PortReason;
				if (!AssemblyRigidPartMetrics::GetConnectionPorts(ConnectorDefinition, Ports, PortReason)
					|| !AssemblyRigidPartMetrics::GetConnectionPorts(BoneDefinition, BonePorts, PortReason) || BonePorts.Num() != 2)
				{
					RejectSnap(PortReason); return;
				}
				const FVector2D PreviousBoneAnchor = bMovingBone ? PreviousSnap.SourceLocalAnchor : PreviousSnap.TargetLocalAnchor;
				const FVector2D PreviousPortAnchor = bMovingBone ? PreviousSnap.TargetLocalAnchor : PreviousSnap.SourceLocalAnchor;
				const int32 RetainedEnd = bSameTarget ? BonePorts.IndexOfByPredicate([&](const AssemblyRigidPartMetrics::FConnectionPort& P)
				{
					return P.Point.Equals(PreviousBoneAnchor, GeometryTolerance * 2.0);
				}) : INDEX_NONE;
				const int32 RetainedPort = bSameTarget ? Ports.IndexOfByPredicate([&](const AssemblyRigidPartMetrics::FConnectionPort& P)
				{
					return P.Point.Equals(PreviousPortAnchor, GeometryTolerance * 2.0);
				}) : INDEX_NONE;
				for (int32 PortIndex = 0; PortIndex < Ports.Num(); ++PortIndex)
				{
					if (RetainedPort != INDEX_NONE && PortIndex != RetainedPort) continue;
					const AssemblyRigidPartMetrics::FConnectionPort& Port = Ports[PortIndex];
					for (int32 End = 0; End < 2; ++End)
					{
						if (RetainedEnd != INDEX_NONE && End != RetainedEnd) continue;
						FVector2D FixedPoint, FixedAxis;
						if (!WorldPort(bMovingBone ? Port : BonePorts[End], Target.Pose, FixedPoint, FixedAxis)) continue;
						const AssemblyRigidPartMetrics::FConnectionPort& MovingPort = bMovingBone ? BonePorts[End] : Port;
						const float ResolvedAngle = AxisAngle(-FixedAxis) - AxisAngle(MovingPort.OutwardAxis);
						const FAssemblyPlacementResult AtOrigin = MakePlacement(FVector2D::ZeroVector, ResolvedAngle);
						FVector2D EndOffset, UnusedAxis;
						if (!AtOrigin.bValid || !WorldPort(MovingPort, AtOrigin.LocalVisualTransform, EndOffset, UnusedAxis)) continue;
						const FVector2D CandidatePosition = FixedPoint - EndOffset;
						const double DistanceSquared = FVector2D::DistSquared(CandidatePosition, RawPosition);
						if (!FMath::IsFinite(DistanceSquared) || DistanceSquared > FMath::Square(Threshold + GeometryTolerance)) continue;
						FAssemblyPlacementResult Result = MakePlacement(CandidatePosition, ResolvedAngle);
						FShape ResolvedShape;
						if (!Result.bValid || !AssemblySurfaceGeometry::BuildShape(*Definition, Result.LocalVisualTransform, ResolvedShape, Result.Reason))
						{
							RejectSnap(Result.Reason); continue;
						}
						Append(Result, DistanceSquared, ResolvedShape);
					}
				}
				return; // Noncircular pieces use their end ports, never a center ray or arbitrary side.
			}
			const FTransform BonePose = bMovingBone ? MakePlacement(FVector2D::ZeroVector, Angle).LocalVisualTransform : Target.Pose;
			FBoneEnds Ends;
			if (!BuildBoneEnds(BoneDefinition, BonePose, Ends)) return;
			// Existing anchors identify the chosen end without adding any serialized or reflected state.
			const int32 RetainedEnd = bSameTarget ? PreviousEnd(Ends, BonePose,
				bMovingBone ? PreviousSnap.SourceLocalAnchor : PreviousSnap.TargetLocalAnchor) : INDEX_NONE;
			for (int32 End = 0; End < 2; ++End)
			{
				if (RetainedEnd != INDEX_NONE && End != RetainedEnd) continue;
				const FVector2D Outward = Ends.Axis * (End == 0 ? -1.0 : 1.0);
				FVector2D ConnectorPoint;
				if (!RayBoundary(ConnectorShape, -Outward, ConnectorPoint)) continue;
				const FVector2D CandidatePosition = bMovingBone ? ConnectorPoint - Ends.Points[End]
					: Ends.Points[End] - (ConnectorPoint - ConnectorShape.Center);
				const FVector2D Translation = CandidatePosition - RawPosition;
				const double DistanceSquared = Translation.SizeSquared();
				if (!FMath::IsFinite(DistanceSquared) || DistanceSquared > FMath::Square(Threshold + GeometryTolerance)) continue;
				FAssemblyPlacementResult Result = MakePlacement(CandidatePosition, Angle);
				if (!Result.bValid) { RejectSnap(Result.Reason); continue; }
				Append(Result, DistanceSquared, Translated(MovingShape, Translation));
			}
		};
		auto Sort = [](TArray<FCandidate>& Candidates)
		{
			Candidates.Sort([](const FCandidate& A, const FCandidate& B)
			{
				// Symmetric ports can differ only by quaternion roundoff. Resolve such equal-distance
				// poses by the user's angle rather than flipping between opposite ends unpredictably.
				const double DistanceA = FMath::RoundToDouble(A.DistanceSquared / FMath::Square(GeometryTolerance));
				const double DistanceB = FMath::RoundToDouble(B.DistanceSquared / FMath::Square(GeometryTolerance));
				if (DistanceA != DistanceB) return DistanceA < DistanceB;
				if (A.bCore != B.bCore) return A.bCore;
				if (A.Target != B.Target) return GuidBefore(A.Target, B.Target);
				if (A.AngleDelta != B.AngleDelta) return A.AngleDelta < B.AngleDelta;
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
	if (!SnapFailure.IsEmpty())
	{
		// Do not silently commit the raw pose after an eligible aligned endpoint pose was blocked.
		// A different successful target still wins above; distant free staging remains available.
		Raw.bValid = false;
		Raw.Reason = SnapFailure;
		return Raw;
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
	for (int32 I = 0; I < Targets.Num(); ++I)
	{
		for (int32 K = I + 1; K < Targets.Num(); ++K)
		{
			const FSurfaceTarget* A = &Targets[I];
			const FSurfaceTarget* B = &Targets[K];
			if (!CanConnect(A->Definition, B->Definition)) continue;
			// Always evaluate the same pair orientation, independent of installed-array order.
			if (!A->bCore && GuidBefore(B->Id, A->Id)) Swap(A, B);
			FContact Contact;
			if (GetEndpointConnection(S, A->Definition, A->Pose, B->Definition, B->Pose, Contact))
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
			if (bPartA) Joint.Kind = C.A->Definition->DefaultJointKind;
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
