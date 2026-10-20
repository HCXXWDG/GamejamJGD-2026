#pragma once

#include "CoreMinimal.h"
#include "Assembly/AssemblyTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

// Formal map progression. Test maps and explicitly configured sandboxes keep their inventory.
namespace AssemblyTutorial
{
	inline int32 Stage(const UObject* Context)
	{
		if (!Context || !Context->GetWorld() || !Context->GetWorld()->IsGameWorld()) return INDEX_NONE;
		const FString Map = UGameplayStatics::GetCurrentLevelName(Context, true);
		if (Map == TEXT("Level_0")) return 0;
		if (Map == TEXT("Level_1")) return 1;
		if (Map == TEXT("Level_2")) return 2;
		return INDEX_NONE;
	}

	inline void ConfigureInventory(int32 StageIndex, TArray<FAssemblyBoneDefinition>& Bones,
		TArray<FAssemblyMuscleDefinition>& Muscles)
	{
		if (StageIndex < 0 || StageIndex > 2) return;
		const bool bGiveParts = StageIndex > 0;
		const int32 BoneAndMuscleQuantity = StageIndex == 2 ? 12 : 8;
		const int32 MovableJointQuantity = StageIndex == 2 ? 6 : 4;
		const int32 FixedJointQuantity = StageIndex == 2 ? 3 : 2;
		bool bGaveBones = false;
		for (FAssemblyBoneDefinition& Bone : Bones)
		{
			const bool bGive = bGiveParts && !bGaveBones && Bone.PartKind == EAssemblyRigidPartKind::Bone;
			Bone.InitialQuantity = bGive ? BoneAndMuscleQuantity : 0;
			if (bGiveParts && Bone.PartKind == EAssemblyRigidPartKind::JointPart)
				Bone.InitialQuantity = Bone.JointShape == EAssemblyJointPartShape::Round
					? MovableJointQuantity : FixedJointQuantity;
			bGaveBones |= bGive;
		}
		for (int32 Index = 0; Index < Muscles.Num(); ++Index)
			Muscles[Index].InitialQuantity = bGiveParts && Index == 0 ? BoneAndMuscleQuantity : 0;
	}
}
