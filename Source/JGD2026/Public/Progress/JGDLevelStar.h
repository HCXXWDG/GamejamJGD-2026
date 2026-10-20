#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JGDLevelStar.generated.h"

class UPaperSpriteComponent;

/** Editable collectible art. The run checks the real core; this actor never blocks physical bodies. */
UCLASS(Blueprintable)
class JGD2026_API AJGDLevelStar : public AActor
{
	GENERATED_BODY()
public:
	AJGDLevelStar();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Star", meta=(ClampMin="0",ClampMax="1")) int32 StarIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Star", meta=(ClampMin="1")) float PickupRadius = 24;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Star", meta=(ClampMin="1")) float DisplaySize = 36;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Star") TObjectPtr<UPaperSpriteComponent> Artwork;
	UFUNCTION(BlueprintPure, Category="Star") bool IsCollected() const { return bCollected; }
	void SetCollected(bool bValue);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;
private:
	bool bCollected = false;
	float BobTime = 0;
};
