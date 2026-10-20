#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "JGDLevelProgressSubsystem.generated.h"

USTRUCT(BlueprintType)
struct JGD2026_API FJGDLevelRatingResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Rating") int32 EarnedStars = 0;
	UPROPERTY(BlueprintReadOnly, Category="Rating") int32 BestStars = 0;
	UPROPERTY(BlueprintReadOnly, Category="Rating") bool bNewRecord = false;
};

UCLASS()
class JGD2026_API UJGDLevelProgressSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame) TMap<FName, int32> BestRatings;
	int32 GetBest(FName Level) const;
	bool Improve(FName Level, int32 Stars);
	static FName LevelKey(FName Level);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJGDLevelRecordChanged, FName, Level, int32, Stars);

/** Only completed attempts are submitted here. Scores persist across maps and game launches. */
UCLASS()
class JGD2026_API UJGDLevelProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UFUNCTION(BlueprintPure, Category="JGD|Rating") int32 GetBestStars(FName Level) const;
	UFUNCTION(BlueprintCallable, Category="JGD|Rating") FJGDLevelRatingResult RecordCompletion(FName Level, int32 CollectedStars);
	UPROPERTY(BlueprintAssignable, Category="JGD|Rating") FJGDLevelRecordChanged OnRecordChanged;
private:
	UPROPERTY(Transient) TObjectPtr<UJGDLevelProgressSave> Records;
	FString SaveSlot;
};
