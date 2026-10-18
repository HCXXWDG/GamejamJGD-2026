#include "Assembly/AssemblyPlayingToolbarWidget.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyHUDWidget.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyPlayPreferences.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Assembly/AssemblySpriteImage.h"
#include "Assembly/AssemblyUITheme.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BoxComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "PaperSprite.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FText Label(const TCHAR* Value) { return FText::FromString(Value); }
	FButtonStyle TransparentButton()
	{
		FSlateBrush Empty;
		Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
		FButtonStyle Style;
		Style.SetNormal(Empty).SetHovered(Empty).SetPressed(Empty).SetDisabled(Empty);
		Style.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, -1));
		return Style;
	}
	constexpr float RouteWidth = 480.0f;
}

void UAssemblyPlayingToolbarWidget::InitializeToolbar(UAssemblySessionComponent* InSession, UAssemblyUITheme* InTheme)
{
	if (Session.Get() != InSession)
	{
		ResetToolbarState();
		Session = InSession;
	}
	Theme = InTheme;
	if (!DestinationClass)
		DestinationClass = LoadClass<AActor>(nullptr, TEXT("/Game/Module/Grid/Blueprints/Destination/BP_Destination.BP_Destination_C"));
	if (!bBuilt && WidgetTree) BuildUI();
	UAssemblyPlayPreferences::Get()->ApplyAudio(this);
}

UTextBlock* UAssemblyPlayingToolbarWidget::MakeText(const FText& Caption, int32 Size)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(Caption);
	Text->SetColorAndOpacity(FSlateColor(Theme ? Theme->TextColor : FLinearColor(0.19f, 0.18f, 0.11f)));
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	if (Theme) Font = Theme->ApplyFont(Font);
	Text->SetFont(Font);
	Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Text;
}

UButton* UAssemblyPlayingToolbarWidget::MakeArtButton(const FText& Caption, UPaperSprite* Sprite,
	UAssemblySpriteImage** OutImage, UTextBlock** OutCaption)
{
	UButton* Button = WidgetTree->ConstructWidget<UAssemblyPointerButton>();
	Button->SetStyle(TransparentButton());
	Button->SetToolTipText(Caption);
	UVerticalBox* Contents = WidgetTree->ConstructWidget<UVerticalBox>();
	Contents->SetVisibility(ESlateVisibility::HitTestInvisible);
	UAssemblySpriteImage* Icon = WidgetTree->ConstructWidget<UAssemblySpriteImage>();
	Icon->SetSprite(Sprite);
	Icon->SetDesiredSize(FVector2D(64, 64));
	USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>();
	IconSize->SetWidthOverride(64);
	IconSize->SetHeightOverride(64);
	IconSize->SetContent(Icon);
	Contents->AddChildToVerticalBox(IconSize)->SetHorizontalAlignment(HAlign_Center);
	UTextBlock* CaptionText = MakeText(Caption, 14);
	CaptionText->SetJustification(ETextJustify::Center);
	Contents->AddChildToVerticalBox(CaptionText)->SetHorizontalAlignment(HAlign_Center);
	Button->SetContent(Contents);
	if (OutImage) *OutImage = Icon;
	if (OutCaption) *OutCaption = CaptionText;
	return Button;
}

UButton* UAssemblyPlayingToolbarWidget::MakeTextButton(const FText& Caption)
{
	UButton* Button = WidgetTree->ConstructWidget<UAssemblyPointerButton>();
	if (Theme) Button->SetStyle(Theme->MakeButtonStyle());
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(160);
	Size->SetHeightOverride(40);
	UTextBlock* Text = MakeText(Caption, 18);
	Text->SetJustification(ETextJustify::Center);
	Size->SetContent(Text);
	Button->SetContent(Size);
	return Button;
}

