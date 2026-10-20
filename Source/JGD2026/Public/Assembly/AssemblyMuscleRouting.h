#pragma once

#include "Assembly/AssemblyTypes.h"

/** One planar tendon path shared by presentation and physical spring gradients. */
namespace AssemblyMuscleRouting
{
	struct FGuide
	{
		bool bValid = false;
		FGuid BoneId; // Invalid means the real core body, whose local center is zero.
		FGuid ConnectionA, ConnectionB;
		FVector2D LocalCenter = FVector2D::ZeroVector;
		double Radius = 0;
		double Sign = 1;
		double ReferenceSweep = 0;
	};
	struct FState
	{
		double Sweep = 0;
		bool bInitialized = false;
	};
	struct FPath
	{
		TArray<FVector> Points;
		FVector PullA = FVector::ZeroVector, PullB = FVector::ZeroVector;
		FVector TouchA = FVector::ZeroVector, TouchB = FVector::ZeroVector;
		double Length = 0;
		bool bWrapped = false;
	};

	JGD2026_API bool BuildGuide(const FAssemblyMuscleInstance& Muscle,
		const TArray<FAssemblyBoneInstance>& Bones, const TArray<FAssemblyBoneDefinition>& Definitions,
		const TArray<FAssemblyJointInstance>& Joints, const FAssemblyFreeSettings& Settings, FGuide& Out);
	JGD2026_API bool IsGuideConnected(const FGuide& Guide, const TArray<FAssemblyJointInstance>& Joints);
	JGD2026_API bool Evaluate(const FGuide& Guide, FVector A, FVector B, const FTransform& GuideWorld,
		FState& State, FPath& Out);
	JGD2026_API bool MakeDirect(FVector A, FVector B, FPath& Out);
}
