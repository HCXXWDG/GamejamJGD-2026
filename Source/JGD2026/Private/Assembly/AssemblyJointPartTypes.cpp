#include "Assembly/AssemblyJointPartTypes.h"
#include "Assembly/AssemblyTypes.h"

#define LOCTEXT_NAMESPACE "AssemblyJointPartTypes"

namespace
{
	bool IsJointPartShapeValid(EAssemblyJointPartShape Shape)
	{
		return static_cast<uint8>(Shape) <= static_cast<uint8>(EAssemblyJointPartShape::FixedRound);
	}

	TArray<FVector2D> DefaultJointPartPorts(EAssemblyJointPartShape Shape, FVector2D Size)
	{
		const double X = Size.X * 0.5, Z = Size.Y * 0.5;
		switch (Shape)
		{
		case EAssemblyJointPartShape::Round:
		case EAssemblyJointPartShape::FixedRound: return { FVector2D(-X, 0), FVector2D(X, 0), FVector2D(0, Z), FVector2D(0, -Z) };
		case EAssemblyJointPartShape::Bar: return { FVector2D(-X, 0), FVector2D(X, 0) };
		case EAssemblyJointPartShape::Corner: return { FVector2D(-X, Z * 0.5), FVector2D(X * 0.5, -Z) };
		case EAssemblyJointPartShape::Tee: return { FVector2D(-X, -Z * 0.5), FVector2D(X, -Z * 0.5), FVector2D(0, Z) };
		case EAssemblyJointPartShape::Cross: return { FVector2D(-X, 0), FVector2D(X, 0), FVector2D(0, Z), FVector2D(0, -Z) };
		case EAssemblyJointPartShape::Fork: return { FVector2D(-X, 0), FVector2D(X, Z * 0.5), FVector2D(X, -Z * 0.5) };
		default: return {};
		}
	}
}

FVector2D AssemblyRigidPartMetrics::GetJointArtworkPixelSize(EAssemblyJointPartShape Shape)
{
	const FVector2D ArtworkSizes[] = { FVector2D(640, 640), FVector2D(640, 320), FVector2D(640, 640),
		FVector2D(960, 640), FVector2D(960, 960), FVector2D(640, 640), FVector2D(640, 640) };
	return IsJointPartShapeValid(Shape) ? ArtworkSizes[static_cast<uint8>(Shape)] : FVector2D::ZeroVector;
}

TArray<FVector2D> AssemblyRigidPartMetrics::GetConnectionPoints(const FAssemblyBoneDefinition& D)
{
	if (D.PartKind == EAssemblyRigidPartKind::Bone)
	{
		FNormalBonePhysicalStats Stats;
		FText Reason;
		if (!NormalBoneMetrics::Evaluate(D.BoneParameters, Stats, Reason)) { return {}; }
		return { FVector2D(-Stats.Length * 0.5, 0), FVector2D(Stats.Length * 0.5, 0) };
	}
	if (D.PartKind != EAssemblyRigidPartKind::JointPart || !IsJointPartShapeValid(D.JointShape)) { return {}; }
	return D.ConnectionPoints.IsEmpty() ? DefaultJointPartPorts(D.JointShape, D.JointPartSize) : D.ConnectionPoints;
}

