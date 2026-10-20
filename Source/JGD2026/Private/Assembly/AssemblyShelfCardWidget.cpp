#include "Assembly/AssemblyShelfCardWidget.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyDragDropOperation.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Assembly/AssemblySpriteImage.h"
#include "Assembly/AssemblyUITheme.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/NormalBone.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
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
	if (!UITheme) UITheme = UAssemblyUITheme::LoadDefault();
	if (bBuildPlaceholderUI && WidgetTree && !WidgetTree->RootWidget) BuildPlaceholder();
}

void UAssemblyShelfCardWidget::BuildPlaceholder()
{
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSize"));
	Size->SetWidthOverride(256.0f);
	Size->SetHeightOverride(64.0f);
	Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardBackground"));
	Background->SetPadding(FMargin(0.0f));
	if (UITheme && UITheme->ButtonNormalSprite)
	{
		Background->SetBrush(UITheme->MakeBrush(UITheme->ButtonNormalSprite, FVector2D(256.0, 64.0), FMargin(0.10f, 0.18f)));
	}
	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardContent"));
	USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardIconSize"));
	IconSize->SetWidthOverride(64.0f);
	IconSize->SetHeightOverride(64.0f);
	UScaleBox* IconScale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("CardIconScale"));
	IconScale->SetStretch(EStretch::ScaleToFit);
	IconScale->SetStretchDirection(EStretchDirection::Both);
	IconScale->SetVisibility(ESlateVisibility::HitTestInvisible);
	IconImage = WidgetTree->ConstructWidget<UAssemblySpriteImage>(UAssemblySpriteImage::StaticClass(), TEXT("CardIcon"));
	IconImage->SetVisibility(ESlateVisibility::Hidden);
	UScaleBoxSlot* IconSlot = Cast<UScaleBoxSlot>(IconScale->AddChild(IconImage));
	IconSlot->SetHorizontalAlignment(HAlign_Center);
	IconSlot->SetVerticalAlignment(VAlign_Center);
	if (UITheme && UITheme->ComponentFrameSprite)
	{
		UOverlay* IconLayers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CardIconLayers"));
		IconLayers->SetVisibility(ESlateVisibility::HitTestInvisible);
		IconFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardIconFrame"));
		IconFrame->SetPadding(FMargin(0.0f));
		IconFrame->SetBrush(UITheme->MakeBrush(UITheme->ComponentFrameSprite, FVector2D(64.0, 64.0)));
		IconFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* FrameSlot = IconLayers->AddChildToOverlay(IconFrame);
		FrameSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameSlot->SetVerticalAlignment(VAlign_Fill);
		UOverlaySlot* ImageSlot = IconLayers->AddChildToOverlay(IconScale);
		ImageSlot->SetHorizontalAlignment(HAlign_Fill);
		ImageSlot->SetVerticalAlignment(VAlign_Fill);
		IconSize->SetContent(IconLayers);
	}
	else
	{
		IconSize->SetContent(IconScale);
	}
	Content->AddChildToHorizontalBox(IconSize);
	Caption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CardCaption"));
	Caption->SetAutoWrapText(false);
	Caption->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Caption->SetClipping(EWidgetClipping::ClipToBounds);
	Caption->SetColorAndOpacity(FSlateColor(UITheme && UITheme->ButtonNormalSprite ? UITheme->TextColor : FLinearColor::White));
	FSlateFontInfo CaptionFont = Caption->GetFont();
	CaptionFont.Size = UITheme ? FMath::Clamp(UITheme->FontSize - 2, 10, 30) : 14;
	if (UITheme) CaptionFont = UITheme->ApplyFont(CaptionFont);
	Caption->SetFont(CaptionFont);
	if (UITheme && UITheme->ButtonNormalSprite)
	{
		Caption->SetShadowOffset(FVector2D(0.0, 1.0));
		Caption->SetShadowColorAndOpacity(FLinearColor(1.0f, 0.98f, 0.88f, 0.25f));
	}
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
	InitializePartCard(InWorkbench, InTypeId, InDisplayName, bInMuscle ? EAssemblyShelfPartKind::Muscle : EAssemblyShelfPartKind::Bone);
}

