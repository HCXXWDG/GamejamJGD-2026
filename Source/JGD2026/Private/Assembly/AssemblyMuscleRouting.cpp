#include "Assembly/AssemblyMuscleRouting.h"
#include "Assembly/AssemblySurfaceGeometry.h"

namespace AssemblyMuscleRouting
{
	namespace
	{
		constexpr double Tau = 2 * UE_DOUBLE_PI;
		double Cross(FVector2D A, FVector2D B) { return A.X * B.Y - A.Y * B.X; }
		double PositiveAngle(double Value)
		{
			Value = FMath::Fmod(Value, Tau);
			return Value < 0 ? Value + Tau : Value;
		}
		FVector2D Planar(FVector P) { return FVector2D(P.X, P.Z); }
		FVector Direction(FVector2D V) { return FVector(V.X, 0, V.Y); }
		bool OtherEnd(const FAssemblyJointInstance& Joint, FGuid Bone, FGuid& Other)
		{
			if (Joint.BoneA == Bone) { Other = Joint.BoneB; return true; }
			if (Joint.BoneB == Bone) { Other = Joint.BoneA; return true; }
			return false;
		}
	}

	bool BuildGuide(const FAssemblyMuscleInstance& Muscle, const TArray<FAssemblyBoneInstance>& Bones,
		const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyJointInstance>& Joints,
		const FAssemblyFreeSettings& Settings, FGuide& Out)
	{
		Out = FGuide();
		if (Muscle.EndpointA.BoneId == Muscle.EndpointB.BoneId) return false;
		const FAssemblyBoneInstance* BoneA = Bones.FindByPredicate([&](const auto& Bone) { return Bone.InstanceId == Muscle.EndpointA.BoneId; });
		const FAssemblyBoneInstance* BoneB = Bones.FindByPredicate([&](const auto& Bone) { return Bone.InstanceId == Muscle.EndpointB.BoneId; });
		if (!BoneA || !BoneB) return false;
		const FVector2D PointA = Planar(BoneA->LocalVisualTransform.TransformPosition(Direction(Muscle.EndpointA.BoneLocalPoint)));
		const FVector2D PointB = Planar(BoneB->LocalVisualTransform.TransformPosition(Direction(Muscle.EndpointB.BoneLocalPoint)));
		double BestDistance = TNumericLimits<double>::Max();
		for (const FAssemblyJointInstance& EdgeA : Joints)
		{
			FGuid Hub;
			if (!OtherEnd(EdgeA, Muscle.EndpointA.BoneId, Hub) || Hub == Muscle.EndpointB.BoneId) continue;
			const FAssemblyJointInstance* EdgeB = Joints.FindByPredicate([&](const auto& Edge)
			{
				FGuid Other;
				return OtherEnd(Edge, Muscle.EndpointB.BoneId, Other) && Other == Hub;
			});
			if (!EdgeB) continue;
			FVector2D Center = Settings.CoreLocalPosition;
			FVector2D LocalCenter = FVector2D::ZeroVector;
			double Radius = Settings.CoreRadius;
			if (Hub.IsValid())
			{
				const auto* Part = Bones.FindByPredicate([&](const auto& Bone) { return Bone.InstanceId == Hub; });
				const auto* Definition = Part ? Definitions.FindByPredicate([&](const auto& Value) { return Value.TypeId == Part->TypeId; }) : nullptr;
				AssemblySurfaceGeometry::FShape Shape;
				FText Reason;
				if (!Definition || !AssemblySurfaceGeometry::BuildShape(*Definition, FTransform::Identity, Shape, Reason) || !Shape.bCircle) continue;
				LocalCenter = Shape.Center;
				Center = Planar(Part->LocalVisualTransform.TransformPosition(Direction(LocalCenter)));
				Radius = Shape.Radius;
			}
			if (Radius <= UE_SMALL_NUMBER) continue;
			const double Distance = FVector2D::DistSquared((PointA + PointB) * .5, Center);
			if (Distance >= BestDistance) continue;
			BestDistance = Distance;
			Out.bValid = true; Out.BoneId = Hub; Out.ConnectionA = EdgeA.InstanceId; Out.ConnectionB = EdgeB->InstanceId;
			Out.LocalCenter = LocalCenter; Out.Radius = Radius;
			const FVector2D CenterA = Planar(BoneA->LocalVisualTransform.GetLocation());
			const FVector2D CenterB = Planar(BoneB->LocalVisualTransform.GetLocation());
			// The two muscles can share the same bone pair and endpoint-angle sign.
			// Their signed offsets from the bone axes, rather than that angle, select the side.
			double Side = Cross(CenterA - Center, PointA - CenterA);
			if (FMath::Abs(Side) <= UE_KINDA_SMALL_NUMBER) Side = -Cross(CenterB - Center, PointB - CenterB);
			if (FMath::Abs(Side) <= UE_KINDA_SMALL_NUMBER) Side = Cross(PointA - Center, PointB - Center);
			Out.Sign = Side < 0 ? -1 : 1;
			Out.ReferenceSweep = PositiveAngle(Out.Sign * (FMath::Atan2(PointB.Y - Center.Y, PointB.X - Center.X)
				- FMath::Atan2(PointA.Y - Center.Y, PointA.X - Center.X)));
		}
		return Out.bValid;
	}

