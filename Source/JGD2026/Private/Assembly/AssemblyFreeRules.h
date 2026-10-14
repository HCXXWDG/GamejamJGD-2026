#pragma once

#include "Assembly/AssemblyTypes.h"

namespace AssemblyFreeRules
{
	bool ValidateConfiguration(const FAssemblyFreeSettings& Settings, const TArray<FAssemblyBoneDefinition>& Bones,
		const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason);
	bool ValidateJointLimits(float Min, float Max);
	bool IsJointKindValid(EAssemblyJointKind Kind);
	bool IsJointKindAllowed(const FAssemblyBoneDefinition* PartA, const FAssemblyBoneDefinition* PartB, EAssemblyJointKind Kind);
	FAssemblyPlacementResult MakePlacement(FVector2D Position, float Angle);
	FAssemblyPlacementResult ResolveSurfacePlacement(const FAssemblyFreeSettings& Settings,
		const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyBoneInstance>& Bones,
		FName TypeId, FVector2D RawPosition, float Angle, FGuid IgnoreId, const FAssemblySurfaceSnapState& PreviousSnap);
	bool IsPointOnBone(const FNormalBoneParameters& Parameters, FVector2D LocalPoint);
	bool IsPointOnRigidPart(const FAssemblyBoneDefinition& Definition, FVector2D LocalPoint);
	bool ValidateGeometry(const FAssemblyFreeSettings& Settings, const TArray<FAssemblyBoneDefinition>& Definitions,
		const TArray<FAssemblyBoneInstance>& Bones, FName TypeId, const FAssemblyPlacementResult& Placement,
		FGuid IgnoreId, FText& OutReason);
	TArray<FAssemblyJointInstance> BuildJoints(const FAssemblyFreeSettings& Settings,
		const TArray<FAssemblyBoneDefinition>& Definitions, const TArray<FAssemblyBoneInstance>& Bones,
		const TArray<FAssemblyJointInstance>& Previous);
}
