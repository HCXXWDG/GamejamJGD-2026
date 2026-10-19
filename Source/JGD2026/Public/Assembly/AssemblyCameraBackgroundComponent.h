#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AssemblyCameraBackgroundComponent.generated.h"

class UMaterialParameterCollection;
class UStaticMeshComponent;

/** Keeps the player's existing parallax planes behind the actual gameplay camera. */
UCLASS(ClassGroup=(Assembly), meta=(BlueprintSpawnableComponent))
class JGD2026_API UAssemblyCameraBackgroundComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAssemblyCameraBackgroundComponent();
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	struct FBackgroundLayer
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FTransform OriginalTransform;
		FVector LocalCenter = FVector::ZeroVector;
		FVector WorldSize = FVector::ZeroVector;
		float Depth = 0;
		EComponentMobility::Type OriginalMobility = EComponentMobility::Movable;
		ECollisionEnabled::Type OriginalCollision = ECollisionEnabled::NoCollision;
		bool bOriginalGenerateOverlap = false;
	};
	UPROPERTY() TObjectPtr<UMaterialParameterCollection> ParallaxParameters;
	TArray<FBackgroundLayer> Layers;
	void CacheBackgroundLayers();
};
