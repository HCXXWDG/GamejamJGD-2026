#include "Progress/JGDStarRatingWidget.h"

#include "Assembly/AssemblySpriteImage.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "PaperSprite.h"
#include "Progress/JGDLevelProgressSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

UJGDStarRatingWidget::UJGDStarRatingWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	static ConstructorHelpers::FObjectFinder<UPaperSprite> Art(TEXT("/Game/Res/Art/Sprites/SP_LevelStar.SP_LevelStar"));
	Sprite = Art.Object;
}

void UJGDStarRatingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	WidgetTree->RootWidget = Row;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UAssemblySpriteImage* Icon = WidgetTree->ConstructWidget<UAssemblySpriteImage>();
		Icon->SetSprite(Sprite);
		Row->AddChildToHorizontalBox(Icon)->SetPadding(FMargin(3, 0));
		Icons.Add(Icon);
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Refresh();
}

void UJGDStarRatingWidget::SetStarCount(int32 Stars) { StarCount = FMath::Clamp(Stars, 0, 3); Refresh(); }
void UJGDStarRatingWidget::SetIconSize(float Size) { IconSize = FMath::Max(8.f, Size); Refresh(); }
void UJGDStarRatingWidget::Refresh()
{
	for (int32 Index = 0; Index < Icons.Num(); ++Index)
	{
		Icons[Index]->SetDesiredSize(FVector2D(IconSize));
		Icons[Index]->SetColorAndOpacity(Index < StarCount ? FLinearColor::White : FLinearColor(.22f, .18f, .14f, .32f));
	}
}

void UJGDLevelSelectionRatings::Initialize(UUserWidget* InSelector) { Selector = InSelector; Refresh(); }
void UJGDLevelSelectionRatings::Refresh()
{
	UUserWidget* Panel = Selector.Get();
	if (!Panel || !Panel->WidgetTree || !Panel->GetGameInstance()) return;
	UJGDLevelProgressSubsystem* Progress = Panel->GetGameInstance()->GetSubsystem<UJGDLevelProgressSubsystem>();
	if (!Progress) return;
	TArray<UWidget*> Widgets;
	Panel->WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Widget : Widgets)
	{
		UUserWidget* Button = Cast<UUserWidget>(Widget);
		if (!Button || Button->GetClass()->GetFName() != TEXT("WBP_LevelButton_C")) continue;
		// This is the same soft world property consumed by the button's OpenNewLevel graph.
		const FSoftObjectProperty* LevelProperty = FindFProperty<FSoftObjectProperty>(Button->GetClass(), TEXT("Level"));
		const FSoftObjectPtr* Level = LevelProperty ? LevelProperty->GetPropertyValuePtr_InContainer(Button) : nullptr;
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(Button->GetRootWidget());
		if (!Level || !Canvas) continue;
		UJGDStarRatingWidget* Row = Rows.FindRef(Button).Get();
		if (!Row)
		{
			Row = CreateWidget<UJGDStarRatingWidget>(Panel->GetOwningPlayer());
			if (!Row) continue;
			Row->SetIconSize(28);
			UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Row);
			Slot->SetPosition(FVector2D(125, 136));
			Slot->SetAlignment(FVector2D(.5f, 0));
			Slot->SetAutoSize(true);
			Slot->SetZOrder(2);
			if (UTextBlock* Number = Cast<UTextBlock>(Button->GetWidgetFromName(TEXT("Text_Main"))))
			{
				FSlateFontInfo Font = Number->GetFont(); Font.Size = 72; Number->SetFont(Font);
			}
			Rows.Add(Button, Row);
		}
		Row->SetStarCount(Progress->GetBestStars(FName(Level->ToSoftObjectPath().GetLongPackageName())));
	}
}
