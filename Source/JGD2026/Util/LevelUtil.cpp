// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelUtil.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/World.h"

namespace
{
	/** Content/Maps/Levels in the editor == /Game/Maps/Levels at runtime. */
	const FName LevelPackagePath(TEXT("/Game/Maps/Levels"));

	/** Levels are expected to be named "Level_<number>". */
	const FString LevelNamePrefix(TEXT("Level_"));

	/**
	 * "Level_12" -> 12.
	 *
	 * Anything that does not match the naming convention returns TNumericLimits<int32>::Max()
	 * so it sorts after the numbered levels instead of breaking the ordering.
	 */
	int32 ParseLevelIndex(const FString& AssetName)
	{
		if (!AssetName.StartsWith(LevelNamePrefix, ESearchCase::CaseSensitive))
		{
			return TNumericLimits<int32>::Max();
		}

		const FString IndexText = AssetName.Mid(LevelNamePrefix.Len());
		if (!IndexText.IsNumeric())
		{
			return TNumericLimits<int32>::Max();
		}

		return FCString::Atoi(*IndexText);
	}

	/**
	 * Collects the level (UWorld) assets sitting directly in the Levels folder.
	 *
	 * Both GetLevelCount and GetLevelList go through this so they can never disagree.
	 */
	void CollectLevelAssets(TArray<FAssetData>& OutAssets)
	{
		OutAssets.Reset();

		IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
		if (AssetRegistry == nullptr)
		{
			// The registry is not available yet during very early startup (and is absent on
			// dedicated servers), so report nothing instead of asserting.
			return;
		}

		TArray<FAssetData> AllAssets;
		AssetRegistry->GetAssetsByPath(
			LevelPackagePath,
			AllAssets,
			/*bRecursive=*/false,
			/*bIncludeOnlyOnDiskAssets=*/true);

		const FTopLevelAssetPath WorldClassPath = UWorld::StaticClass()->GetClassPathName();

		OutAssets.Reserve(AllAssets.Num());
		for (const FAssetData& Asset : AllAssets)
		{
			// Only real level assets; the folder may also pick up other asset types.
			if (Asset.AssetClassPath == WorldClassPath)
			{
				OutAssets.Add(Asset);
			}
		}
	}
}

int32 ULevelUtil::GetLevelCount()
{
	TArray<FAssetData> LevelAssets;
	CollectLevelAssets(LevelAssets);

	return LevelAssets.Num();
}

TArray<TSoftObjectPtr<UWorld>> ULevelUtil::GetLevelList()
{
	TArray<FAssetData> LevelAssets;
	CollectLevelAssets(LevelAssets);

	TArray<TSoftObjectPtr<UWorld>> Levels;
	Levels.Reserve(LevelAssets.Num());

	for (const FAssetData& Asset : LevelAssets)
	{
		Levels.Add(TSoftObjectPtr<UWorld>(Asset.ToSoftObjectPath()));
	}

	Levels.Sort([](const TSoftObjectPtr<UWorld>& A, const TSoftObjectPtr<UWorld>& B)
	{
		const int32 IndexA = ParseLevelIndex(A.GetAssetName());
		const int32 IndexB = ParseLevelIndex(B.GetAssetName());

		// Numeric ordering, so Level_2 comes before Level_10.
		if (IndexA != IndexB)
		{
			return IndexA < IndexB;
		}

		// Stable-enough tiebreak for duplicate numbers and for names that do not follow
		// the convention (both land on Max and would otherwise have no defined order).
		return A.GetAssetName() < B.GetAssetName();
	});

	return Levels;
}
