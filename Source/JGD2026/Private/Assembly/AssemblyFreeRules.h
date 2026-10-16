#pragma once

#include "Assembly/AssemblyTypes.h"

namespace AssemblySurfaceGeometry { struct FContact; }

namespace AssemblyFreeRules
{
	bool ValidateConfiguration(const FAssemblyFreeSettings& Settings, const TArray<FAssemblyBoneDefinition>& Bones,
		const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason);
	bool ValidateJointLimits(float Min, float Max);
	bool IsJointKindValid(EAssemblyJointKind Kind);
	bool IsJointKindAllowed(const FAssemblyBoneDefinition* PartA, const FAssemblyBoneDefinition* PartB, EAssemblyJointKind Kind);
	// Exactly one ordinary bone and one connector (nullptr = core). Points retain parameter order.
	// Circular connectors align radially; noncircular end-port axes oppose the true bone-end axis.
	// Both anchors remain on the actual body boundary.
	bool GetEndpointConnection(const FAssemblyFreeSettings& Settings,
		const FAssemblyBoneDefinition* PartA, const FTransform& PoseA,
		const FAssemblyBoneDefinition* PartB, const FTransform& PoseB,
		AssemblySurfaceGeometry::FContact& OutContact);
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
