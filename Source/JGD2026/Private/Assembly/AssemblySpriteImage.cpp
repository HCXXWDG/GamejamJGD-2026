#include "Assembly/AssemblySpriteImage.h"

#include "AssemblySpriteGeometry.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

class SAssemblySpriteImage : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAssemblySpriteImage) {} SLATE_END_ARGS()
	void Construct(const FArguments&) {}

	void SetPresentation(TSharedPtr<const AssemblySpriteGeometry::FRenderData> InData,
		FVector2D InDesiredSize, FLinearColor InColor)
	{
		Data = MoveTemp(InData);
		DesiredSize = InDesiredSize;
		Color = InColor;
		Brush.SetResourceObject(Data.IsValid() ? Data->Texture.Get() : nullptr);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.ImageType = ESlateBrushImageType::FullColor;
		Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	}

private:
	TSharedPtr<const AssemblySpriteGeometry::FRenderData> Data;
	FSlateBrush Brush;
	FVector2D DesiredSize = FVector2D(64.0, 64.0);
	FLinearColor Color = FLinearColor::White;

	virtual FVector2D ComputeDesiredSize(float) const override { return DesiredSize; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (!Data.IsValid() || !Data->IsRenderable() || !FSlateApplication::IsInitialized()) return LayerId;
		FVector2f Offset;
		float Scale;
		if (!AssemblySpriteGeometry::Fit(Data->Mesh, FVector2f(AllottedGeometry.GetLocalSize()), Offset, Scale)) return LayerId;
		const FSlateResourceHandle Resource = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush);
		if (!Resource.IsValid()) return LayerId;
		const FColor VertexColor = (InWidgetStyle.GetColorAndOpacityTint() * Color).ToFColor(true);
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		Vertices.Reserve(Data->Mesh.Vertices.Num());
		Indices.Reserve(Data->Mesh.Vertices.Num());
		for (int32 Index = 0; Index < Data->Mesh.Vertices.Num(); ++Index)
		{
			const AssemblySpriteGeometry::FVertex& Vertex = Data->Mesh.Vertices[Index];
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(AllottedGeometry.GetAccumulatedRenderTransform(),
				Offset + Vertex.Position * Scale, Vertex.UV, VertexColor));
			Indices.Add(static_cast<SlateIndex>(Index));
		}
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Resource, Vertices, Indices,
			nullptr, 0, 0, ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
		return LayerId;
	}
};

void UAssemblySpriteImage::SetSprite(UPaperSprite* InSprite)
{
	Sprite = InSprite;
	RefreshSpriteData();
}

void UAssemblySpriteImage::SetColorAndOpacity(FLinearColor InColorAndOpacity)
{
	const bool bValid = FMath::IsFinite(InColorAndOpacity.R) && FMath::IsFinite(InColorAndOpacity.G)
		&& FMath::IsFinite(InColorAndOpacity.B) && FMath::IsFinite(InColorAndOpacity.A);
	ColorAndOpacity = bValid ? InColorAndOpacity : FLinearColor::White;
	if (SpriteWidget.IsValid()) SpriteWidget->SetPresentation(RenderData, DesiredSize, ColorAndOpacity);
}

void UAssemblySpriteImage::SetDesiredSize(FVector2D InDesiredSize)
{
	DesiredSize = InDesiredSize.ContainsNaN() || !FMath::IsFinite(InDesiredSize.X) || !FMath::IsFinite(InDesiredSize.Y)
		? FVector2D::ZeroVector : FVector2D(FMath::Max(0.0, InDesiredSize.X), FMath::Max(0.0, InDesiredSize.Y));
	if (SpriteWidget.IsValid()) SpriteWidget->SetPresentation(RenderData, DesiredSize, ColorAndOpacity);
}

bool UAssemblySpriteImage::HasRenderableSprite() const
{
	return RenderData.IsValid() && RenderData->IsRenderable();
}

FString UAssemblySpriteImage::GetSpriteFailureReason() const
{
	return RenderData.IsValid() ? RenderData->LastError : TEXT("Sprite presentation has not been initialized.");
}

void UAssemblySpriteImage::RefreshSpriteData()
{
	RenderData = MakeShared<AssemblySpriteGeometry::FRenderData>();
	RenderData->SetSprite(Sprite);
	if (SpriteWidget.IsValid()) SpriteWidget->SetPresentation(RenderData, DesiredSize, ColorAndOpacity);
}

TSharedRef<SWidget> UAssemblySpriteImage::RebuildWidget()
{
	SpriteWidget = SNew(SAssemblySpriteImage);
	RefreshSpriteData();
	return SpriteWidget.ToSharedRef();
}

void UAssemblySpriteImage::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	SetDesiredSize(DesiredSize);
	SetColorAndOpacity(ColorAndOpacity);
	RefreshSpriteData();
}

void UAssemblySpriteImage::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SpriteWidget.Reset();
	RenderData.Reset();
}

#if WITH_EDITOR
const FText UAssemblySpriteImage::GetPaletteCategory()
{
	return NSLOCTEXT("AssemblySpriteImage", "PaletteCategory", "Assembly");
}
#endif
