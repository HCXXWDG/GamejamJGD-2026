#pragma once

#include "CoreMinimal.h"
#include "Assembly/NormalBoneTypes.h"
#include "AssemblyJointPartTypes.generated.h"

UENUM(BlueprintType)
enum class EAssemblyRigidPartKind : uint8 { Bone, JointPart };

UENUM(BlueprintType)
enum class EAssemblyJointPartShape : uint8
{
	Round UMETA(DisplayName = "圆形关节"),
	Bar UMETA(DisplayName = "直线关节"),
	Corner UMETA(DisplayName = "转角关节"),
	Tee UMETA(DisplayName = "T形关节"),
	Cross UMETA(DisplayName = "十字关节"),
	Fork UMETA(DisplayName = "分叉关节"),
	FixedRound UMETA(DisplayName = "固定圆形关节")
};

struct FAssemblyBoneDefinition;

/** Shared body metrics and attachment points. Artwork never scales either the actor or its local anchor frame. */
namespace AssemblyRigidPartMetrics
{
	JGD2026_API bool Evaluate(const FAssemblyBoneDefinition& Definition, FNormalBonePhysicalStats& OutStats, FText& OutReason);
	JGD2026_API TArray<FVector2D> GetConnectionPoints(const FAssemblyBoneDefinition& Definition);
	JGD2026_API FVector2D GetJointArtworkPixelSize(EAssemblyJointPartShape Shape);
	JGD2026_API FAssemblyBoneDefinition MakeJointPartDefinition(EAssemblyJointPartShape Shape, float ArtUnitsPerPixel = 128.0f / 957.0f);
}
