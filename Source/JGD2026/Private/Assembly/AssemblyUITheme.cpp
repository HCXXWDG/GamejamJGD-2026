#include "Assembly/AssemblyUITheme.h"

#include "Engine/Font.h"
#include "Misc/PackageName.h"
#include "PaperSprite.h"

UAssemblyUITheme* UAssemblyUITheme::LoadDefault()
{
	// The path is a game package, not a machine path. An absent optional theme keeps
	// the functional fallback usable while authoring or creating the first theme.
	static const TCHAR* Package = TEXT("/Game/UI/Assembly/DA_AssemblyUITheme_Default");
	return FPackageName::DoesPackageExist(Package)
		? LoadObject<UAssemblyUITheme>(nullptr, TEXT("/Game/UI/Assembly/DA_AssemblyUITheme_Default.DA_AssemblyUITheme_Default"))
		: nullptr;
}

FSlateFontInfo UAssemblyUITheme::ApplyFont(FSlateFontInfo FontInfo) const
{
	if (TextFont)
	{
		FontInfo.FontObject = TextFont;
		FontInfo.CompositeFont.Reset();
		// Font assets can name their default face differently from the engine font.
		// None selects the first configured face and preserves per-control sizing.
		FontInfo.TypefaceFontName = NAME_None;
	}
	return FontInfo;
}

FSlateBrush UAssemblyUITheme::MakeBrush(UPaperSprite* Sprite, const FVector2D& Size, FMargin Margin) const
{
	FSlateBrush Brush;
	Brush.SetResourceObject(Sprite);
	Brush.ImageSize = Size;
	Brush.DrawAs = Sprite ? ESlateBrushDrawType::Box : ESlateBrushDrawType::NoDrawType;
	Brush.Margin = Margin;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	return Brush;
}

FButtonStyle UAssemblyUITheme::MakeButtonStyle(UPaperSprite* NormalOverride) const
{
	UPaperSprite* NormalSprite = NormalOverride ? NormalOverride : ButtonNormalSprite.Get();
	UPaperSprite* HoverSprite = NormalOverride ? NormalOverride : ButtonHoveredSprite.Get();
	if (!HoverSprite) HoverSprite = NormalSprite;
	FSlateBrush Normal = MakeBrush(NormalSprite, FVector2D(128, 48), FMargin(0.10f, 0.18f));
	FSlateBrush Hovered = MakeBrush(HoverSprite, FVector2D(128, 48), FMargin(0.10f, 0.18f));
	FSlateBrush Pressed = Hovered;
	Pressed.TintColor = FSlateColor(FLinearColor(0.82f, 0.82f, 0.82f, 1.0f));
	FSlateBrush Disabled = Normal;
	Disabled.TintColor = FSlateColor(FLinearColor(0.70f, 0.70f, 0.70f, 0.55f));
	FButtonStyle Style;
	Style.SetNormal(Normal).SetHovered(Hovered).SetPressed(Pressed).SetDisabled(Disabled);
	Style.SetNormalPadding(FMargin(10.0f, 5.0f));
	Style.SetPressedPadding(FMargin(10.0f, 6.0f, 10.0f, 4.0f));
	return Style;
}
