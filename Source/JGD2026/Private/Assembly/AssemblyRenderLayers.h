#pragma once

#include "CoreMinimal.h"

// Display offsets only. Never apply these to bodies, model poses or spline anchors.
namespace AssemblyRenderLayers
{
	// Local +Y faces the side-view camera. Use real depth for Masked sprite materials.
	// Reserve whole category ranges before assigning GUID ranks: large inventories cannot
	// push a joint over a bone or a bone over a muscle. No body or model pose uses this depth.
	constexpr double DepthStep = 0.02;
	constexpr int32 MaxMuscleSlices = 64;
	constexpr int32 MuscleSlotWidth = MaxMuscleSlices + 1;
	constexpr double MaxSlotBias = DepthStep * 0.2;

	struct FCounts
	{
		int32 NumRigid = 0;
		int32 NumMuscles = 0;
		int32 NumJoints = 0;
	};

	inline int64 Count(int32 Value) { return FMath::Max<int64>(0, Value); }
	inline int64 Rank(int32 Value, int32 Num)
	{
		return FMath::Clamp<int64>(Value, 0, FMath::Max<int64>(0, Count(Num) - 1));
	}
	inline double SlotDepth(int64 Slot, double Bias)
	{
		const double SafeBias = FMath::IsFinite(Bias) ? FMath::Clamp(Bias, -MaxSlotBias, MaxSlotBias) : 0.0;
		return static_cast<double>(Slot) * DepthStep + SafeBias;
	}
	inline int64 BoneBase(const FCounts& Counts)
	{
		return 1 + Count(Counts.NumRigid) + Count(Counts.NumJoints);
	}
	inline int64 MuscleBase(const FCounts& Counts)
	{
		return BoneBase(Counts) + Count(Counts.NumRigid);
	}
	inline int64 MarkerBase(const FCounts& Counts)
	{
		return MuscleBase(Counts) + Count(Counts.NumMuscles) * MuscleSlotWidth;
	}
	inline int64 PreviewBase(const FCounts& Counts)
	{
		return MarkerBase(Counts) + Count(Counts.NumJoints) * 2 + 1;
	}
	inline double CoreDepth(const FCounts&, double Bias = 0.0)
	{
		return SlotDepth(0, Bias);
	}
	inline double RigidDepth(const FCounts& Counts, int32 InstanceRank, bool bJoint,
		bool bPreview, double Bias = 0.0)
	{
		if (bPreview) return SlotDepth(PreviewBase(Counts), Bias);
		const int64 Base = bJoint ? 1 : BoneBase(Counts);
		return SlotDepth(Base + Rank(InstanceRank, Counts.NumRigid), Bias);
	}
	inline double MuscleDepth(const FCounts& Counts, int32 InstanceRank, int32 Slice,
		bool bPreview, double Bias = 0.0)
	{
		const int64 SliceRank = FMath::Clamp(Slice, 0, MaxMuscleSlices - 1);
		const int64 Slot = bPreview ? PreviewBase(Counts) + 1 + SliceRank
			: MuscleBase(Counts) + Rank(InstanceRank, Counts.NumMuscles) * MuscleSlotWidth + SliceRank;
		return SlotDepth(Slot, Bias);
	}
	inline double OverlayDepth(const FCounts& Counts, int32 InstanceRank,
		bool bMarker, double Bias = 0.0)
	{
		// Legacy caps belong behind bones; breakable markers must remain readable in front.
		const int64 Instance = Rank(InstanceRank, Counts.NumJoints);
		return SlotDepth(bMarker ? MarkerBase(Counts) + Instance * 2 + 1
			: 1 + Count(Counts.NumRigid) + Instance, Bias);
	}
	inline double ConnectionPinDepth(const FCounts& Counts, int32 InstanceRank, double Bias = 0.0)
	{
		return SlotDepth(MarkerBase(Counts) + Rank(InstanceRank, Counts.NumJoints) * 2, Bias);
	}

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
