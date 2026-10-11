#include "Assembly/AssemblyShelfCardWidget.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyDragDropOperation.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/NormalBone.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "InputCoreTypes.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"

void UAssemblyShelfCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
	if (bBuildPlaceholderUI && WidgetTree && !WidgetTree->RootWidget) BuildPlaceholder();
}

void UAssemblyShelfCardWidget::BuildPlaceholder()
{
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSize"));
	Size->SetWidthOverride(256.0f);
	Size->SetHeightOverride(64.0f);
	Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardBackground"));
	Background->SetPadding(FMargin(0.0f));
	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardContent"));
	USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardIconSize"));
	IconSize->SetWidthOverride(64.0f);
	IconSize->SetHeightOverride(64.0f);
	UScaleBox* IconScale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("CardIconScale"));
	IconScale->SetStretch(EStretch::ScaleToFit);
	IconScale->SetStretchDirection(EStretchDirection::Both);
	IconScale->SetVisibility(ESlateVisibility::HitTestInvisible);
	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CardIcon"));
	IconImage->SetVisibility(ESlateVisibility::Hidden);
	UScaleBoxSlot* IconSlot = Cast<UScaleBoxSlot>(IconScale->AddChild(IconImage));
	IconSlot->SetHorizontalAlignment(HAlign_Center);
	IconSlot->SetVerticalAlignment(VAlign_Center);
	IconSize->SetContent(IconScale);
	Content->AddChildToHorizontalBox(IconSize);
	Caption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CardCaption"));
	Caption->SetAutoWrapText(true);
	Caption->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo CaptionFont = Caption->GetFont();
	CaptionFont.Size = 14;
	Caption->SetFont(CaptionFont);
	Caption->SetVisibility(ESlateVisibility::HitTestInvisible);
	UHorizontalBoxSlot* CaptionSlot = Content->AddChildToHorizontalBox(Caption);
	CaptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	CaptionSlot->SetPadding(FMargin(8.0f, 4.0f));
	CaptionSlot->SetVerticalAlignment(VAlign_Center);
	Background->SetContent(Content);
	Size->SetContent(Background);
	WidgetTree->RootWidget = Size;
}

void UAssemblyShelfCardWidget::InitializeCard(UAssemblyWorkbenchComponent* InWorkbench, FName InTypeId, FText InDisplayName, bool bInMuscle)
{
	Workbench = InWorkbench;
	TypeId = InTypeId;
	DisplayName = InDisplayName.IsEmpty() ? FText::FromName(InTypeId) : InDisplayName;
	bMuscle = bInMuscle;
	RefreshQuantity();
}

int32 UAssemblyShelfCardWidget::GetRemainingQuantity() const
{
	const UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get();
	const UAssemblyComponent* Model = ActiveWorkbench ? ActiveWorkbench->GetAssemblyComponent() : nullptr;
	return Model ? Model->GetRemainingQuantity(TypeId) : 0;
}

UPaperSprite* UAssemblyShelfCardWidget::GetPresentationSprite() const
{
	if (IconSprite) return IconSprite.Get();
	const UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get();
	const UAssemblyInteractionComponent* Interaction = ActiveWorkbench ? ActiveWorkbench->GetInteractionComponent() : nullptr;
	if (!Interaction) return nullptr;
	if (bMuscle)
	{
		const AAssemblyMuscle* MuscleDefaults = Interaction->MuscleActorClass.GetDefaultObject();
		return MuscleDefaults ? MuscleDefaults->CurveSprite.Get() : nullptr;
	}
	const ANormalBone* BoneDefaults = Interaction->BoneActorClass.GetDefaultObject();
	UPaperSpriteComponent* SpriteComponent = BoneDefaults ? BoneDefaults->GetBoneSprite() : nullptr;
	return SpriteComponent ? SpriteComponent->GetSprite() : nullptr;
}

void UAssemblyShelfCardWidget::RefreshQuantity()
{
	const int32 Remaining = GetRemainingQuantity();
	UPaperSprite* PresentationSprite = GetPresentationSprite();
	if (IconImage && AppliedIconSprite != PresentationSprite)
	{
		AppliedIconSprite = PresentationSprite;
		IconImage->SetBrushFromAtlasInterface(PresentationSprite, true);
		IconImage->SetVisibility(PresentationSprite ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (Caption)
	{
		Caption->SetText(FText::Format(FText::FromString(TEXT("{0}  × {1}\n{2}")), DisplayName, FText::AsNumber(Remaining),
			FText::FromString(bMuscle ? TEXT("拖到骨头上，点击第二端") : TEXT("拖到场景中摆放"))));
	}
	if (Background)
	{
		Background->SetBrushColor(Remaining > 0 ? (bMuscle ? FLinearColor(0.40f, 0.12f, 0.22f, 0.96f) : FLinearColor(0.15f, 0.24f, 0.34f, 0.96f))
			: FLinearColor(0.12f, 0.12f, 0.12f, 0.96f));
	}
	OnCardPresentationChanged(DisplayName, Remaining, bMuscle);
}

FReply UAssemblyShelfCardWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = Super::NativeOnMouseButtonDown(Geometry, MouseEvent);
	if (Reply.IsEventHandled()) return Reply;
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return UWidgetBlueprintLibrary::DetectDragIfPressed(MouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}
	return Reply;
}

void UAssemblyShelfCardWidget::NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(Geometry, MouseEvent, OutOperation);
	if (OutOperation) return;
	UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get();
	if (!ActiveWorkbench) return;
	FText Reason;
	const bool bStarted = bMuscle ? ActiveWorkbench->BeginMusclePlacement(TypeId, FGuid(), Reason)
		: ActiveWorkbench->BeginShelfBoneDrag(TypeId, Reason);
	if (!bStarted) return;
	UAssemblyDragDropOperation* Operation = NewObject<UAssemblyDragDropOperation>(this);
	Operation->InitializeDrag(ActiveWorkbench, bMuscle);
	Operation->Pivot = EDragPivot::CenterCenter;
	UTextBlock* Visual = NewObject<UTextBlock>(Operation);
	Visual->SetText(DisplayName);
	Visual->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Visual->SetVisibility(ESlateVisibility::HitTestInvisible);
	Operation->DefaultDragVisual = Visual;
	OutOperation = Operation;
}

bool UAssemblyShelfCardWidget::NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, UDragDropOperation* Operation)
{
	if (Super::NativeOnDrop(Geometry, DragDropEvent, Operation)) return true;
	if (UAssemblyDragDropOperation* AssemblyDrag = Cast<UAssemblyDragDropOperation>(Operation))
	{
		AssemblyDrag->CancelAssemblyDrag();
		return true;
	}
	return false;
}
