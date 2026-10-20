#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Assembly/AssemblyTypes.h"
#include "Progress/JGDLevelProgressSubsystem.h"
#include "JGDLevelRatingComponent.generated.h"

class AJGDLevelStar;
class UAssemblyComponent;
class UPrimitiveComponent;

UCLASS(ClassGroup=(JGD), meta=(BlueprintSpawnableComponent))
class JGD2026_API UJGDLevelRatingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	void InitializeRun(UAssemblyComponent* InModel);
	void UpdateCollection(UPrimitiveComponent* Core);
	FJGDLevelRatingResult CompleteRun();
	UFUNCTION(BlueprintPure, Category="JGD|Rating") int32 GetCollectedStars() const;
	UFUNCTION(BlueprintPure, Category="JGD|Rating") bool IsRunComplete() const { return bComplete; }
	UFUNCTION(BlueprintCallable, Category="JGD|Rating") void ResetRun();
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Transient) TWeakObjectPtr<UAssemblyComponent> Model;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<AJGDLevelStar>> Stars;
	FVector PreviousCore = FVector::ZeroVector;
	uint8 CollectedMask = 0;
	bool bHavePrevious = false;
	bool bComplete = false;
	FJGDLevelRatingResult Result;
	UFUNCTION() void HandlePhase(EAssemblyPhase Phase);
};
