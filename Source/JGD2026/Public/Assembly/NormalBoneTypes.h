#pragma once

#include "CoreMinimal.h"
#include "NormalBoneTypes.generated.h"

UENUM(BlueprintType)
enum class EBoneLengthTier : uint8
{
	Short UMETA(DisplayName = "短"),
	Standard UMETA(DisplayName = "一般长度"),
	Long UMETA(DisplayName = "长")
};

UENUM(BlueprintType)
enum class EBoneThicknessTier : uint8
{
	Thin UMETA(DisplayName = "细"),
	Medium UMETA(DisplayName = "中等粗细"),
	Thick UMETA(DisplayName = "粗")
};

UENUM(BlueprintType)
enum class EBoneDensityTier : uint8
{
	Loose UMETA(DisplayName = "疏松"),
	Medium UMETA(DisplayName = "中等密度"),
	Dense UMETA(DisplayName = "致密")
};

UENUM(BlueprintType)
enum class EBoneEnd : uint8 { Start, End };

/** The designer's three traits. The unit and density baseline remain tunable by the physics programmer. */
USTRUCT(BlueprintType)
struct JGD2026_API FNormalBoneParameters
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone")
	EBoneLengthTier Length = EBoneLengthTier::Long;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone")
	EBoneThicknessTier Thickness = EBoneThicknessTier::Medium;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone")
	EBoneDensityTier Density = EBoneDensityTier::Medium;
	// One design unit in Unreal units. This is independent of image resolution and grid cell size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone", meta = (ClampMin = "0.01"))
	float UnitLength = 64.0f;
	// kg per design unit squared. 2D mass uses area, not the body's Y extrusion thickness.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone", meta = (ClampMin = "0.0001"))
	float StandardDensityKgPerSquareUnit = 1.0f;
};

USTRUCT(BlueprintType)
struct JGD2026_API FNormalBonePhysicalStats
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Bone")
	bool bValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "Bone")
	float Length = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Bone")
	float Thickness = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Bone")
	float DensityMultiplier = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Bone")
	float AreaInSquareUnits = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Bone")
	float MassKg = 0.0f;
};

namespace NormalBoneMetrics
{
	JGD2026_API bool Evaluate(const FNormalBoneParameters& Parameters, FNormalBonePhysicalStats& OutStats, FText& OutReason);
	JGD2026_API FText Describe(const FNormalBoneParameters& Parameters);
	JGD2026_API FIntPoint RequiredGridSize(const FNormalBonePhysicalStats& Stats, float CellSize);
}
