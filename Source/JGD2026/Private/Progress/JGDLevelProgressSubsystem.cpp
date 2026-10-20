#include "Progress/JGDLevelProgressSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace { const FString ProgressSlot(TEXT("JGD2026_LevelRatings_v1")); }

FName UJGDLevelProgressSave::LevelKey(FName Level)
{
	FString Name = FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(Level.ToString()));
	if (Name.StartsWith(TEXT("UEDPIE_")))
	{
		int32 End = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
		if (End != INDEX_NONE) Name.RightChopInline(End + 1);
	}
	return FName(Name);
}

int32 UJGDLevelProgressSave::GetBest(FName Level) const
{
	const int32* Stars = BestRatings.Find(LevelKey(Level));
	return Stars ? FMath::Clamp(*Stars, 0, 3) : 0;
}

bool UJGDLevelProgressSave::Improve(FName Level, int32 Stars)
{
	const FName Key = LevelKey(Level);
	const int32 Score = FMath::Clamp(Stars, 0, 3);
	if (Key.IsNone() || Score <= GetBest(Key)) return false;
	BestRatings.Add(Key, Score);
	return true;
}

void UJGDLevelProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SaveSlot = ProgressSlot;
#if WITH_EDITOR
	// PIE checks use a separate profile, so verification never marks the player's levels complete.
	FParse::Value(FCommandLine::Get(), TEXT("JGDLevelRatingTestSlot="), SaveSlot);
#endif
	if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
		Records = Cast<UJGDLevelProgressSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	if (!Records) Records = Cast<UJGDLevelProgressSave>(UGameplayStatics::CreateSaveGameObject(UJGDLevelProgressSave::StaticClass()));
}

int32 UJGDLevelProgressSubsystem::GetBestStars(FName Level) const
{
	return Records ? Records->GetBest(Level) : 0;
}

FJGDLevelRatingResult UJGDLevelProgressSubsystem::RecordCompletion(FName Level, int32 CollectedStars)
{
	FJGDLevelRatingResult Result;
	Result.EarnedStars = 1 + FMath::Clamp(CollectedStars, 0, 2);
	if (!Records) return Result;
	Result.bNewRecord = Records->Improve(Level, Result.EarnedStars);
	Result.BestStars = Records->GetBest(Level);
	if (Result.bNewRecord)
	{
		if (!UGameplayStatics::SaveGameToSlot(Records, SaveSlot, 0))
			UE_LOG(LogTemp, Warning, TEXT("Could not save level star record for %s."), *Level.ToString());
		OnRecordChanged.Broadcast(UJGDLevelProgressSave::LevelKey(Level), Result.BestStars);
	}
	return Result;
}
