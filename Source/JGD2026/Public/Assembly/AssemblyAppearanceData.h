#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Assembly/AssemblyTypes.h"
#include "AssemblyAppearanceData.generated.h"

class UPaperSprite;

/** Artwork and display sizes only; collision, connection rules and physical movement stay in their own systems. */
UCLASS(BlueprintType)
class JGD2026_API UAssemblyAppearanceData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance")
	TObjectPtr<UPaperSprite> CoreSprite;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance")
	TMap<EAssemblyJointKind, TObjectPtr<UPaperSprite>> JointSprites;
	// One visual pin per actual connection; this does not imply that it is breakable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance")
	TObjectPtr<UPaperSprite> ConnectionPinSprite;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance", meta = (ClampMin = "0.01"))
	float ConnectionPinDiameter = 16.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance")
	TObjectPtr<UPaperSprite> BreakableJointMarker;
	// Longest displayed dimension in Unreal units. This never changes a joint's physical limits or pick radius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance", meta = (ClampMin = "0.01"))
	float JointDiameter = 16.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance", meta = (ClampMin = "0.01"))
	float BreakableMarkerDiameter = 6.0f;
	// Offset along the existing 2D plane's local +Y, toward the side-view camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance")
	float PlaneDepthOffset = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance")
	int32 TranslucencySortPriority = 20;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Appearance")
	FLinearColor Tint = FLinearColor::White;
};
