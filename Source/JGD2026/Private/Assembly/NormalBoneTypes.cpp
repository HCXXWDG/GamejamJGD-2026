#include "Assembly/NormalBoneTypes.h"

#define LOCTEXT_NAMESPACE "NormalBoneTypes"

bool NormalBoneMetrics::Evaluate(const FNormalBoneParameters& P, FNormalBonePhysicalStats& OutStats, FText& OutReason)
{
	OutStats = FNormalBonePhysicalStats();
	OutReason = FText::GetEmpty();
	if (static_cast<uint8>(P.Length) > static_cast<uint8>(EBoneLengthTier::Long)
		|| static_cast<uint8>(P.Thickness) > static_cast<uint8>(EBoneThicknessTier::Thick)
		|| static_cast<uint8>(P.Density) > static_cast<uint8>(EBoneDensityTier::Dense)
		|| !FMath::IsFinite(P.UnitLength) || P.UnitLength <= 0.0f
		|| !FMath::IsFinite(P.StandardDensityKgPerSquareUnit) || P.StandardDensityKgPerSquareUnit <= 0.0f)
	{
		OutReason = LOCTEXT("InvalidTraits", "骨头词条必须有效，单位长度与标准密度必须为正的有限数值。");
		return false;
	}
	const double LengthUnits = 1.0 + static_cast<uint8>(P.Length) * 0.5;
	const double ThicknessUnits = 0.1 * (static_cast<uint8>(P.Thickness) + 1);
	const double DensityMultiplier = static_cast<uint8>(P.Density) + 1.0;
	const double Length = P.UnitLength * LengthUnits;
	const double Thickness = P.UnitLength * ThicknessUnits;
	const double Mass = LengthUnits * ThicknessUnits * DensityMultiplier * P.StandardDensityKgPerSquareUnit;
	if (!FMath::IsFinite(Length) || !FMath::IsFinite(Thickness) || !FMath::IsFinite(Mass)
		|| Length > TNumericLimits<float>::Max() || Thickness > TNumericLimits<float>::Max()
		|| Mass > TNumericLimits<float>::Max() || static_cast<float>(Mass) <= 0.0f
		|| static_cast<float>(Length) <= UE_SMALL_NUMBER || static_cast<float>(Thickness) <= UE_SMALL_NUMBER)
	{
		OutReason = LOCTEXT("Overflow", "骨头尺寸或质量超出有效范围，请降低单位长度或标准密度。");
		return false;
	}
	OutStats.bValid = true;
	OutStats.Length = static_cast<float>(Length);
	OutStats.Thickness = static_cast<float>(Thickness);
	OutStats.DensityMultiplier = static_cast<float>(DensityMultiplier);
	OutStats.AreaInSquareUnits = static_cast<float>(LengthUnits * ThicknessUnits);
	OutStats.MassKg = static_cast<float>(Mass);
	return true;
}

FText NormalBoneMetrics::Describe(const FNormalBoneParameters& P)
{
	FNormalBonePhysicalStats Stats;
	FText Reason;
	if (!Evaluate(P, Stats, Reason))
	{
		return LOCTEXT("InvalidName", "骨骼-无效词条");
	}
	const FText Lengths[] = { LOCTEXT("Short", "短"), LOCTEXT("Standard", "一般长度"), LOCTEXT("Long", "长") };
	const FText Thicknesses[] = { LOCTEXT("Thin", "细"), LOCTEXT("MediumThickness", "中等粗细"), LOCTEXT("Thick", "粗") };
	const FText Densities[] = { LOCTEXT("Loose", "疏松"), LOCTEXT("MediumDensity", "中等密度"), LOCTEXT("Dense", "致密") };
	return FText::Format(LOCTEXT("BoneName", "骨骼-{0}-{1}-{2}"),
		Lengths[static_cast<uint8>(P.Length)], Thicknesses[static_cast<uint8>(P.Thickness)], Densities[static_cast<uint8>(P.Density)]);
}

FIntPoint NormalBoneMetrics::RequiredGridSize(const FNormalBonePhysicalStats& Stats, float CellSize)
{
	if (!Stats.bValid || !FMath::IsFinite(CellSize) || CellSize <= 0.0f)
	{
		return FIntPoint::ZeroValue;
	}
	const double Columns = static_cast<double>(Stats.Length) / CellSize;
	const double Rows = static_cast<double>(Stats.Thickness) / CellSize;
	if (!FMath::IsFinite(Columns) || !FMath::IsFinite(Rows) || Columns >= MAX_int32 || Rows >= MAX_int32)
	{
		return FIntPoint::ZeroValue;
	}
	return FIntPoint(FMath::Max(1, FMath::CeilToInt32(Columns)), FMath::Max(1, FMath::CeilToInt32(Rows)));
}

#undef LOCTEXT_NAMESPACE