void UAssemblyPlayingToolbarWidget::BuildUI()
{
	SetIsFocusable(false);
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;
	ToolbarPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlayingToolButtons"));
	if (Theme && Theme->StatusSprite) ToolbarPanel->SetBrush(Theme->MakeBrush(Theme->StatusSprite, FVector2D(288, 112), FMargin(0.06f)));
	ToolbarPanel->SetBrushColor(Theme && Theme->StatusSprite ? FLinearColor::White : FLinearColor(0.92f, 0.88f, 0.72f));
	ToolbarPanel->SetPadding(FMargin(18, 12));
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* Settings = MakeArtButton(Label(TEXT("设置")), Theme ? Theme->SettingsIconSprite.Get() : nullptr);
	Settings->OnClicked.AddDynamic(this, &ThisClass::HandleSettings);
	UAssemblySpriteImage* PauseIcon = nullptr;
	UTextBlock* PauseLabel = nullptr;
	PauseButton = MakeArtButton(Label(TEXT("暂停")), Theme ? Theme->PauseIconSprite.Get() : nullptr, &PauseIcon, &PauseLabel);
	PauseImage = PauseIcon;
	PauseCaption = PauseLabel;
	PauseButton->OnClicked.AddDynamic(this, &ThisClass::HandlePause);
	UButton* Restart = MakeArtButton(Label(TEXT("重置")), Theme ? Theme->RestartIconSprite.Get() : nullptr);
	Restart->SetToolTipText(Label(TEXT("清空本轮组装，回到出生点重新组装")));
	Restart->OnClicked.AddDynamic(this, &ThisClass::HandleRestart);
	for (UButton* Button : {Settings, PauseButton.Get(), Restart})
	{
		UHorizontalBoxSlot* ButtonLayout = Buttons->AddChildToHorizontalBox(Button);
		ButtonLayout->SetPadding(FMargin(8, 0));
	}
	ToolbarPanel->SetContent(Buttons);
	UOverlaySlot* ButtonsSlot = Root->AddChildToOverlay(ToolbarPanel);
	ButtonsSlot->SetHorizontalAlignment(HAlign_Right);
	ButtonsSlot->SetVerticalAlignment(VAlign_Top);
	ButtonsSlot->SetPadding(FMargin(16));

	UVerticalBox* RouteContents = WidgetTree->ConstructWidget<UVerticalBox>();
	RouteContents->SetVisibility(ESlateVisibility::HitTestInvisible);
	RouteCaption = MakeText(Label(TEXT("距终点 —")), 16);
	RouteCaption->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.93f, 0.76f)));
	RouteCaption->SetJustification(ETextJustify::Center);
	RouteContents->AddChild(RouteCaption);
	USizeBox* RouteSize = WidgetTree->ConstructWidget<USizeBox>();
	RouteSize->SetWidthOverride(512);
	RouteSize->SetHeightOverride(56);
	UCanvasPanel* Track = WidgetTree->ConstructWidget<UCanvasPanel>();
	UImage* Background = WidgetTree->ConstructWidget<UImage>();
	if (Theme) Background->SetBrush(Theme->MakeBrush(Theme->RouteBackgroundSprite, FVector2D(512, 44), FMargin(0.045f, 0.20f)));
	UCanvasPanelSlot* TrackSlot = Track->AddChildToCanvas(Background);
	TrackSlot->SetPosition(FVector2D(0, 6));
	TrackSlot->SetSize(FVector2D(512, 44));
	RouteFillSize = WidgetTree->ConstructWidget<USizeBox>();
	RouteFillSize->SetWidthOverride(0);
	RouteFillSize->SetHeightOverride(24);
	RouteFillSize->SetClipping(EWidgetClipping::ClipToBoundsAlways);
	UHorizontalBox* Fill = WidgetTree->ConstructWidget<UHorizontalBox>();
	UPaperSprite* FillSprites[] = {Theme ? Theme->RouteFillLeftSprite.Get() : nullptr,
		Theme ? Theme->RouteFillMiddleSprite.Get() : nullptr, Theme ? Theme->RouteFillRightSprite.Get() : nullptr};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UImage* Piece = WidgetTree->ConstructWidget<UImage>();
		FSlateBrush Brush = Theme ? Theme->MakeBrush(FillSprites[Index], FVector2D(24, 24), FMargin(0)) : FSlateBrush();
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Piece->SetBrush(Brush);
		USizeBox* PieceSize = WidgetTree->ConstructWidget<USizeBox>();
		PieceSize->SetHeightOverride(24);
		if (Index != 1) PieceSize->SetWidthOverride(24);
		PieceSize->SetContent(Piece);
		UHorizontalBoxSlot* FillLayout = Fill->AddChildToHorizontalBox(PieceSize);
		if (Index == 1) FillLayout->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	RouteFillSize->SetContent(Fill);
	UCanvasPanelSlot* FillSlot = Track->AddChildToCanvas(RouteFillSize);
	FillSlot->SetAutoSize(true);
	FillSlot->SetPosition(FVector2D(16, 16));
	UAssemblySpriteImage* Marker = WidgetTree->ConstructWidget<UAssemblySpriteImage>();
	Marker->SetSprite(Theme ? Theme->RouteMarkerSprite.Get() : nullptr);
	Marker->SetDesiredSize(FVector2D(48, 48));
	RouteMarkerSlot = Track->AddChildToCanvas(Marker);
	RouteMarkerSlot->SetPosition(FVector2D(-8, 4));
	RouteMarkerSlot->SetSize(FVector2D(48, 48));
	RouteSize->SetContent(Track);
	RouteContents->AddChild(RouteSize);
	UOverlaySlot* RouteSlot = Root->AddChildToOverlay(RouteContents);
	RouteSlot->SetHorizontalAlignment(HAlign_Center);
	RouteSlot->SetVerticalAlignment(VAlign_Top);
	RouteSlot->SetPadding(FMargin(16, 16, 320, 16));

	SettingsOverlay = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlayingSettingsOverlay"));
	SettingsOverlay->SetBrushColor(FLinearColor(0, 0, 0, 0.68f));
	SettingsOverlay->SetHorizontalAlignment(HAlign_Center);
	SettingsOverlay->SetVerticalAlignment(VAlign_Center);
	UBorder* Paper = WidgetTree->ConstructWidget<UBorder>();
	if (Theme && Theme->SettingsPanelSprite) Paper->SetBrush(Theme->MakeBrush(Theme->SettingsPanelSprite, FVector2D(688, 440), FMargin(0.035f, 0.08f)));
	Paper->SetBrushColor(Theme && Theme->SettingsPanelSprite ? FLinearColor::White : FLinearColor(0.92f, 0.88f, 0.72f));
	Paper->SetPadding(FMargin(40));
	USizeBox* SettingsSize = WidgetTree->ConstructWidget<USizeBox>();
	SettingsSize->SetWidthOverride(560);
	UVerticalBox* SettingsContents = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* Title = MakeText(Label(TEXT("设置")), 28);
	Title->SetJustification(ETextJustify::Center);
	SettingsContents->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 16));
	VolumeCaption = MakeText(Label(TEXT("主音量 100%")), 18);
	SettingsContents->AddChild(VolumeCaption);
	VolumeSlider = WidgetTree->ConstructWidget<USlider>();
	VolumeSlider->SetMinValue(0);
	VolumeSlider->SetMaxValue(1);
	VolumeSlider->SetStepSize(0.01f);
	VolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleVolumeChanged);
	SettingsContents->AddChildToVerticalBox(VolumeSlider)->SetPadding(FMargin(0, 10, 0, 20));
	SettingsContents->AddChild(MakeText(Label(TEXT("画面质量")), 18));
	QualityChoice = WidgetTree->ConstructWidget<UComboBoxString>();
	QualityChoice->OnGenerateWidgetEvent.BindDynamic(this, &ThisClass::GenerateQualityOption);
	for (const TCHAR* Quality : {TEXT("低"), TEXT("中"), TEXT("高"), TEXT("极高")}) QualityChoice->AddOption(Quality);
	SettingsContents->AddChildToVerticalBox(QualityChoice)->SetPadding(FMargin(0, 8, 0, 18));
	UHorizontalBox* FullscreenRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	FullscreenChoice = WidgetTree->ConstructWidget<UCheckBox>();
	FullscreenRow->AddChild(FullscreenChoice);
	FullscreenRow->AddChildToHorizontalBox(MakeText(Label(TEXT("全屏显示")), 18))->SetPadding(FMargin(10, 0));
	SettingsContents->AddChild(FullscreenRow);
	SettingsMessage = MakeText(Label(TEXT("设置期间游戏暂停。关闭后恢复打开前的状态。")), 14);
	SettingsMessage->SetAutoWrapText(true);
	SettingsContents->AddChildToVerticalBox(SettingsMessage)->SetPadding(FMargin(0, 16));
	UHorizontalBox* SettingActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* Apply = MakeTextButton(Label(TEXT("应用并保存")));
	Apply->OnClicked.AddDynamic(this, &ThisClass::HandleApplySettings);
	UButton* Close = MakeTextButton(Label(TEXT("关闭")));
	Close->OnClicked.AddDynamic(this, &ThisClass::HandleCloseSettings);
	for (UButton* Button : {Apply, Close})
		SettingActions->AddChildToHorizontalBox(Button)->SetPadding(FMargin(8, 0));
	SettingsContents->AddChildToVerticalBox(SettingActions)->SetHorizontalAlignment(HAlign_Center);
	SettingsSize->SetContent(SettingsContents);
	Paper->SetContent(SettingsSize);
	SettingsOverlay->SetContent(Paper);
	SettingsOverlay->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* SettingsSlot = Root->AddChildToOverlay(SettingsOverlay);
	SettingsSlot->SetHorizontalAlignment(HAlign_Fill);
	SettingsSlot->SetVerticalAlignment(VAlign_Fill);
	bBuilt = true;
	SetVisibility(bPlaying ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UAssemblyPlayingToolbarWidget::SetPlaying(bool bInPlaying)
{
	if (bPlaying != bInPlaying)
	{
		ResetToolbarState();
		bPlaying = bInPlaying;
		if (bPlaying) CaptureRoute();
	}
	SetVisibility(bPlaying ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	RefreshPauseButton();
}

void UAssemblyPlayingToolbarWidget::ResetToolbarState()
{
	if (bSettingsOpen) CloseSettings(false);
	bSettingsOpen = false;
	bManualPaused = false;
	ReleaseOwnedPause();
	RouteCore.Reset();
	RouteGoal.Reset();
	bRouteAvailable = false;
	RouteProgress = 0;
	bPlaying = false;
	SetVisibility(ESlateVisibility::Collapsed);
	if (SettingsOverlay) SettingsOverlay->SetVisibility(ESlateVisibility::Collapsed);
}

bool UAssemblyPlayingToolbarWidget::CanReleasePause() { return !bManualPaused && !bSettingsOpen; }

bool UAssemblyPlayingToolbarWidget::UpdatePauseState()
{
	const bool bWantPause = bManualPaused || bSettingsOpen;
	UWorld* World = GetWorld();
	if (!World) return false;
	if (!bWantPause)
	{
		ReleaseOwnedPause();
		return true;
	}
	UAssemblySessionComponent* Active = Session.Get();
	UAssemblyComponent* Model = Active ? Active->GetAssemblyComponent() : nullptr;
	if (!bPlaying || !Active || !Model || Model->GetPhase() != EAssemblyPhase::Playing) return false;
	Active->ClearHeldInput();
	// Input-release listeners may synchronously shut down or replace the Session.
	// Never acquire an invisible pause lease after that callback detached this HUD.
	if (!bPlaying || Session.Get() != Active || Active->GetAssemblyComponent() != Model
		|| Model->GetPhase() != EAssemblyPhase::Playing || (!bManualPaused && !bSettingsOpen)) return false;
	// A pre-existing external pause is left untouched. Tick can acquire our own lease
	// later if that owner releases its pause while this modal is still open.
	if (bOwnPauseLease || World->IsPaused()) return true;
	APlayerController* PC = GetOwningPlayer();
	AGameModeBase* GameMode = World->GetAuthGameMode();
	if (!PC || !GameMode || !GameMode->SetPause(PC, FCanUnpause::CreateUObject(this, &ThisClass::CanReleasePause)))
		return false;
	PauseGameMode = GameMode;
	bOwnPauseLease = true;
	return true;
}

void UAssemblyPlayingToolbarWidget::ReleaseOwnedPause()
{
	if (!bOwnPauseLease) return;
	bOwnPauseLease = false;
	if (AGameModeBase* GameMode = PauseGameMode.Get()) GameMode->ClearPause();
	PauseGameMode.Reset();
}

void UAssemblyPlayingToolbarWidget::RefreshPauseButton()
{
	const bool bPaused = bManualPaused || (GetWorld() && GetWorld()->IsPaused());
	const FText Caption = Label(bPaused ? TEXT("继续") : TEXT("暂停"));
	if (PauseCaption) PauseCaption->SetText(Caption);
	if (PauseButton) PauseButton->SetToolTipText(Caption);
	if (PauseImage && Theme) PauseImage->SetSprite(bPaused ? Theme->ResumeIconSprite.Get() : Theme->PauseIconSprite.Get());
}

void UAssemblyPlayingToolbarWidget::HandlePause()
{
	if (!bPlaying || bSettingsOpen) return;
	bManualPaused = !bManualPaused;
	if (!UpdatePauseState())
	{
		bManualPaused = false;
		Report(Label(TEXT("当前关卡无法暂停。")));
	}
	RefreshPauseButton();
}

void UAssemblyPlayingToolbarWidget::HandleSettings()
{
	if (!bPlaying || bSettingsOpen) return;
	OriginalVolume = UAssemblyPlayPreferences::Get()->GetMasterVolume();
	bSettingsApplied = false;
	bPopulatingSettings = true;
	VolumeSlider->SetValue(OriginalVolume);
	HandleVolumeChanged(OriginalVolume);
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		const int32 Quality = Settings->GetOverallScalabilityLevel();
		QualityChoice->SetSelectedIndex(FMath::Clamp(Quality < 0 ? 2 : Quality, 0, 3));
		FullscreenChoice->SetIsChecked(Settings->GetFullscreenMode() != EWindowMode::Windowed);
	}
	bPopulatingSettings = false;
	bSettingsOpen = true;
	if (!UpdatePauseState())
	{
		bSettingsOpen = false;
		Report(Label(TEXT("当前关卡无法暂停，请稍后打开设置。")));
		return;
	}
	SettingsMessage->SetText(Label(TEXT("设置期间游戏暂停；关闭后恢复打开前的状态。未保存的音量预览会撤销。")));
	SettingsOverlay->SetVisibility(ESlateVisibility::Visible);
	RefreshPauseButton();
}

void UAssemblyPlayingToolbarWidget::HandleVolumeChanged(float Value)
{
	if (VolumeCaption) VolumeCaption->SetText(FText::FromString(FString::Printf(TEXT("主音量 %d%%"), FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 100))));
	if (bPopulatingSettings) return;
	UAssemblyPlayPreferences::Get()->SetMasterVolume(Value);
	UAssemblyPlayPreferences::Get()->ApplyAudio(this);
	bSettingsApplied = false;
}

