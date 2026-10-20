#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "JGDStarRatingWidget.generated.h"

class UAssemblySpriteImage;
class UPaperSprite;

/** Three slots reuse the artist's red heart collectible for both results and saved records. */
UCLASS()
class JGD2026_API UJGDStarRatingWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UJGDStarRatingWidget(const FObjectInitializer& Initializer);
	UFUNCTION(BlueprintCallable, Category="JGD|Rating") void SetStarCount(int32 Stars);
	UFUNCTION(BlueprintPure, Category="JGD|Rating") int32 GetStarCount() const { return StarCount; }
	void SetIconSize(float Size);
protected:
	virtual void NativeOnInitialized() override;
private:
	UPROPERTY() TObjectPtr<UPaperSprite> Sprite;
	UPROPERTY(Transient) TArray<TObjectPtr<UAssemblySpriteImage>> Icons;
	int32 StarCount = 0;
	float IconSize = 40;
	void Refresh();
};

/** Decorates the existing Blueprint level buttons while retaining their navigation handlers. */
UCLASS()
class JGD2026_API UJGDLevelSelectionRatings : public UObject
{
	GENERATED_BODY()
public:
	void Initialize(UUserWidget* InSelector);
	void Refresh();
private:
	TWeakObjectPtr<UUserWidget> Selector;
	TMap<TWeakObjectPtr<UUserWidget>, TWeakObjectPtr<UJGDStarRatingWidget>> Rows;
};
