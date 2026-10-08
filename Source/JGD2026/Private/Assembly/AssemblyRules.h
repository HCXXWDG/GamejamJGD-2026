#pragma once

#include "Assembly/AssemblyTypes.h"

namespace AssemblyRules
{
	int32 NormalizeTurns(int32 QuarterTurns);
	bool IsValidKey(EAssemblyMuscleKey Key, bool bAllowNone = false);
	bool ValidateConfiguration(const FAssemblyGridSettings& Grid,
		const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason);
	FAssemblyPlacementResult MakePlacement(const FAssemblyGridSettings& Grid,
		const TArray<FIntPoint>& Footprint, FIntPoint Anchor, int32 QuarterTurns);
	// The sole topology entry: bounds, overlap and four-neighbor connectivity, including all remaining bones.
	bool ValidateLayout(const FAssemblyGridSettings& Grid, const TArray<FAssemblyBoneInstance>& Bones,
		const TArray<FIntPoint>* Candidate, FGuid IgnoreId, FText& OutReason);
	bool IsPointOnBone(const TArray<FIntPoint>& Footprint, double CellSize, FVector2D BoneLocalPoint);
}
