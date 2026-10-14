#pragma once

#include "CoreMinimal.h"
#include "Assembly/NormalBone.h"
#include "AssemblyJointPart.generated.h"

class UPaperSprite;

/** Placeable rigid connector. Its instance ID differs from the IDs of its constraint edges. */
UCLASS(BlueprintType, Blueprintable)
class JGD2026_API AAssemblyJointPart : public ANormalBone
{
	GENERATED_BODY()
public:
	AAssemblyJointPart();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual bool ApplyRigidPartDefinition(const FAssemblyBoneDefinition& Definition, FText& OutReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Joint Part|Preview")
	EAssemblyJointPartShape PreviewShape = EAssemblyJointPartShape::Round;
	// Shared pixel-to-world scale, independent of the atlas's 64-pixel crop blocks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Joint Part|Preview", meta = (ClampMin = "0.0001"))
	float ArtUnitsPerPixel = 128.0f / 957.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Joint Part|Artwork")
	TMap<EAssemblyJointPartShape, TObjectPtr<UPaperSprite>> ShapeSprites;

	UFUNCTION(BlueprintPure, Category = "Joint Part|Artwork")
	UPaperSprite* GetSpriteForShape(EAssemblyJointPartShape Shape) const;
	UFUNCTION(BlueprintPure, Category = "Joint Part|Assembly")
	EAssemblyJointPartShape GetJointShape() const;
	UFUNCTION(BlueprintPure, Category = "Joint Part|Assembly")
	TArray<FVector2D> GetConnectionLocalPositions() const;
	UFUNCTION(BlueprintPure, Category = "Joint Part|Assembly")
	TArray<FVector> GetConnectionWorldPositions() const;
	UFUNCTION(BlueprintPure, Category = "Joint Part|Assembly")
	FText GetJointPartDisplayName() const;
	UFUNCTION(BlueprintPure, Category = "Joint Part|Body")
	UBoxComponent* GetJointBody() const { return GetBoneBody(); }
	UFUNCTION(BlueprintPure, Category = "Joint Part|Artwork")
	UPaperSpriteComponent* GetJointSprite() const { return GetBoneSprite(); }
	UFUNCTION(BlueprintPure, Category = "Joint Part|Config")
	static FAssemblyBoneDefinition MakeJointPartDefinition(EAssemblyJointPartShape Shape, float PixelScale = 0.1337513f);
};
