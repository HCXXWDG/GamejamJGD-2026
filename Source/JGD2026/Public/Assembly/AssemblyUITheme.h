#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Styling/SlateTypes.h"
#include "AssemblyUITheme.generated.h"

class UPaperSprite;
class UFont;

/** Replaceable presentation only. No assembly rules or input ownership live here. */
UCLASS(BlueprintType)
class JGD2026_API UAssemblyUITheme : public UDataAsset
{

	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Panels")
	TObjectPtr<UPaperSprite> PanelSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Panels")
	TObjectPtr<UPaperSprite> StatusSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Buttons")
	TObjectPtr<UPaperSprite> ButtonNormalSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Buttons")
	TObjectPtr<UPaperSprite> ButtonHoveredSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Buttons")
	TObjectPtr<UPaperSprite> PrimaryButtonSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Buttons")
	TObjectPtr<UPaperSprite> DestructiveButtonSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Cards")
	TObjectPtr<UPaperSprite> ComponentFrameSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Icons")
	TObjectPtr<UPaperSprite> RemoveIconSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Icons")
	TObjectPtr<UPaperSprite> RestartIconSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Icons")
	TObjectPtr<UPaperSprite> RotateIconSprite;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Text")
	TObjectPtr<UFont> TextFont;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Text")
	FLinearColor TextColor = FLinearColor(0.19f, 0.18f, 0.11f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Text")
	FLinearColor MutedTextColor = FLinearColor(0.36f, 0.34f, 0.25f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Selection")
	FLinearColor SelectedTint = FLinearColor(0.76f, 0.90f, 0.40f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assembly|UI|Text", meta = (ClampMin = "10", ClampMax = "32"))
	int32 FontSize = 14;

	static UAssemblyUITheme* LoadDefault();
	FSlateFontInfo ApplyFont(FSlateFontInfo FontInfo) const;
	FSlateBrush MakeBrush(UPaperSprite* Sprite, const FVector2D& Size, FMargin Margin = FMargin(0.06f)) const;
	FButtonStyle MakeButtonStyle(UPaperSprite* NormalOverride = nullptr) const;
};
