#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Styling/SlateBrush.h"
#include "JGDLoadingScreenSubsystem.generated.h"

class SWidget;
class UTexture2D;
class UGameViewportClient;
struct FWorldContext;

/** Persistent map-transition presentation; independent of the Blueprint GameInstance. */
UCLASS()
class JGD2026_API UJGDLoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintPure, Category="JGD|Loading")
	bool IsLoadingScreenVisible() const { return bLoading; }
	UFUNCTION(BlueprintPure, Category="JGD|Loading")
	int32 GetCompletedTransitionCount() const { return CompletedTransitions; }

private:
	UPROPERTY() TSoftObjectPtr<UTexture2D> PosterAsset = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/UI/Loading/T_LoadingPoster.T_LoadingPoster")));
	UPROPERTY(Transient) TObjectPtr<UTexture2D> Poster;
	TWeakObjectPtr<UGameViewportClient> PreviewViewport;
	TSharedPtr<SWidget> Preview;
	FSlateBrush PosterBrush;
	FDelegateHandle PreLoadHandle, PostLoadHandle, MovieFinishedHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	double StartedAt = 0;
	bool bLoading = false;
	bool bMapReady = false;
	bool bUsingMovie = false;
	int32 CompletedTransitions = 0;
	void BeginMapLoad(const FWorldContext& Context, const FString& MapName);
	void EndMapLoad(UWorld* World);
	void MovieFinished();
	void ShowPreview();
	void RemovePreview();
	bool TickLoading(float DeltaTime);
};
