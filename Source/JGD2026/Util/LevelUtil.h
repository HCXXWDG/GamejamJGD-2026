// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelUtil.generated.h"

class UWorld;

/** Level-related helpers exposed to Blueprint. */
UCLASS()
class JGD2026_API ULevelUtil : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns the number of levels in /Game/Maps/Levels.
	 *
	 * Only UWorld (level) assets are counted, so this always matches GetLevelList().Num();
	 * other asset types dropped into the folder are ignored.
	 *
	 * Reads the asset registry rather than the file system: in a packaged build the content
	 * is cooked into .pak files, so IFileManager would see an empty directory and the count
	 * would silently be 0. The asset registry works the same in the editor and in packaged
	 * builds.
	 *
	 * Not recursive: only assets sitting directly in the folder are counted.
	 */
	UFUNCTION(BlueprintPure, Category = "JGD|Util")
	static int32 GetLevelCount();

	/**
	 * Returns every level in /Game/Maps/Levels as a soft reference, ordered by the numeric
	 * suffix of the asset name: "Level_2" sorts as 2, so Level_2 comes before Level_10.
	 *
	 * Soft references do not load anything up front - pass an element straight to
	 * Open Level by Soft Object Ptr. Only UWorld assets are returned; any other asset type
	 * sitting in the folder is ignored.
	 *
	 * Assets whose name does not follow the "Level_<number>" convention are still returned,
	 * but sorted after the numbered ones (by name).
	 */
	UFUNCTION(BlueprintPure, Category = "JGD|Util")
	static TArray<TSoftObjectPtr<UWorld>> GetLevelList();
};