UWidget* UAssemblyPlayingToolbarWidget::GenerateQualityOption(FString Option)
{
	return MakeText(FText::FromString(Option), 16);
}

void UAssemblyPlayingToolbarWidget::HandleApplySettings()
{
	if (!bSettingsOpen) return;
	UAssemblyPlayPreferences* Preferences = UAssemblyPlayPreferences::Get();
	Preferences->SetMasterVolume(VolumeSlider->GetValue());
	Preferences->ApplyAudio(this);
	Preferences->SavePreferences();
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetOverallScalabilityLevel(FMath::Clamp(QualityChoice->GetSelectedIndex(), 0, 3));
		Settings->SetFullscreenMode(FullscreenChoice->IsChecked() ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		// PIE shares the editor's host window. Save the chosen mode without resizing
		// that host; a standalone game applies the actual screen mode immediately.
		if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
			Settings->ApplyNonResolutionSettings();
		else Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
	OriginalVolume = Preferences->GetMasterVolume();
	bSettingsApplied = true;
	SettingsMessage->SetText(Label(GetWorld() && GetWorld()->WorldType == EWorldType::PIE
		? TEXT("已保存。主音量与画质已应用；全屏模式在独立运行时应用。") : TEXT("设置已应用并保存。")));
}

void UAssemblyPlayingToolbarWidget::CloseSettings(bool bKeepChanges)
{
	if (!bSettingsOpen) return;
	if (!bKeepChanges && !bSettingsApplied)
	{
		UAssemblyPlayPreferences::Get()->SetMasterVolume(OriginalVolume);
		UAssemblyPlayPreferences::Get()->ApplyAudio(this);
	}
	bSettingsOpen = false;
	if (SettingsOverlay) SettingsOverlay->SetVisibility(ESlateVisibility::Collapsed);
	UpdatePauseState();
	RefreshPauseButton();
}

void UAssemblyPlayingToolbarWidget::HandleCloseSettings() { CloseSettings(false); }

void UAssemblyPlayingToolbarWidget::HandleRestart()
{
	UAssemblySessionComponent* Active = Session.Get();
	if (!bPlaying || !Active) return;
	FText Reason;
	if (Active->RestartSession(Reason))
	{
		ResetToolbarState();
		SetPlaying(false);
	}
	else Report(Reason);
}

void UAssemblyPlayingToolbarWidget::Report(const FText& Message)
{
	if (UAssemblySessionComponent* Active = Session.Get()) Active->ShowMessage(Message);
}

void UAssemblyPlayingToolbarWidget::CaptureRoute()
{
	RouteCore.Reset();
	RouteGoal.Reset();
	bRouteAvailable = false;
	RouteProgress = 0;
	UAssemblySessionComponent* Active = Session.Get();
	UAssemblyWorkbenchComponent* Workbench = Active ? Active->GetWorkbench() : nullptr;
	UAssemblyInteractionComponent* Interaction = Workbench ? Workbench->GetInteractionComponent() : nullptr;
	UPrimitiveComponent* Core = Interaction ? Interaction->GetCoreBody() : nullptr;
	if (Core && DestinationClass && GetWorld())
	{
		RouteCore = Core;
		RouteStart = Core->GetComponentLocation();
		double BestDistance = TNumericLimits<double>::Max();
		for (TActorIterator<AActor> It(GetWorld(), DestinationClass); It; ++It)
		{
			USceneComponent* Goal = It->GetRootComponent();
			for (UBoxComponent* Box : TInlineComponentArray<UBoxComponent*>(*It))
				if (Box->GetFName() == TEXT("Box")) { Goal = Box; break; }
			if (!Goal) continue;
			const FVector Position = Goal->GetComponentLocation();
			const double Distance = FVector2D::Distance(FVector2D(RouteStart.X, RouteStart.Z), FVector2D(Position.X, Position.Z));
			if (FMath::IsFinite(Distance) && Distance > 1 && Distance < BestDistance)
			{
				BestDistance = Distance;
				RouteGoal = Goal;
			}
		}
		bRouteAvailable = RouteGoal.IsValid();
	}
	RefreshRoute();
}

void UAssemblyPlayingToolbarWidget::RefreshRoute()
{
	if (bRouteAvailable && RouteCore.IsValid() && RouteGoal.IsValid())
	{
		const FVector Goal = RouteGoal->GetComponentLocation();
		const FVector Core = RouteCore->GetComponentLocation();
		RouteProgress = AssemblyPlayingHUD::CalculateRouteProgress(RouteStart, Goal, Core);
		const double Remaining = FVector2D::Distance(FVector2D(Goal.X, Goal.Z), FVector2D(Core.X, Core.Z));
		RouteCaption->SetText(FText::FromString(FString::Printf(TEXT("距终点 %.0f · 进度 %.0f%%"), Remaining, RouteProgress * 100)));
	}
	else
	{
		RouteProgress = 0;
		if (RouteCaption) RouteCaption->SetText(Label(TEXT("本关未配置终点")));
	}
	if (RouteFillSize)
	{
		RouteFillSize->SetWidthOverride(RouteWidth * RouteProgress);
		RouteFillSize->SetVisibility(RouteProgress > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (RouteMarkerSlot) RouteMarkerSlot->SetPosition(FVector2D(16 + RouteWidth * RouteProgress - 24, 4));
}

void UAssemblyPlayingToolbarWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!bPlaying) return;
	UAssemblySessionComponent* Active = Session.Get();
	if (!Active || !Active->GetAssemblyComponent() || Active->GetAssemblyComponent()->GetPhase() != EAssemblyPhase::Playing)
	{
		SetPlaying(false);
		return;
	}
	if ((bSettingsOpen || bManualPaused) && GetWorld() && !GetWorld()->IsPaused()) UpdatePauseState();
	RouteRefreshTime += DeltaTime;
	if (RouteRefreshTime >= 0.10f)
	{
		RouteRefreshTime = 0;
		RefreshRoute();
		RefreshPauseButton();
	}
}

bool UAssemblyPlayingToolbarWidget::IsOverControls(FVector2D Position) const
{
	return bPlaying && (bSettingsOpen || (ToolbarPanel && ToolbarPanel->GetCachedGeometry().IsUnderLocation(Position)));
}

FReply UAssemblyPlayingToolbarWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	if (bSettingsOpen) return FReply::Handled();
	return Super::NativeOnMouseButtonDown(Geometry, MouseEvent);
}

FReply UAssemblyPlayingToolbarWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (bSettingsOpen && KeyEvent.GetKey() == EKeys::Escape)
	{
		CloseSettings(false);
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

void UAssemblyPlayingToolbarWidget::NativeDestruct()
{
	ResetToolbarState();
	Super::NativeDestruct();
}
