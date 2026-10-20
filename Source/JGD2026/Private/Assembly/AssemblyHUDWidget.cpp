#include "Assembly/AssemblyHUDWidget.h"
#include "Audio/JGDInputFeedbackLibrary.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyDragDropOperation.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Assembly/AssemblyPlayingToolbarWidget.h"
#include "Assembly/AssemblySpriteImage.h"
#include "Assembly/AssemblyShelfCardWidget.h"
#include "Assembly/AssemblyUITheme.h"
#include "AssemblyTutorial.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"

namespace
{
	FText Text(const TCHAR* Value) { return FText::FromString(Value); }
	FString KeyName(EAssemblyMuscleKey Key)
	{
		switch (Key)
		{
		case EAssemblyMuscleKey::W: return TEXT("W");
		case EAssemblyMuscleKey::A: return TEXT("A");
		case EAssemblyMuscleKey::S: return TEXT("S");
		case EAssemblyMuscleKey::D: return TEXT("D");
		default: return TEXT("未选择");
		}
	}
	FString JointName(EAssemblyJointKind Kind)
	{
		switch (Kind)
		{
		case EAssemblyJointKind::Fixed: return TEXT("固定关节");
		case EAssemblyJointKind::BreakableHinge: return TEXT("可断活动关节");
		case EAssemblyJointKind::BreakableFixed: return TEXT("可断固定关节");
		default: return TEXT("活动关节");
		}
	}
}

void UAssemblyHUDActionReceiver::Invoke()
{
	if (UAssemblyHUDWidget* ActiveHUD = HUD.Get()) ActiveHUD->ExecuteAction(Action, Argument, InstanceId);
}

UAssemblyPointerButton::UAssemblyPointerButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitIsFocusable(false);
}

UAssemblyHUDWidget::UAssemblyHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Keep the theme reachable from the native HUD CDO for cooking.
	UITheme = UAssemblyUITheme::LoadDefault();
}

void UAssemblyHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
	if (!UITheme) UITheme = UAssemblyUITheme::LoadDefault();
	if (!ShelfCardClass) ShelfCardClass = UAssemblyShelfCardWidget::StaticClass();
	if (bBuildPlaceholderUI && WidgetTree && !WidgetTree->RootWidget) BuildPlaceholder();
}

void UAssemblyHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindPresentation();
	RefreshPresentation();
}

void UAssemblyHUDWidget::NativeDestruct()
{
	if (PlayingToolbar) PlayingToolbar->ResetToolbarState();
	// Widget removal is presentation-only; the session owns gameplay input cleanup.
	// The cleanup below cancels once, including tools started from the shelf.
	EndWorldPointerCapture(false);
	UnbindPresentation();
	if (UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench())
	{
		ActiveWorkbench->SetExternalPointerTracking(false);
		ActiveWorkbench->CancelTool();
	}
	Super::NativeDestruct();
}

void UAssemblyHUDWidget::InitializeHUD(UAssemblySessionComponent* InSession)
{
	const bool bNewSession = Session.Get() != InSession;
	if (bNewSession)
	{
		EndWorldPointerCapture(true);
		UnbindPresentation();
		Session = InSession;
		ShelfSignature.Reset();
		JointSignature.Reset();
		DisplayedAngleJoint.Invalidate();
		bHaveDisplayedState = false;
		bShowingExplicitMessage = false;
	}
	BindPresentation();
	RefreshPresentation();
	if (bNewSession) OnHUDInitialized(InSession);
}

void UAssemblyHUDWidget::DetachHUD()
{
	if (PlayingToolbar) PlayingToolbar->InitializeToolbar(nullptr, UITheme);
	// Clear capture ownership first; the cleanup below cancels the active tool once.
	EndWorldPointerCapture(false);
	UnbindPresentation();
	if (UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench())
	{
		ActiveWorkbench->SetExternalPointerTracking(false);
		ActiveWorkbench->CancelTool();
	}
	Session.Reset();
}

UAssemblyWorkbenchComponent* UAssemblyHUDWidget::GetWorkbench() const
{
	const UAssemblySessionComponent* ActiveSession = Session.Get();
	return ActiveSession ? ActiveSession->GetWorkbench() : nullptr;
}

void UAssemblyHUDWidget::BindPresentation()
{
	UAssemblySessionComponent* ActiveSession = Session.Get();
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	if (BoundSession.Get() != ActiveSession || BoundWorkbench.Get() != ActiveWorkbench) UnbindPresentation();
	if (ActiveSession && !BoundSession.IsValid())
	{
		ActiveSession->OnSessionChanged.AddUniqueDynamic(this, &UAssemblyHUDWidget::HandleChanged);
		ActiveSession->OnMessage.AddUniqueDynamic(this, &UAssemblyHUDWidget::HandleMessage);
		BoundSession = ActiveSession;
	}
	if (ActiveWorkbench && !BoundWorkbench.IsValid())
	{
		// The session coalesces workbench/model changes after model notifications unwind.
		BoundWorkbench = ActiveWorkbench;
		ActiveWorkbench->OnMuscleBindingRequired.AddUniqueDynamic(this, &ThisClass::HandleBindingRequired);
	}
}

void UAssemblyHUDWidget::UnbindPresentation()
{
	if (BoundWorkbench.IsValid()) BoundWorkbench->OnMuscleBindingRequired.RemoveDynamic(this, &ThisClass::HandleBindingRequired);
	if (UAssemblySessionComponent* ActiveSession = BoundSession.Get())
	{
		ActiveSession->OnSessionChanged.RemoveDynamic(this, &UAssemblyHUDWidget::HandleChanged);
		ActiveSession->OnMessage.RemoveDynamic(this, &UAssemblyHUDWidget::HandleMessage);
	}
	BoundSession.Reset();
	BoundWorkbench.Reset();
}

UTextBlock* UAssemblyHUDWidget::MakeText(const FText& Value)
{
	UTextBlock* Widget = WidgetTree->ConstructWidget<UTextBlock>();
	Widget->SetText(Value);
	Widget->SetAutoWrapText(true);
	Widget->SetColorAndOpacity(FSlateColor(UITheme ? UITheme->TextColor : FLinearColor::White));
	Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
	FSlateFontInfo Font = Widget->GetFont();
	Font.Size = UITheme ? FMath::Clamp(UITheme->FontSize, 10, 32) : 14;
	if (UITheme) Font = UITheme->ApplyFont(Font);
	Widget->SetFont(Font);
	return Widget;
}