bool AssemblyRigidPartMetrics::Evaluate(const FAssemblyBoneDefinition& D, FNormalBonePhysicalStats& OutStats, FText& OutReason)
{
	OutStats = FNormalBonePhysicalStats();
	OutReason = FText::GetEmpty();
	if (D.PartKind == EAssemblyRigidPartKind::Bone) { return NormalBoneMetrics::Evaluate(D.BoneParameters, OutStats, OutReason); }
	if (D.PartKind != EAssemblyRigidPartKind::JointPart || !IsJointPartShapeValid(D.JointShape)
		|| static_cast<uint8>(D.DefaultJointKind) > static_cast<uint8>(EAssemblyJointKind::BreakableFixed)
		|| !FMath::IsFinite(D.JointPartSize.X) || !FMath::IsFinite(D.JointPartSize.Y)
		|| D.JointPartSize.X <= UE_SMALL_NUMBER || D.JointPartSize.Y <= UE_SMALL_NUMBER
		|| D.JointPartSize.X > TNumericLimits<float>::Max() || D.JointPartSize.Y > TNumericLimits<float>::Max())
	{
		OutReason = LOCTEXT("InvalidJointPart", "关节零件种类、连接类型与宽高必须有效，尺寸必须为正的有限数值。");
		return false;
	}
	FNormalBonePhysicalStats Baseline;
	if (!NormalBoneMetrics::Evaluate(D.BoneParameters, Baseline, OutReason)) { return false; }
	const bool bHingeDefault = D.DefaultJointKind == EAssemblyJointKind::Hinge || D.DefaultJointKind == EAssemblyJointKind::BreakableHinge;
	if ((D.JointShape == EAssemblyJointPartShape::Round) != bHingeDefault)
	{
		OutReason = LOCTEXT("JointPartKindFamily", "紫色圆形关节只能使用活动或可断活动连接；黄色关节只能使用固定或可断固定连接。改变活动性质需要更换零件。");
		return false;
	}
	const FVector2D Canvas = GetJointArtworkPixelSize(D.JointShape);
	const double ScaleX = D.JointPartSize.X / Canvas.X, ScaleZ = D.JointPartSize.Y / Canvas.Y;
	if (FMath::Abs(ScaleX - ScaleZ) > FMath::Max(ScaleX, ScaleZ) * 0.0001)
	{
		OutReason = LOCTEXT("JointArtworkRatio", "关节零件宽高需保持原美术画布的比例，请统一缩放两个尺寸。");
		return false;
	}
	const TArray<FVector2D> Ports = GetConnectionPoints(D);
	if (Ports.IsEmpty() || Ports.Num() > 16)
	{
		OutReason = LOCTEXT("InvalidPortCount", "关节零件需要1至16个有效连接槽位。");
		return false;
	}
	for (int32 I = 0; I < Ports.Num(); ++I)
	{
		const FVector2D Port = Ports[I];
		if (!FMath::IsFinite(Port.X) || !FMath::IsFinite(Port.Y)
			|| FMath::Abs(Port.X) > D.JointPartSize.X * 0.5 + UE_KINDA_SMALL_NUMBER
			|| FMath::Abs(Port.Y) > D.JointPartSize.Y * 0.5 + UE_KINDA_SMALL_NUMBER)
		{
			OutReason = LOCTEXT("PortOutsidePart", "关节连接槽位必须位于自身局部轮廓内。");
			return false;
		}
		for (int32 K = 0; K < I; ++K)
		{
			if (Ports[K].Equals(Port, UE_KINDA_SMALL_NUMBER))
			{
				OutReason = LOCTEXT("DuplicatePort", "同一关节零件的连接槽位不能重复。");
				return false;
			}
		}
	}
	const double Unit = D.BoneParameters.UnitLength;
	const double Area = (D.JointPartSize.X / Unit) * (D.JointPartSize.Y / Unit);
	const double Mass = Area * Baseline.DensityMultiplier * D.BoneParameters.StandardDensityKgPerSquareUnit;
	if (!FMath::IsFinite(Area) || !FMath::IsFinite(Mass) || Area > TNumericLimits<float>::Max()
		|| Mass > TNumericLimits<float>::Max() || static_cast<float>(Area) <= 0 || static_cast<float>(Mass) <= 0)
	{
		OutReason = LOCTEXT("JointMetricsOverflow", "关节零件面积或质量超出有效范围。");
		return false;
	}
	OutStats.bValid = true;
	OutStats.Length = static_cast<float>(D.JointPartSize.X);
	OutStats.Thickness = static_cast<float>(D.JointPartSize.Y);
	OutStats.DensityMultiplier = Baseline.DensityMultiplier;
	OutStats.AreaInSquareUnits = static_cast<float>(Area);
	OutStats.MassKg = static_cast<float>(Mass);
	return true;
}

FAssemblyBoneDefinition AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape Shape, float ArtUnitsPerPixel)
{
	FAssemblyBoneDefinition D;
	D.PartKind = EAssemblyRigidPartKind::JointPart;
	D.JointShape = Shape;
	D.DefaultJointKind = Shape == EAssemblyJointPartShape::Round ? EAssemblyJointKind::Hinge : EAssemblyJointKind::Fixed;
	D.InitialQuantity = 2;
	D.Footprint.Reset();
	const FName TypeIds[] = { TEXT("Joint_Round"), TEXT("Joint_Bar"), TEXT("Joint_Corner"), TEXT("Joint_Tee"), TEXT("Joint_Cross"), TEXT("Joint_Fork"), TEXT("Joint_FixedRound") };
	const FText Names[] = { LOCTEXT("Round", "圆形关节"), LOCTEXT("Bar", "直线关节"), LOCTEXT("Corner", "转角关节"),
		LOCTEXT("Tee", "T形关节"), LOCTEXT("Cross", "十字关节"), LOCTEXT("Fork", "分叉关节"), LOCTEXT("FixedRound", "固定圆形关节") };
	if (!IsJointPartShapeValid(Shape) || !FMath::IsFinite(ArtUnitsPerPixel) || ArtUnitsPerPixel <= 0.0f)
	{
		D.TypeId = NAME_None;
		D.JointPartSize = FVector2D::ZeroVector;
		return D;
	}
	const uint8 Index = static_cast<uint8>(Shape);
	D.TypeId = TypeIds[Index];
	D.DisplayName = Names[Index];
	D.JointPartSize = GetJointArtworkPixelSize(Shape) * ArtUnitsPerPixel;
	D.ConnectionPoints = DefaultJointPartPorts(Shape, D.JointPartSize);
	return D;
}

#undef LOCTEXT_NAMESPACE
