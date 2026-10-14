#pragma once

#include "CoreMinimal.h"

// Display offsets only. Never apply these to bodies, model poses or spline anchors.
namespace AssemblyRenderLayers
{
	// Fixed spacing avoids shrinking below GPU depth precision as instance counts grow.
	constexpr double DepthStep = 0.02;
	constexpr double MuscleSlotSpan = (64 + 1) * DepthStep;
	constexpr double PreviewOffset = 0.075;

	inline bool GuidBefore(const FGuid& Left, const FGuid& Right)
	{
		if (Left.A != Right.A) return Left.A < Right.A;
		if (Left.B != Right.B) return Left.B < Right.B;
		if (Left.C != Right.C) return Left.C < Right.C;
		return Left.D < Right.D;
	}

	template<typename TInstance>
	int32 GetRank(const TArray<TInstance>& Instances, const FGuid& Id)
	{
		int32 Rank = 0;
		for (const TInstance& Instance : Instances)
		{
			if (GuidBefore(Instance.InstanceId, Id)) ++Rank;
		}
		return Rank;
	}
}