UButton* UAssemblyHUDWidget::MakeButton(const FText& Value, EAssemblyHUDAction Action, int32 Argument, FGuid InstanceId, bool bJointAction)
{
	UButton* Button = WidgetTree->ConstructWidget<UAssemblyPointerButton>();
	UPaperSprite* Icon = nullptr;
	if (UITheme)
	{
		UPaperSprite* Background = Action == EAssemblyHUDAction::Start ? UITheme->PrimaryButtonSprite.Get()
			: (Action == EAssemblyHUDAction::Remove || Action == EAssemblyHUDAction::BreakJoint
				? UITheme->DestructiveButtonSprite.Get() : nullptr);
		if (Background || UITheme->ButtonNormalSprite) Button->SetStyle(UITheme->MakeButtonStyle(Background));
		if (Action == EAssemblyHUDAction::Remove) Icon = UITheme->RemoveIconSprite;
		else if (Action == EAssemblyHUDAction::Restart) Icon = UITheme->RestartIconSprite;
		else if (Action == EAssemblyHUDAction::Rotate) Icon = UITheme->RotateIconSprite;
	}
	const bool bArtRestart = Action == EAssemblyHUDAction::Restart && Icon;
	if (bArtRestart)
	{
		FSlateBrush Empty;
		Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
		FButtonStyle Style;
		Style.SetNormal(Empty).SetHovered(Empty).SetPressed(Empty).SetDisabled(Empty);
		Style.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, -1));
		Button->SetStyle(Style);
	}
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetMinDesiredWidth(bJointAction ? 256.0f : 48.0f);
	if (Action == EAssemblyHUDAction::Binding) Size->SetWidthOverride(40.0f);
	Size->SetHeightOverride(44.0f);
	UTextBlock* Caption = MakeText(Value);
	Caption->SetAutoWrapText(false);
	Caption->SetJustification(ETextJustify::Center);
	if (Icon)
	{
		UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
		Content->SetVisibility(ESlateVisibility::HitTestInvisible);
		UImage* Image = WidgetTree->ConstructWidget<UImage>();
		FSlateBrush IconBrush = UITheme->MakeBrush(Icon, FVector2D(20.0f, 20.0f), FMargin(0.0f));
		IconBrush.DrawAs = ESlateBrushDrawType::Image;
		if (bArtRestart) IconBrush.ImageSize = FVector2D(44, 44);
		Image->SetBrush(IconBrush);
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		UHorizontalBoxSlot* IconSlot = Content->AddChildToHorizontalBox(Image);
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
		UHorizontalBoxSlot* CaptionSlot = Content->AddChildToHorizontalBox(Caption);
		CaptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CaptionSlot->SetVerticalAlignment(VAlign_Center);
		Size->SetContent(Content);
	}
	else Size->SetContent(Caption);
	Button->SetContent(Size);
	UAssemblyHUDActionReceiver* Receiver = NewObject<UAssemblyHUDActionReceiver>(this);
	Receiver->HUD = this;
	Receiver->Action = Action;
	Receiver->Argument = Argument;
	Receiver->InstanceId = InstanceId;
	Button->OnClicked.AddDynamic(Receiver, &UAssemblyHUDActionReceiver::Invoke);
	(bJointAction ? JointActions : Actions).Add(Receiver);
	return Button;
}