void UAssemblyShelfCardWidget::InitializePartCard(UAssemblyWorkbenchComponent* InWorkbench, FName InTypeId,
	FText InDisplayName, EAssemblyShelfPartKind InPartKind)
{
	Workbench = InWorkbench;
	TypeId = InTypeId;
	DisplayName = InDisplayName.IsEmpty() ? FText::FromName(InTypeId) : InDisplayName;
	PartKind = InPartKind;
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
	if (PartKind == EAssemblyShelfPartKind::JointPart)
	{
		return Interaction->GetJointPartArtSprite(TypeId);
	}
	if (PartKind == EAssemblyShelfPartKind::Muscle)
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
	const bool bMuscle = PartKind == EAssemblyShelfPartKind::Muscle;
	const bool bJointPart = PartKind == EAssemblyShelfPartKind::JointPart;
	const int32 Remaining = GetRemainingQuantity();
	UPaperSprite* PresentationSprite = GetPresentationSprite();
	if (IconImage && AppliedIconSprite != PresentationSprite)
	{
		AppliedIconSprite = PresentationSprite;
		IconImage->SetSprite(PresentationSprite);
		IconImage->SetVisibility(IconImage->HasRenderableSprite() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (IconImage)
	{
		IconImage->SetColorAndOpacity(UITheme && Remaining <= 0
			? FLinearColor(0.65f, 0.65f, 0.65f, 0.60f) : FLinearColor::White);
	}
	if (IconFrame)
	{
		IconFrame->SetBrushColor(Remaining > 0 ? FLinearColor::White : FLinearColor(0.75f, 0.75f, 0.75f, 0.70f));
	}
	if (Caption)
	{
		Caption->SetColorAndOpacity(FSlateColor(UITheme && UITheme->ButtonNormalSprite
			? (Remaining > 0 ? UITheme->TextColor : UITheme->MutedTextColor) : FLinearColor::White));
		FString Instruction = bMuscle ? TEXT("先绑定按键，再连接骨头") : TEXT("拖到场景中摆放");
		const UAssemblyWorkbenchComponent* ActiveWorkbench = Workbench.Get();
		const UAssemblyComponent* Model = ActiveWorkbench ? ActiveWorkbench->GetAssemblyComponent() : nullptr;
		FAssemblyBoneDefinition Definition;
		if (bJointPart && Model && Model->GetBoneDefinition(TypeId, Definition))
		{
			Instruction = Definition.JointShape == EAssemblyJointPartShape::Round
				? TEXT("紫色活动 · 自由摆放") : TEXT("黄色固定 · 自由摆放");
		}
		Caption->SetText(FText::Format(FText::FromString(TEXT("{0}  × {1}\n{2}")), DisplayName, FText::AsNumber(Remaining),
			FText::FromString(Instruction)));
	}
	if (Background)
	{
		if (UITheme && UITheme->ButtonNormalSprite)
		{
			Background->SetBrushColor(Remaining > 0 ? FLinearColor::White : FLinearColor(0.75f, 0.75f, 0.75f, 0.75f));
		}
		else
		{
			Background->SetBrushColor(Remaining > 0 ? (bMuscle ? FLinearColor(0.40f, 0.12f, 0.22f, 0.96f)
				: (bJointPart ? FLinearColor(0.29f, 0.23f, 0.40f, 0.96f) : FLinearColor(0.15f, 0.24f, 0.34f, 0.96f)))
				: FLinearColor(0.12f, 0.12f, 0.12f, 0.96f));
		}
	}
	OnCardPresentationChanged(DisplayName, Remaining, bMuscle);
	OnPartCardPresentationChanged(DisplayName, Remaining, PartKind);
}

FReply UAssemblyShelfCardWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	if (PartKind == EAssemblyShelfPartKind::Muscle && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Workbench.IsValid())
	{
		UAssemblyComponent* Model = Workbench->GetAssemblyComponent();
		if (Model && Model->GetRemainingQuantity(TypeId) > 0 && Model->GetSelectedMuscleKey() == EAssemblyMuscleKey::None)
		{
			FText Reason;
			Workbench->BeginMusclePlacement(TypeId, FGuid(), Reason);
			return FReply::Handled();
		}
	}
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
	bool bStarted = false;
	switch (PartKind)
	{
	case EAssemblyShelfPartKind::Muscle: bStarted = ActiveWorkbench->BeginMusclePlacement(TypeId, FGuid(), Reason); break;
	case EAssemblyShelfPartKind::JointPart: bStarted = ActiveWorkbench->BeginShelfJointDrag(TypeId, Reason); break;
	case EAssemblyShelfPartKind::Bone: bStarted = ActiveWorkbench->BeginShelfBoneDrag(TypeId, Reason); break;
	default: break;
	}
	if (!bStarted) return;
	UAssemblyDragDropOperation* Operation = NewObject<UAssemblyDragDropOperation>(this);
	Operation->InitializePartDrag(ActiveWorkbench, PartKind);
	Operation->Pivot = EDragPivot::CenterCenter;
	UTextBlock* Visual = NewObject<UTextBlock>(Operation);
	if (UITheme) Visual->SetFont(UITheme->ApplyFont(Visual->GetFont()));
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
		if (!Workbench.IsValid() || AssemblyDrag->GetWorkbench() != Workbench.Get() || !AssemblyDrag->IsCurrentDrag())
		{
			return false;
		}
		AssemblyDrag->CancelAssemblyDrag();
		return true;
	}
	return false;
}
