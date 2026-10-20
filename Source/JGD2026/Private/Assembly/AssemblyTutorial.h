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
		return INDEX_NONE;
	}

	inline void ConfigureInventory(int32 StageIndex, TArray<FAssemblyBoneDefinition>& Bones,
		TArray<FAssemblyMuscleDefinition>& Muscles)
	{
		if (StageIndex == INDEX_NONE) return;
		bool bGaveBones = false;
		for (FAssemblyBoneDefinition& Bone : Bones)
		{
			const bool bGive = StageIndex == 1 && !bGaveBones && Bone.PartKind == EAssemblyRigidPartKind::Bone;
			Bone.InitialQuantity = bGive ? 2 : 0;
			bGaveBones |= bGive;
		}
		for (int32 Index = 0; Index < Muscles.Num(); ++Index)
			Muscles[Index].InitialQuantity = StageIndex == 1 && Index == 0 ? 1 : 0;
	}
}