void UAssemblyHUDWidget::BuildPlaceholder()
{
	// Sprite nine-slice borders use the source pixel size, not Brush.ImageSize.
	// Keep every control inside the yellow paper, including the scroll viewport.
	const bool bPaperPanel = UITheme && UITheme->PanelSprite;
	const FMargin PanelPadding = bPaperPanel ? FMargin(32.0f) : FMargin(12.0f);
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("AssemblyWorldOverlay"));
	WidgetTree->RootWidget = Root;
	UBorder* WorldSurface = WidgetTree->ConstructWidget<UBorder>();
	WorldSurface->SetBrushColor(FLinearColor::Transparent);
	WorldSurface->SetVisibility(ESlateVisibility::Visible);
	UOverlaySlot* SurfaceSlot = Root->AddChildToOverlay(WorldSurface);
	SurfaceSlot->SetHorizontalAlignment(HAlign_Fill);
	SurfaceSlot->SetVerticalAlignment(VAlign_Fill);

	EditPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AssemblyEditPanel"));
	if (UITheme && UITheme->PanelSprite)
		EditPanel->SetBrush(UITheme->MakeBrush(UITheme->PanelSprite, FVector2D(304.0f, 576.0f)));
	EditPanel->SetBrushColor(UITheme ? (UITheme->PanelSprite ? FLinearColor::White : FLinearColor(0.92f, 0.88f, 0.72f))
		: FLinearColor(0.035f, 0.045f, 0.065f, 0.95f));
	EditPanel->SetPadding(PanelPadding);
	USizeBox* EditSize = WidgetTree->ConstructWidget<USizeBox>();
	EditSize->SetWidthOverride(224.0f);
	EditSize->SetMaxDesiredHeight(492.0f);
	UVerticalBox* EditContents = WidgetTree->ConstructWidget<UVerticalBox>();
	EditContents->SetClipping(EWidgetClipping::ClipToBounds);
	UTextBlock* ShelfTitle = MakeText(Text(TEXT("组装工坊")));
	FSlateFontInfo ShelfFont = ShelfTitle->GetFont(); ShelfFont.Size = 22; ShelfTitle->SetFont(ShelfFont);
	EditContents->AddChildToVerticalBox(ShelfTitle)->SetPadding(FMargin(0,0,0,8));
	EditContents->AddChildToVerticalBox(MakeText(Text(TEXT("拖入场景即可组装\nR 旋转 · 滚轮微调 · Esc 取消"))))->SetPadding(FMargin(0,0,0,12));
	AssemblyActionsPanel = WidgetTree->ConstructWidget<UBorder>();
	if (UITheme && UITheme->StatusSprite) AssemblyActionsPanel->SetBrush(UITheme->MakeBrush(UITheme->StatusSprite, FVector2D(560,136), FMargin(.08f,.18f)));
	AssemblyActionsPanel->SetBrushColor(FLinearColor::White);
	AssemblyActionsPanel->SetPadding(FMargin(48,18));
	USizeBox* ActionSize = WidgetTree->ConstructWidget<USizeBox>();
	ActionSize->SetWidthOverride(400);
	UVerticalBox* ActionContents = WidgetTree->ConstructWidget<UVerticalBox>();
	BindingControls = WidgetTree->ConstructWidget<UVerticalBox>();
	UVerticalBox* BindingContents = CastChecked<UVerticalBox>(BindingControls);
	ActionSize->SetContent(ActionContents);
	AssemblyActionsPanel->SetContent(ActionSize);
	UOverlaySlot* ActionsSlot = Root->AddChildToOverlay(AssemblyActionsPanel);
	ActionsSlot->SetHorizontalAlignment(HAlign_Center);
	ActionsSlot->SetVerticalAlignment(VAlign_Bottom);
	ActionsSlot->SetPadding(FMargin(16,16,16,104));
	UHorizontalBox* SessionActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (UButton* Button : { MakeButton(Text(TEXT("开始操控")), EAssemblyHUDAction::Start),
		MakeButton(Text(TEXT("重新开始")), EAssemblyHUDAction::Restart) })
	{
		UHorizontalBoxSlot* ActionSlot = SessionActions->AddChildToHorizontalBox(Button);
		ActionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Scroll->SetClipping(EWidgetClipping::ClipToBounds);
	UVerticalBox* Contents = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Contents);
	EditSize->SetContent(EditContents);
	EditPanel->SetContent(EditSize);
	UOverlaySlot* EditSlot = Root->AddChildToOverlay(EditPanel);
	EditSlot->SetHorizontalAlignment(HAlign_Left);
	EditSlot->SetVerticalAlignment(VAlign_Center);
	EditSlot->SetPadding(FMargin(16.0f));

	UHorizontalBox* Keys = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 Key = static_cast<int32>(EAssemblyMuscleKey::W); Key <= static_cast<int32>(EAssemblyMuscleKey::D); ++Key)
	{
		UButton* Button = MakeButton(FText::FromString(KeyName(static_cast<EAssemblyMuscleKey>(Key))), EAssemblyHUDAction::Binding, Key);
		KeyButtons.Add(Button);
		UHorizontalBoxSlot* KeySlot = Keys->AddChildToHorizontalBox(Button);
		KeySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	BindingContents->AddChildToVerticalBox(MakeText(Text(TEXT("放肌肉前，先选择绑定键"))))->SetHorizontalAlignment(HAlign_Center);
	BindingContents->AddChild(Keys);
	ActionContents->AddChild(BindingControls);
	ActionContents->AddChildToVerticalBox(SessionActions)->SetPadding(FMargin(0,8,0,0));
	UVerticalBoxSlot* ScrollSlot = EditContents->AddChildToVerticalBox(Scroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Shelf = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ComponentShelf"));
	Contents->AddChild(Shelf);
	InspectorPanel = WidgetTree->ConstructWidget<UBorder>();
	if (UITheme && UITheme->PanelSprite) InspectorPanel->SetBrush(UITheme->MakeBrush(UITheme->PanelSprite, FVector2D(304,384)));
	InspectorPanel->SetBrushColor(FLinearColor::White);
	InspectorPanel->SetPadding(PanelPadding);
	USizeBox* InspectorSize = WidgetTree->ConstructWidget<USizeBox>();
	InspectorSize->SetWidthOverride(240);
	InspectorSize->SetMaxDesiredHeight(360);
	UScrollBox* InspectorScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBox* Details = WidgetTree->ConstructWidget<UVerticalBox>();
	InspectorScroll->AddChild(Details);
	InspectorSize->SetContent(InspectorScroll);
	InspectorPanel->SetContent(InspectorSize);
	UOverlaySlot* InspectorSlot = Root->AddChildToOverlay(InspectorPanel);
	InspectorSlot->SetHorizontalAlignment(HAlign_Right);
	InspectorSlot->SetVerticalAlignment(VAlign_Center);
	InspectorSlot->SetPadding(FMargin(16,116,16,90));
	Contents = Details;
	Contents->AddChildToVerticalBox(MakeText(Text(TEXT("部件与连接"))))->SetPadding(FMargin(0,0,0,12));
	SelectionCaption = MakeText(Text(TEXT("点击骨头、肌肉或关节组件；约束连接可在下方列表选择。")));
	Contents->AddChild(SelectionCaption);
	UButton* InstanceButtons[] = {
		MakeButton(Text(TEXT("移动")), EAssemblyHUDAction::Move),
		MakeButton(Text(TEXT("拆下")), EAssemblyHUDAction::Remove),
		MakeButton(Text(TEXT("取消")), EAssemblyHUDAction::Cancel),
		MakeButton(Text(TEXT("旋转")), EAssemblyHUDAction::Rotate, 90)
	};
	for (int32 Row = 0; Row < 2; ++Row)
	{
		UHorizontalBox* InstanceActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 Column = 0; Column < 2; ++Column)
		{
			UHorizontalBoxSlot* ActionSlot = InstanceActions->AddChildToHorizontalBox(InstanceButtons[Row * 2 + Column]);
			ActionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		Contents->AddChild(InstanceActions);
	}
	Contents->AddChild(MakeText(Text(TEXT("关节：选择连接后调整"))));
	JointList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AssemblyJoints"));
	Contents->AddChild(JointList);
	UHorizontalBox* JointKinds = WidgetTree->ConstructWidget<UHorizontalBox>();
	const TCHAR* JointKindLabels[] = { TEXT("活动"), TEXT("固定"), TEXT("可断\n活动"), TEXT("可断\n固定") };
	for (int32 Kind = 0; Kind < UE_ARRAY_COUNT(JointKindLabels); ++Kind)
	{
		UButton* Button = MakeButton(Text(JointKindLabels[Kind]), EAssemblyHUDAction::JointKind, Kind);
		JointKindButtons.Add(Button);
		UHorizontalBoxSlot* KindSlot = JointKinds->AddChildToHorizontalBox(Button);
		KindSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	Contents->AddChild(JointKinds);
	UHorizontalBox* Limits = WidgetTree->ConstructWidget<UHorizontalBox>();
	MinAngle = WidgetTree->ConstructWidget<USpinBox>();
	MaxAngle = WidgetTree->ConstructWidget<USpinBox>();
	for (USpinBox* Spin : { MinAngle.Get(), MaxAngle.Get() })
	{
		if (UITheme) Spin->SetFont(UITheme->ApplyFont(Spin->GetFont()));
		Spin->SetMinValue(-180.0f);
		Spin->SetMaxValue(180.0f);
		Spin->SetMinDesiredWidth(64.0f);
		Spin->SetMaxFractionalDigits(1);
		Limits->AddChild(Spin);
	}
	Limits->AddChild(MakeButton(Text(TEXT("应用\n角度")), EAssemblyHUDAction::ApplyLimits));
	Contents->AddChild(Limits);

	PlayPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AssemblyPlayingPanel"));
	if (UITheme && UITheme->PanelSprite)
		PlayPanel->SetBrush(UITheme->MakeBrush(UITheme->PanelSprite, FVector2D(304.0f, 400.0f)));
	PlayPanel->SetBrushColor(UITheme ? (UITheme->PanelSprite ? FLinearColor::White : FLinearColor(0.92f, 0.88f, 0.72f))
		: FLinearColor(0.035f, 0.045f, 0.065f, 0.90f));
	PlayPanel->SetPadding(PanelPadding);
	USizeBox* PlaySize = WidgetTree->ConstructWidget<USizeBox>();
	PlaySize->SetWidthOverride(288.0f);
	PlaySize->SetMaxDesiredHeight(bPaperPanel ? 328.0f : 384.0f);
	UScrollBox* PlayScroll = WidgetTree->ConstructWidget<UScrollBox>();
	PlayScroll->SetClipping(EWidgetClipping::ClipToBounds);
	UVerticalBox* PlayingControls = WidgetTree->ConstructWidget<UVerticalBox>();
	PlayingControls->AddChild(MakeText(Text(TEXT("可断关节"))));
	PlayJointList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlayingJoints"));
	PlayingControls->AddChild(PlayJointList);
	PlayingControls->AddChild(MakeButton(Text(TEXT("请求断开选中关节")), EAssemblyHUDAction::BreakJoint));
	PlayScroll->AddChild(PlayingControls);
	PlaySize->SetContent(PlayScroll);
	PlayPanel->SetContent(PlaySize);
	UOverlaySlot* PlaySlot = Root->AddChildToOverlay(PlayPanel);
	PlaySlot->SetHorizontalAlignment(HAlign_Right);
	PlaySlot->SetVerticalAlignment(VAlign_Top);
	PlaySlot->SetPadding(FMargin(16.0f, 144.0f, 16.0f, 16.0f));

	UBorder* Status = WidgetTree->ConstructWidget<UBorder>();
	if (UITheme && UITheme->StatusSprite)
		Status->SetBrush(UITheme->MakeBrush(UITheme->StatusSprite, FVector2D(656.0f, 80.0f)));
	Status->SetBrushColor(UITheme ? (UITheme->StatusSprite ? FLinearColor::White : FLinearColor(0.92f, 0.88f, 0.72f))
		: FLinearColor(0.015f, 0.02f, 0.03f, 0.8f));
	const bool bPaperStatus = UITheme && UITheme->StatusSprite;
	Status->SetPadding(bPaperStatus ? FMargin(64.0f, 20.0f) : FMargin(8.0f));
	Status->SetVisibility(ESlateVisibility::HitTestInvisible);
	USizeBox* StatusSize = WidgetTree->ConstructWidget<USizeBox>();
	StatusSize->SetWidthOverride(bPaperStatus ? 528.0f : 640.0f);
	StatusSize->SetMinDesiredHeight(bPaperStatus ? 40.0f : 64.0f);
	StatusSize->SetClipping(EWidgetClipping::ClipToBounds);
	StatusCaption = MakeText(Text(TEXT("骨头可自由摆放。完成连通结构后点击“开始操控”。")));
	StatusSize->SetContent(StatusCaption);
	Status->SetContent(StatusSize);
	UOverlaySlot* StatusSlot = Root->AddChildToOverlay(Status);
	StatusSlot->SetHorizontalAlignment(HAlign_Center);
	StatusSlot->SetVerticalAlignment(VAlign_Bottom);
	StatusSlot->SetPadding(FMargin(16.0f));
	PlayingToolbar = GetOwningPlayer() ? CreateWidget<UAssemblyPlayingToolbarWidget>(GetOwningPlayer())
		: CreateWidget<UAssemblyPlayingToolbarWidget>(this);
	if (PlayingToolbar)
	{
		PlayingToolbar->InitializeToolbar(Session.Get(), UITheme);
		UOverlaySlot* ToolbarSlot = Root->AddChildToOverlay(PlayingToolbar);
		ToolbarSlot->SetHorizontalAlignment(HAlign_Fill);
		ToolbarSlot->SetVerticalAlignment(VAlign_Fill);
	}
	bPlaceholderBuilt = true;
}

void UAssemblyHUDWidget::RefreshShelf()
{
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	UAssemblyComponent* Model = ActiveWorkbench ? ActiveWorkbench->GetAssemblyComponent() : nullptr;
	if (!Shelf || !Model) return;
	FString Signature;
	const TArray<FAssemblyBoneDefinition> Bones = Model->GetBoneDefinitions();
	const TArray<FAssemblyMuscleDefinition> Muscles = Model->GetMuscleDefinitions();
	for (const FAssemblyBoneDefinition& Bone : Bones)
		Signature += (Bone.PartKind == EAssemblyRigidPartKind::JointPart ? TEXT("J:") : TEXT("B:"))
			+ Bone.TypeId.ToString() + TEXT(":") + Bone.DisplayName.ToString() + FString::FromInt(Bone.InitialQuantity) + TEXT(";");
	for (const FAssemblyMuscleDefinition& Muscle : Muscles) Signature += TEXT("M:") + Muscle.TypeId.ToString() + TEXT(":") + Muscle.DisplayName.ToString() + FString::FromInt(Muscle.InitialQuantity) + TEXT(";");
	if (Signature != ShelfSignature)
	{
		ShelfSignature = Signature;
		Shelf->ClearChildren();
		Cards.Reset();
		auto AddCard = [this, ActiveWorkbench](FName TypeId, FText Name, EAssemblyShelfPartKind PartKind)
		{
			UAssemblyShelfCardWidget* Card = GetOwningPlayer() ? CreateWidget<UAssemblyShelfCardWidget>(GetOwningPlayer(), ShelfCardClass)
				: CreateWidget<UAssemblyShelfCardWidget>(this, ShelfCardClass);
			if (!Card) return;
			Card->InitializePartCard(ActiveWorkbench, TypeId, Name, PartKind);
			Shelf->AddChild(Card);
			Cards.Add(Card);
		};
		for (const FAssemblyBoneDefinition& Bone : Bones)
			if (Bone.InitialQuantity > 0 && Bone.PartKind == EAssemblyRigidPartKind::Bone) AddCard(Bone.TypeId, Bone.DisplayName, EAssemblyShelfPartKind::Bone);
		for (const FAssemblyMuscleDefinition& Muscle : Muscles) if (Muscle.InitialQuantity > 0) AddCard(Muscle.TypeId, Muscle.DisplayName, EAssemblyShelfPartKind::Muscle);
		for (const FAssemblyBoneDefinition& Part : Bones)
			if (Part.InitialQuantity > 0 && Part.PartKind == EAssemblyRigidPartKind::JointPart) AddCard(Part.TypeId, Part.DisplayName, EAssemblyShelfPartKind::JointPart);
	}
	for (UAssemblyShelfCardWidget* Card : Cards) if (Card) Card->RefreshQuantity();
}

void UAssemblyHUDWidget::RefreshJoints()
{
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	UAssemblyComponent* Model = ActiveWorkbench ? ActiveWorkbench->GetAssemblyComponent() : nullptr;
	if (!Model || !JointList || !PlayJointList) return;
	const FAssemblySelection Selection = ActiveWorkbench->GetSelection();
	const TArray<FAssemblyJointInstance> Joints = Model->GetJoints();
	FString Signature = Selection.InstanceId.ToString() + FString::FromInt(static_cast<int32>(Selection.Kind));
	for (const FAssemblyJointInstance& Joint : Joints)
		Signature += Joint.InstanceId.ToString() + FString::FromInt(static_cast<int32>(Joint.Kind));
	if (Signature == JointSignature) return;
	JointSignature = Signature;
	JointList->ClearChildren();
	PlayJointList->ClearChildren();
	JointActions.Reset();
	for (const FAssemblyJointInstance& Joint : Joints)
	{
		const bool bSelected = Selection.Kind == EAssemblySelectionKind::Joint && Selection.InstanceId == Joint.InstanceId;
		const FText Label = FText::FromString((bSelected ? TEXT("▶ ") : TEXT("")) + JointName(Joint.Kind) + TEXT(" ") + Joint.InstanceId.ToString().Left(8));
		JointList->AddChild(MakeButton(Label, EAssemblyHUDAction::SelectJoint, 0, Joint.InstanceId, true));
		if (Joint.Kind == EAssemblyJointKind::BreakableHinge || Joint.Kind == EAssemblyJointKind::BreakableFixed)
		{
			PlayJointList->AddChild(MakeButton(Label, EAssemblyHUDAction::SelectJoint, 0, Joint.InstanceId, true));
		}
	}
}

void UAssemblyHUDWidget::RefreshPresentation()
{
	if (bRefreshing) return;
	bRefreshing = true;
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	UAssemblyComponent* Model = ActiveWorkbench ? ActiveWorkbench->GetAssemblyComponent() : nullptr;
	if (Model)
	{
		const bool bAssembly = Model->GetPhase() == EAssemblyPhase::Assembly;
		const bool bCoreOnly = AssemblyTutorial::Stage(this) == 0;
		if (BindingControls) BindingControls->SetVisibility(bCoreOnly ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		if (BindingPrompt && (!bAssembly || Model->GetSelectedMuscleKey() != EAssemblyMuscleKey::None))
			BindingPrompt->SetVisibility(ESlateVisibility::Collapsed);
		if (PlayingToolbar)
		{
			PlayingToolbar->InitializeToolbar(Session.Get(), UITheme);
			PlayingToolbar->SetPlaying(!bAssembly);
		}
		const EAssemblyWorkbenchTool CurrentTool = ActiveWorkbench->GetTool();
		const FGuid CurrentToolId = ActiveWorkbench->GetToolId();
		const bool bToolChanged = !bHaveDisplayedState || DisplayedTool != CurrentTool || DisplayedToolId != CurrentToolId;
		const bool bPhaseChanged = !bHaveDisplayedState || DisplayedPhase != Model->GetPhase();
		DisplayedTool = CurrentTool;
		DisplayedToolId = CurrentToolId;
		DisplayedPhase = Model->GetPhase();
		bHaveDisplayedState = true;
		// Preserve failed-release reasons after a tool ends. A new tool or phase replaces
		// that stale error with its own instructions; pointer-only refreshes do not.
		if (!bShowingExplicitMessage || (bToolChanged && CurrentTool != EAssemblyWorkbenchTool::None) || bPhaseChanged)
			HandleMessage(FText::GetEmpty());
		if (EditPanel) EditPanel->SetVisibility(bAssembly && !bCoreOnly ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (InspectorPanel) InspectorPanel->SetVisibility(bAssembly && !bCoreOnly ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (AssemblyActionsPanel) AssemblyActionsPanel->SetVisibility(bAssembly ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		const bool bHasBreakableJoint = Model->GetJoints().ContainsByPredicate([](const FAssemblyJointInstance& Joint)
		{
			return Joint.Kind == EAssemblyJointKind::BreakableHinge || Joint.Kind == EAssemblyJointKind::BreakableFixed;
		});
		if (PlayPanel) PlayPanel->SetVisibility(!bAssembly && bHasBreakableJoint ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		EAssemblyMuscleKey HighlightKey = Model->GetSelectedMuscleKey();
		const FAssemblySelection Selection = ActiveWorkbench->GetSelection();
		FString Description = TEXT("未选择组件。点击骨头、肌肉或关节组件。");
		if (Selection.Kind == EAssemblySelectionKind::Bone)
		{
			FAssemblyBoneInstance Bone;
			if (Model->GetBone(Selection.InstanceId, Bone))
				Description = FString::Printf(TEXT("骨头 %s\n位置 %.1f, %.1f · 角度 %.1f°"), *Bone.TypeId.ToString(), Bone.LocalPosition.X, Bone.LocalPosition.Y, Bone.RotationDegrees);
		}
		else if (Selection.Kind == EAssemblySelectionKind::JointPart)
		{
			FAssemblyBoneInstance Part;
			FAssemblyBoneDefinition Definition;
			if (Model->GetBone(Selection.InstanceId, Part) && Model->GetBoneDefinition(Part.TypeId, Definition))
			{
				const TCHAR* Nature = Definition.JointShape == EAssemblyJointPartShape::Round
					? TEXT("紫色活动关节：可转动") : TEXT("黄色固定关节：保持固定");
				Description = FString::Printf(TEXT("%s\n位置 %.1f, %.1f · 角度 %.1f°\n骨头端点连接 · %s\n可拖动、旋转和拆下；连接约束在列表中编辑。"),
					*Definition.DisplayName.ToString(), Part.LocalPosition.X, Part.LocalPosition.Y, Part.RotationDegrees,
					Nature);
			}
		}
		else if (Selection.Kind == EAssemblySelectionKind::Muscle)
		{
			FAssemblyMuscleInstance Muscle;
			if (Model->GetMuscle(Selection.InstanceId, Muscle))
			{
				HighlightKey = Muscle.BindingKey;
				Description = FString::Printf(TEXT("肌肉 %s · 绑定 %s\n选择 WASD 可修改该肌肉绑定。"), *Muscle.TypeId.ToString(), *KeyName(Muscle.BindingKey));
			}
		}
		else if (Selection.Kind == EAssemblySelectionKind::Joint)
		{
			for (const FAssemblyJointInstance& Joint : Model->GetJoints())
			{
				if (Joint.InstanceId != Selection.InstanceId) continue;
				Description = FString::Printf(TEXT("%s\n限制 %.1f° 至 %.1f°"), *JointName(Joint.Kind), Joint.MinAngleDegrees, Joint.MaxAngleDegrees);
				if (DisplayedAngleJoint != Joint.InstanceId || DisplayedModelMinAngle != Joint.MinAngleDegrees || DisplayedModelMaxAngle != Joint.MaxAngleDegrees)
				{
					DisplayedAngleJoint = Joint.InstanceId;
					DisplayedModelMinAngle = Joint.MinAngleDegrees;
					DisplayedModelMaxAngle = Joint.MaxAngleDegrees;
					if (MinAngle) MinAngle->SetValue(Joint.MinAngleDegrees);
					if (MaxAngle) MaxAngle->SetValue(Joint.MaxAngleDegrees);
				}
				break;
			}
		}
		if (SelectionCaption) SelectionCaption->SetText(FText::FromString(Description));
		const bool bJointSelected = bAssembly && Selection.Kind == EAssemblySelectionKind::Joint;
		if (Selection.Kind != EAssemblySelectionKind::Joint) DisplayedAngleJoint.Invalidate();
		if (MinAngle) MinAngle->SetIsEnabled(bJointSelected);
		if (MaxAngle) MaxAngle->SetIsEnabled(bJointSelected);
		for (int32 Kind = 0; Kind < JointKindButtons.Num(); ++Kind)
		{
			if (JointKindButtons[Kind])
			{
				JointKindButtons[Kind]->SetIsEnabled(bJointSelected
					&& Model->IsJointKindAllowed(Selection.InstanceId, static_cast<EAssemblyJointKind>(Kind)));
			}
		}
		for (int32 Index = 0; Index < KeyButtons.Num(); ++Index)
		{
			if (!KeyButtons[Index]) continue;
			const bool bSelected = static_cast<int32>(HighlightKey) == Index + 1;
			if (UITheme && UITheme->ButtonNormalSprite)
			{
				KeyButtons[Index]->SetStyle(UITheme->MakeButtonStyle(bSelected ? UITheme->ButtonHoveredSprite.Get() : nullptr));
				KeyButtons[Index]->SetBackgroundColor(bSelected ? UITheme->SelectedTint : FLinearColor::White);
			}
			else KeyButtons[Index]->SetBackgroundColor(bSelected
				? FLinearColor(0.10f, 0.65f, 0.80f) : FLinearColor(0.16f, 0.20f, 0.27f));
		}
		RefreshShelf();
		RefreshJoints();
	}
	bRefreshing = false;
	OnHUDPresentationChanged();
}

void UAssemblyHUDWidget::HandleChanged() { RefreshPresentation(); }

void UAssemblyHUDWidget::CompleteLevel(int32 EarnedStars, int32 BestStars)
{
	if (PlayingToolbar) PlayingToolbar->CompleteLevel(EarnedStars, BestStars);
}

void UAssemblyHUDWidget::HandleBindingRequired()
{
	if (!BindingPrompt)
	{
		UOverlay* Root = Cast<UOverlay>(GetRootWidget());
		if (!Root) return;
		BindingPrompt = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MuscleBindingPrompt"));
		BindingPrompt->SetBrushColor(FLinearColor(0, 0, 0, .45f));
		BindingPrompt->SetHorizontalAlignment(HAlign_Center);
		BindingPrompt->SetVerticalAlignment(VAlign_Center);
		UBorder* Paper = WidgetTree->ConstructWidget<UBorder>();
		if (UITheme) Paper->SetBrush(UITheme->MakeBrush(UITheme->StatusSprite, FVector2D(520, 220)));
		Paper->SetBrushColor(UITheme ? FLinearColor::White : FLinearColor(.9f, .85f, .65f));
		Paper->SetPadding(FMargin(36));
		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		UTextBlock* Title = MakeText(Text(TEXT("先为这块肌肉绑定一个按键")));
		FSlateFontInfo Font = Title->GetFont(); Font.Size = 22; Title->SetFont(Font);
		Content->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 12));
		Content->AddChildToVerticalBox(MakeText(Text(TEXT("选好后，再把肌肉拖到两块骨头之间。"))))->SetPadding(FMargin(0, 0, 0, 12));
		UHorizontalBox* Keys = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 Key = 1; Key <= 4; ++Key)
			Keys->AddChildToHorizontalBox(MakeButton(Text(*KeyName(static_cast<EAssemblyMuscleKey>(Key))), EAssemblyHUDAction::Binding, Key))
				->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Content->AddChild(Keys);
		Content->AddChild(MakeButton(Text(TEXT("取消")), EAssemblyHUDAction::Cancel));
		Paper->SetContent(Content);
		BindingPrompt->SetContent(Paper);
		UOverlaySlot* PromptSlot = Root->AddChildToOverlay(BindingPrompt);
		PromptSlot->SetHorizontalAlignment(HAlign_Fill);
		PromptSlot->SetVerticalAlignment(VAlign_Fill);
	}
	BindingPrompt->SetVisibility(ESlateVisibility::Visible);
}

void UAssemblyHUDWidget::HandleMessage(FText Message)
{
	bShowingExplicitMessage = !Message.IsEmpty();
	if (Message.IsEmpty()) Message = GetDefaultMessage();
	if (DisplayedMessage.EqualTo(Message)) return;
	DisplayedMessage = Message;
	if (StatusCaption) StatusCaption->SetText(Message);
	OnHUDMessage(Message);
}

FText UAssemblyHUDWidget::GetDefaultMessage() const
{
	if (const UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench())
	{
		switch (ActiveWorkbench->GetTool())
		{
		case EAssemblyWorkbenchTool::Bone:
			return Text(TEXT("骨头端点吸附关节或核心，连接件中心对齐骨头延长线，拉远脱离。R 旋转，滚轮微调；松开安装，Esc 或右键取消。"));
		case EAssemblyWorkbenchTool::JointPart:
			return Text(TEXT("关节只吸附骨头两端，中心对齐骨头延长线；不连接其他关节或核心。R 旋转，滚轮微调；松开安装，Esc 或右键取消。"));
		case EAssemblyWorkbenchTool::MuscleFirst:
			return Text(TEXT("在骨头内侧或外侧选择第一连接点；从组件栏拖出时，在骨头上松开。靠近边缘会吸附到骨头轮廓。Esc 或右键取消。"));
		case EAssemblyWorkbenchTool::MuscleSecond:
			return Text(TEXT("点击骨头内侧或外侧的第二连接点。同一对骨头可连接多条肌肉，各自保留连接位置和键位。Esc 或右键取消。"));
		default: break;
		}
		if (const UAssemblyComponent* Model = ActiveWorkbench->GetAssemblyComponent())
		{
			if (Model->GetPhase() == EAssemblyPhase::Playing)
			{
				if (AssemblyTutorial::Stage(this) == 0)
					return Text(TEXT("按住 Space 向前滚动，松开停止施力。滚到终点即可通关！"));
				return Text(TEXT("WASD 按住收缩，松开舒张；Space 旋转核心。右上方可暂停、设置或重置回组装。"));
			}
		}
	}
	if (AssemblyTutorial::Stage(this) == 0)
		return Text(TEXT("第一关：从核心开始。点击“开始操控”，再按住 Space 向前滚动。"));
	if (AssemblyTutorial::Stage(this) == 1)
		return Text(TEXT("第二关：八块骨头、八块肌肉、四个活动关节，各种固定关节各两个。骨头通过核心或关节连接；先选择 W/A/S/D，再拖动肌肉。"));
	return Text(TEXT("将组件从组件栏拖到场景中。仅肌肉需要先选择 WASD；结构连通后点击“开始操控”。"));
}

void UAssemblyHUDWidget::PresentReason(bool bSucceeded, const FText& Reason)
{
	if (!Reason.IsEmpty()) HandleMessage(Reason);
	else if (!bSucceeded) HandleMessage(Text(TEXT("操作未完成，请检查选择和组装状态。")));
	else HandleMessage(FText::GetEmpty());
}

void UAssemblyHUDWidget::ExecuteAction(EAssemblyHUDAction Action, int32 Argument, FGuid InstanceId)
{
	if (BindingPrompt && (Action == EAssemblyHUDAction::Cancel || Action == EAssemblyHUDAction::Binding))
		BindingPrompt->SetVisibility(ESlateVisibility::Collapsed);
	UAssemblySessionComponent* ActiveSession = Session.Get();
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	if (!ActiveSession || !ActiveWorkbench) return;
	UJGDInputFeedbackLibrary::PlayInputPress(this);
	FText Reason;
	bool bSucceeded = false;
	switch (Action)
	{
	case EAssemblyHUDAction::Start: bSucceeded = ActiveSession->StartPlaying(Reason); break;
	case EAssemblyHUDAction::Restart: bSucceeded = ActiveSession->RestartSession(Reason); break;
	case EAssemblyHUDAction::Cancel: ActiveWorkbench->CancelTool(); bSucceeded = true; break;
	case EAssemblyHUDAction::Move: bSucceeded = ActiveWorkbench->BeginMoveSelected(Reason); break;
	case EAssemblyHUDAction::Remove: bSucceeded = ActiveWorkbench->RemoveSelected(Reason); break;
	case EAssemblyHUDAction::Rotate: bSucceeded = ActiveWorkbench->RotateTool(static_cast<float>(Argument), Reason); break;
	case EAssemblyHUDAction::Binding: bSucceeded = ActiveWorkbench->SelectBindingKey(static_cast<EAssemblyMuscleKey>(Argument), Reason); break;
	case EAssemblyHUDAction::SelectJoint: bSucceeded = ActiveWorkbench->SelectInstance(InstanceId, EAssemblySelectionKind::Joint, Reason); break;
	case EAssemblyHUDAction::JointKind: bSucceeded = ActiveWorkbench->SetSelectedJointKind(static_cast<EAssemblyJointKind>(Argument), Reason); break;
	case EAssemblyHUDAction::ApplyLimits:
		bSucceeded = MinAngle && MaxAngle && ActiveWorkbench->SetSelectedJointLimits(MinAngle->GetValue(), MaxAngle->GetValue(), Reason);
		break;
	case EAssemblyHUDAction::BreakJoint: bSucceeded = ActiveWorkbench->RequestSelectedJointBreak(Reason); break;
	}
	PresentReason(bSucceeded, Reason);
	RefreshPresentation();
}

FVector2D UAssemblyHUDWidget::ToViewportPixels(FVector2D AbsolutePosition) const
{
	FVector2D PixelPosition, ViewportPosition;
	USlateBlueprintLibrary::AbsoluteToViewport(this, AbsolutePosition, PixelPosition, ViewportPosition);
	return PixelPosition;
}

bool UAssemblyHUDWidget::IsOverPlaceholderPanel(FVector2D AbsolutePosition) const
{
	if (BindingPrompt && BindingPrompt->GetVisibility() == ESlateVisibility::Visible) return true;
	if (!bPlaceholderBuilt) return false;
	return (PlayingToolbar && PlayingToolbar->IsOverControls(AbsolutePosition))
		|| (EditPanel && EditPanel->GetVisibility() == ESlateVisibility::Visible && EditPanel->GetCachedGeometry().IsUnderLocation(AbsolutePosition))
		|| (InspectorPanel && InspectorPanel->GetVisibility() == ESlateVisibility::Visible && InspectorPanel->GetCachedGeometry().IsUnderLocation(AbsolutePosition))
		|| (AssemblyActionsPanel && AssemblyActionsPanel->GetVisibility() == ESlateVisibility::Visible && AssemblyActionsPanel->GetCachedGeometry().IsUnderLocation(AbsolutePosition))
		|| (PlayPanel && PlayPanel->GetVisibility() == ESlateVisibility::Visible && PlayPanel->GetCachedGeometry().IsUnderLocation(AbsolutePosition));
}

bool UAssemblyHUDWidget::OwnsCapturedTool() const
{
	const UAssemblyWorkbenchComponent* ActiveWorkbench = CapturedWorkbench.Get();
	return bOwnsPointerCapture && CapturedToolId.IsValid() && ActiveWorkbench
		&& ActiveWorkbench->GetToolId() == CapturedToolId && ActiveWorkbench->IsRigidPartTool();
}

void UAssemblyHUDWidget::EndWorldPointerCapture(bool bCancelTool)
{
	const bool bOwnsTool = OwnsCapturedTool();
	UAssemblyWorkbenchComponent* ActiveWorkbench = CapturedWorkbench.Get();
	// Clear ownership before callbacks: a replacement tool must not inherit this capture.
	bOwnsPointerCapture = false;
	CapturedToolId.Invalidate();
	CapturedWorkbench.Reset();
	if (bOwnsTool)
	{
		ActiveWorkbench->SetExternalPointerTracking(false);
		if (bCancelTool) ActiveWorkbench->CancelTool();
	}
}

FReply UAssemblyHUDWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = Super::NativeOnMouseButtonDown(Geometry, MouseEvent);
	if (Reply.IsEventHandled() || !bForwardWorldPointerEvents || IsOverPlaceholderPanel(MouseEvent.GetScreenSpacePosition())) return Reply;
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	if (!ActiveWorkbench) return Reply;
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const bool bHadCapture = bOwnsPointerCapture;
		ActiveWorkbench->CancelTool();
		EndWorldPointerCapture(false);
		HandleMessage(FText::GetEmpty());
		return bHadCapture ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return Reply;
	FText Reason;
	const FVector2D PointerPixels = ToViewportPixels(MouseEvent.GetScreenSpacePosition());
	const bool bSucceeded = ActiveWorkbench->PointerPressed(PointerPixels, Reason);
	const FGuid PressedToolId = ActiveWorkbench->GetToolId();
	PresentReason(bSucceeded, Reason);
	if (ActiveWorkbench == GetWorkbench() && ActiveWorkbench->GetToolId() == PressedToolId
		&& ActiveWorkbench->IsRigidPartTool())
	{
		bOwnsPointerCapture = true;
		CapturedWorkbench = ActiveWorkbench;
		CapturedToolId = PressedToolId;
		// Slate capture stops SViewport's mouse cache updating. Tick must not overwrite
		// these event coordinates with PlayerController::GetMousePosition's old sample.
		ActiveWorkbench->SetExternalPointerTracking(true);
		// Switching sources invalidates the cached preview; seed this press immediately.
		if (OwnsCapturedTool()) ActiveWorkbench->UpdatePointer(PointerPixels, Reason);
		if (OwnsCapturedTool()) return FReply::Handled().CaptureMouse(TakeWidget());
		EndWorldPointerCapture(false);
	}
	return FReply::Handled();
}

FReply UAssemblyHUDWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = Super::NativeOnMouseButtonUp(Geometry, MouseEvent);
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return Reply;
	if (Reply.IsEventHandled() || !bForwardWorldPointerEvents || (bOwnsPointerCapture && !OwnsCapturedTool()))
	{
		if (!bOwnsPointerCapture) return Reply;
		EndWorldPointerCapture(true);
		return FReply::Handled().ReleaseMouseCapture();
	}
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	if (!ActiveWorkbench)
	{
		if (!bOwnsPointerCapture) return Reply;
		EndWorldPointerCapture(true);
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (IsOverPlaceholderPanel(MouseEvent.GetScreenSpacePosition()))
	{
		// Returning an installed rigid part to the shelf removes it only when connectivity allows.
		const FAssemblySelection Selection = ActiveWorkbench->GetSelection();
		const FGuid ReturningToolId = ActiveWorkbench->GetToolId();
		const bool bReturningInstalledBone = ReturningToolId.IsValid() && ActiveWorkbench->IsRigidPartTool()
			&& (Selection.Kind == EAssemblySelectionKind::Bone || Selection.Kind == EAssemblySelectionKind::JointPart)
			&& (!bOwnsPointerCapture || (CapturedWorkbench.Get() == ActiveWorkbench && CapturedToolId == ReturningToolId))
			&& Shelf && Shelf->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition());
		ActiveWorkbench->CancelTool();
		HandleMessage(FText::GetEmpty());
		const FAssemblySelection CurrentSelection = ActiveWorkbench->GetSelection();
		// Cancel/message notifications may select another part or start a replacement drag.
		// The old mouse release may only return the originally selected part from its own tool.
		if (bReturningInstalledBone && ActiveWorkbench == GetWorkbench()
			&& ActiveWorkbench->GetTool() == EAssemblyWorkbenchTool::None && !ActiveWorkbench->GetToolId().IsValid()
			&& CurrentSelection.Kind == Selection.Kind && CurrentSelection.InstanceId == Selection.InstanceId)
		{
			FText Reason;
			PresentReason(ActiveWorkbench->RemoveSelected(Reason), Reason);
		}
	}
	else if (ActiveWorkbench->IsRigidPartTool())
	{
		FText Reason;
		PresentReason(ActiveWorkbench->PointerReleased(ToViewportPixels(MouseEvent.GetScreenSpacePosition()), Reason), Reason);
	}
	EndWorldPointerCapture(false);
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UAssemblyHUDWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = Super::NativeOnMouseMove(Geometry, MouseEvent);
	if (bOwnsPointerCapture && (!OwnsCapturedTool() || !bForwardWorldPointerEvents))
	{
		EndWorldPointerCapture(true);
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (Reply.IsEventHandled() || !bForwardWorldPointerEvents) return Reply;
	if (UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench())
	{
		if (ActiveWorkbench->GetTool() != EAssemblyWorkbenchTool::None && !IsOverPlaceholderPanel(MouseEvent.GetScreenSpacePosition()))
		{
			FText Reason;
			ActiveWorkbench->UpdatePointer(ToViewportPixels(MouseEvent.GetScreenSpacePosition()), Reason);
		}
	}
	return Reply;
}

FReply UAssemblyHUDWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = Super::NativeOnMouseWheel(Geometry, MouseEvent);
	if (Reply.IsEventHandled() || !bForwardWorldPointerEvents || IsOverPlaceholderPanel(MouseEvent.GetScreenSpacePosition())) return Reply;
	if (UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench())
	{
		if (ActiveWorkbench->IsRigidPartTool())
		{
			FText Reason;
			PresentReason(ActiveWorkbench->RotateTool(MouseEvent.GetWheelDelta() * 15.0f, Reason), Reason);
			return FReply::Handled();
		}
	}
	return Reply;
}

void UAssemblyHUDWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	EndWorldPointerCapture(true);
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

bool UAssemblyHUDWidget::NativeOnDragOver(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, UDragDropOperation* Operation)
{
	if (Super::NativeOnDragOver(Geometry, DragDropEvent, Operation)) return true;
	UAssemblyDragDropOperation* AssemblyDrag = Cast<UAssemblyDragDropOperation>(Operation);
	if (!AssemblyDrag || AssemblyDrag->GetWorkbench() != GetWorkbench() || !AssemblyDrag->IsCurrentDrag()) return false;
	if (UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench())
	{
		FText Reason;
		ActiveWorkbench->UpdatePointer(ToViewportPixels(DragDropEvent.GetScreenSpacePosition()), Reason);
	}
	return true;
}

bool UAssemblyHUDWidget::NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, UDragDropOperation* Operation)
{
	if (Super::NativeOnDrop(Geometry, DragDropEvent, Operation)) return true;
	UAssemblyDragDropOperation* AssemblyDrag = Cast<UAssemblyDragDropOperation>(Operation);
	if (!AssemblyDrag || AssemblyDrag->GetWorkbench() != GetWorkbench() || !AssemblyDrag->IsCurrentDrag()) return false;
	if (IsOverPlaceholderPanel(DragDropEvent.GetScreenSpacePosition()))
	{
		AssemblyDrag->CancelAssemblyDrag();
		HandleMessage(FText::GetEmpty());
	}
	else
	{
		FText Reason;
		PresentReason(AssemblyDrag->FinishAtViewportPosition(ToViewportPixels(DragDropEvent.GetScreenSpacePosition()), Reason), Reason);
	}
	return true;
}