	bool IsGuideConnected(const FGuide& Guide, const TArray<FAssemblyJointInstance>& Joints)
	{
		return Guide.bValid && Joints.ContainsByPredicate([&](const auto& Edge) { return Edge.InstanceId == Guide.ConnectionA; })
			&& Joints.ContainsByPredicate([&](const auto& Edge) { return Edge.InstanceId == Guide.ConnectionB; });
	}

	bool MakeDirect(FVector A, FVector B, FPath& Out)
	{
		Out = FPath();
		Out.Length = FVector::Distance(A, B);
		if (!FMath::IsFinite(Out.Length) || Out.Length <= UE_SMALL_NUMBER) return false;
		Out.PullA = (B - A) / Out.Length; Out.PullB = -Out.PullA;
		Out.TouchA = A; Out.TouchB = B;
		Out.Points = { A, B };
		return true;
	}

	bool Evaluate(const FGuide& Guide, FVector A, FVector B, const FTransform& GuideWorld, FState& State, FPath& Out)
	{
		if (!Guide.bValid) return MakeDirect(A, B, Out);
		const FVector Center = GuideWorld.TransformPosition(Direction(Guide.LocalCenter));
		const FVector2D CA = Planar(A - Center), CB = Planar(B - Center);
		const FVector Scale = GuideWorld.GetScale3D().GetAbs();
		const double R = Guide.Radius * FMath::Max(Scale.X, Scale.Z);
		const double DA = CA.Size(), DB = CB.Size();
		if (!FMath::IsFinite(DA + DB + R) || R <= UE_SMALL_NUMBER || DA <= R + UE_SMALL_NUMBER || DB <= R + UE_SMALL_NUMBER)
			return false;
		const double AngleA = FMath::Atan2(CA.Y, CA.X), AngleB = FMath::Atan2(CB.Y, CB.X);
		const double RawSweep = PositiveAngle(Guide.Sign * (AngleB - AngleA));
		const double Previous = State.bInitialized ? State.Sweep : Guide.ReferenceSweep;
		State.Sweep = Previous + FMath::FindDeltaAngleRadians(Previous, RawSweep);
		State.bInitialized = true;
		const double AlphaA = FMath::Acos(FMath::Clamp(R / DA, -1.0, 1.0));
		const double AlphaB = FMath::Acos(FMath::Clamp(R / DB, -1.0, 1.0));
		const double Arc = State.Sweep - AlphaA - AlphaB;
		if (Arc <= UE_SMALL_NUMBER) return MakeDirect(A, B, Out);
		Out = FPath(); Out.bWrapped = true;
		const double StartAngle = AngleA + Guide.Sign * AlphaA;
		const auto CirclePoint = [&](double Angle)
		{
			return FVector(Center.X + R * FMath::Cos(Angle), (A.Y + B.Y) * .5, Center.Z + R * FMath::Sin(Angle));
		};
		Out.TouchA = CirclePoint(StartAngle);
		Out.TouchB = CirclePoint(StartAngle + Guide.Sign * Arc);
		Out.PullA = (Out.TouchA - A).GetSafeNormal(); Out.PullB = (Out.TouchB - B).GetSafeNormal();
		Out.Length = FMath::Sqrt(DA * DA - R * R) + FMath::Sqrt(DB * DB - R * R) + R * Arc;
		Out.Points.Add(A);
		// Circumscribed arc samples keep even linear rendering outside the guide surface.
		Out.Points.Add(Out.TouchA);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Arc / (UE_DOUBLE_PI / 48)), 1, 160);
		const double Step = Guide.Sign * Arc / Steps;
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			const double Middle = StartAngle + Step * (Index + .5);
			const double OuterRadius = R / FMath::Cos(Step * .5);
			Out.Points.Add(FVector(Center.X + OuterRadius * FMath::Cos(Middle), (A.Y + B.Y) * .5,
				Center.Z + OuterRadius * FMath::Sin(Middle)));
		}
		Out.Points.Add(Out.TouchB); Out.Points.Add(B);
		return FMath::IsFinite(Out.Length);
	}
}
