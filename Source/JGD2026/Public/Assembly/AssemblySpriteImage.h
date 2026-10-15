#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "AssemblySpriteImage.generated.h"

class UPaperSprite;
class SAssemblySpriteImage;
namespace AssemblySpriteGeometry { struct FRenderData; }

/** Displays a PaperSprite's cooked triangles in UMG, preserving custom atlas cutouts. */
UCLASS(BlueprintType, meta = (DisplayName = "Assembly Sprite Image"))
class JGD2026_API UAssemblySpriteImage : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSprite, Category = "Appearance")
	TObjectPtr<UPaperSprite> Sprite;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColorAndOpacity, Category = "Appearance")
	FLinearColor ColorAndOpacity = FLinearColor::White;
	// The mesh is always fitted uniformly into the allotted size, including a SizeBox's override.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDesiredSize, Category = "Appearance", meta = (ClampMin = "0.0"))
	FVector2D DesiredSize = FVector2D(64.0, 64.0);

	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void SetSprite(UPaperSprite* InSprite);
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	UPaperSprite* GetSprite() const { return Sprite.Get(); }
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void SetColorAndOpacity(FLinearColor InColorAndOpacity);
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void SetDesiredSize(FVector2D InDesiredSize);
	// Empty, missing-texture or malformed sprites draw nothing; there is no rectangular fallback.
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	bool HasRenderableSprite() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	FString GetSpriteFailureReason() const;

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<AssemblySpriteGeometry::FRenderData> RenderData;
	TSharedPtr<SAssemblySpriteImage> SpriteWidget;
	void RefreshSpriteData();
};
