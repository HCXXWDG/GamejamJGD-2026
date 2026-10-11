#include "Assembly/AssemblyHUDWidget.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyDragDropOperation.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Assembly/AssemblyShelfCardWidget.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
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

void UAssemblyHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
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
	}
}

void UAssemblyHUDWidget::UnbindPresentation()
{
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
	Widget->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo Font = Widget->GetFont();
	Font.Size = 14;
	Widget->SetFont(Font);
	return Widget;
}

UButton* UAssemblyHUDWidget::MakeButton(const FText& Value, EAssemblyHUDAction Action, int32 Argument, FGuid InstanceId, bool bJointAction)
{
	UButton* Button = WidgetTree->ConstructWidget<UAssemblyPointerButton>();
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetMinDesiredWidth(bJointAction ? 256.0f : 64.0f);
	Size->SetHeightOverride(64.0f);
	UTextBlock* Caption = MakeText(Value);
	Caption->SetJustification(ETextJustify::Center);
	Size->SetContent(Caption);
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
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("AssemblyWorldOverlay"));
	WidgetTree->RootWidget = Root;
	UBorder* WorldSurface = WidgetTree->ConstructWidget<UBorder>();
	WorldSurface->SetBrushColor(FLinearColor::Transparent);
	WorldSurface->SetVisibility(ESlateVisibility::Visible);
	UOverlaySlot* SurfaceSlot = Root->AddChildToOverlay(WorldSurface);
	SurfaceSlot->SetHorizontalAlignment(HAlign_Fill);
	SurfaceSlot->SetVerticalAlignment(VAlign_Fill);

	EditPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AssemblyEditPanel"));
	EditPanel->SetBrushColor(FLinearColor(0.035f, 0.045f, 0.065f, 0.95f));
	EditPanel->SetPadding(FMargin(8.0f));
	USizeBox* EditSize = WidgetTree->ConstructWidget<USizeBox>();
	EditSize->SetWidthOverride(288.0f);
	EditSize->SetMaxDesiredHeight(640.0f);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBox* Contents = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Contents);
	EditSize->SetContent(Scroll);
	EditPanel->SetContent(EditSize);
	UOverlaySlot* EditSlot = Root->AddChildToOverlay(EditPanel);
	EditSlot->SetHorizontalAlignment(HAlign_Left);
	EditSlot->SetVerticalAlignment(VAlign_Top);
	EditSlot->SetPadding(FMargin(16.0f));
	Contents->AddChild(MakeText(Text(TEXT("组装：将组件拖到场景中\nR 旋转 90° · 滚轮微调 · Esc 取消\nSpace 请求核心顺时针旋转"))));

	UHorizontalBox* Keys = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 Key = static_cast<int32>(EAssemblyMuscleKey::W); Key <= static_cast<int32>(EAssemblyMuscleKey::D); ++Key)
	{
		UButton* Button = MakeButton(FText::FromString(KeyName(static_cast<EAssemblyMuscleKey>(Key))), EAssemblyHUDAction::Binding, Key);
		KeyButtons.Add(Button);
		Keys->AddChild(Button);
	}
	Contents->AddChild(Keys);
	Contents->AddChild(MakeText(Text(TEXT("仅肌肉绑定 WASD：先选键，再拖组件。\n骨头自由摆放，肌肉两端必须在骨头上。"))));
	Shelf = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ComponentShelf"));
	Contents->AddChild(Shelf);
	SelectionCaption = MakeText(Text(TEXT("点击场景中的骨头、肌肉或关节进行选择。")));
	Contents->AddChild(SelectionCaption);
	UHorizontalBox* InstanceActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	InstanceActions->AddChild(MakeButton(Text(TEXT("移动")), EAssemblyHUDAction::Move));
	InstanceActions->AddChild(MakeButton(Text(TEXT("拆下")), EAssemblyHUDAction::Remove));
	InstanceActions->AddChild(MakeButton(Text(TEXT("取消")), EAssemblyHUDAction::Cancel));
	InstanceActions->AddChild(MakeButton(Text(TEXT("旋转")), EAssemblyHUDAction::Rotate, 90));
	Contents->AddChild(InstanceActions);
	Contents->AddChild(MakeText(Text(TEXT("关节：选择后设置种类和角度"))));
	JointList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AssemblyJoints"));
	Contents->AddChild(JointList);
	UHorizontalBox* JointKinds = WidgetTree->ConstructWidget<UHorizontalBox>();
	JointKinds->AddChild(MakeButton(Text(TEXT("活动")), EAssemblyHUDAction::JointKind, static_cast<int32>(EAssemblyJointKind::Hinge)));
	JointKinds->AddChild(MakeButton(Text(TEXT("固定")), EAssemblyHUDAction::JointKind, static_cast<int32>(EAssemblyJointKind::Fixed)));
	JointKinds->AddChild(MakeButton(Text(TEXT("可断\n活动")), EAssemblyHUDAction::JointKind, static_cast<int32>(EAssemblyJointKind::BreakableHinge)));
	JointKinds->AddChild(MakeButton(Text(TEXT("可断\n固定")), EAssemblyHUDAction::JointKind, static_cast<int32>(EAssemblyJointKind::BreakableFixed)));
	Contents->AddChild(JointKinds);
	UHorizontalBox* Limits = WidgetTree->ConstructWidget<UHorizontalBox>();
	MinAngle = WidgetTree->ConstructWidget<USpinBox>();
	MaxAngle = WidgetTree->ConstructWidget<USpinBox>();
	for (USpinBox* Spin : { MinAngle.Get(), MaxAngle.Get() })
	{
		Spin->SetMinValue(-180.0f);
		Spin->SetMaxValue(180.0f);
		Spin->SetMinDesiredWidth(64.0f);
		Spin->SetMaxFractionalDigits(1);
		Limits->AddChild(Spin);
	}
	Limits->AddChild(MakeButton(Text(TEXT("应用\n角度")), EAssemblyHUDAction::ApplyLimits));
	Contents->AddChild(Limits);
	Contents->AddChild(MakeButton(Text(TEXT("开始操控")), EAssemblyHUDAction::Start));
	Contents->AddChild(MakeButton(Text(TEXT("重新开始")), EAssemblyHUDAction::Restart));

	PlayPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AssemblyPlayingPanel"));
	PlayPanel->SetBrushColor(FLinearColor(0.035f, 0.045f, 0.065f, 0.90f));
	PlayPanel->SetPadding(FMargin(8.0f));
	USizeBox* PlaySize = WidgetTree->ConstructWidget<USizeBox>();
	PlaySize->SetWidthOverride(288.0f);
	PlaySize->SetMaxDesiredHeight(384.0f);
	UScrollBox* PlayScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBox* PlayingControls = WidgetTree->ConstructWidget<UVerticalBox>();
	PlayingControls->AddChild(MakeText(Text(TEXT("WASD：按下收缩，松开舒张\nSpace：核心顺时针旋转"))));
	PlayingControls->AddChild(MakeButton(Text(TEXT("重新开始")), EAssemblyHUDAction::Restart));
	PlayJointList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlayingJoints"));
	PlayingControls->AddChild(PlayJointList);
	PlayingControls->AddChild(MakeButton(Text(TEXT("请求断开选中关节")), EAssemblyHUDAction::BreakJoint));
	PlayScroll->AddChild(PlayingControls);
	PlaySize->SetContent(PlayScroll);
	PlayPanel->SetContent(PlaySize);
	UOverlaySlot* PlaySlot = Root->AddChildToOverlay(PlayPanel);
	PlaySlot->SetHorizontalAlignment(HAlign_Right);
	PlaySlot->SetVerticalAlignment(VAlign_Top);
	PlaySlot->SetPadding(FMargin(16.0f));

	UBorder* Status = WidgetTree->ConstructWidget<UBorder>();
	Status->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.03f, 0.8f));
	Status->SetPadding(FMargin(8.0f));
	Status->SetVisibility(ESlateVisibility::HitTestInvisible);
	USizeBox* StatusSize = WidgetTree->ConstructWidget<USizeBox>();
	StatusSize->SetWidthOverride(640.0f);
	StatusSize->SetMinDesiredHeight(64.0f);
	StatusCaption = MakeText(Text(TEXT("骨头可自由摆放。完成连通结构后点击“开始操控”。")));
	StatusSize->SetContent(StatusCaption);
	Status->SetContent(StatusSize);
	UOverlaySlot* StatusSlot = Root->AddChildToOverlay(Status);
	StatusSlot->SetHorizontalAlignment(HAlign_Center);
	StatusSlot->SetVerticalAlignment(VAlign_Bottom);
	StatusSlot->SetPadding(FMargin(16.0f));
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
	for (const FAssemblyBoneDefinition& Bone : Bones) Signature += TEXT("B:") + Bone.TypeId.ToString() + TEXT(":") + Bone.DisplayName.ToString() + TEXT(";");
	for (const FAssemblyMuscleDefinition& Muscle : Muscles) Signature += TEXT("M:") + Muscle.TypeId.ToString() + TEXT(":") + Muscle.DisplayName.ToString() + TEXT(";");
	if (Signature != ShelfSignature || Cards.Num() != Bones.Num() + Muscles.Num())
	{
		ShelfSignature = Signature;
		Shelf->ClearChildren();
		Cards.Reset();
		auto AddCard = [this, ActiveWorkbench](FName TypeId, FText Name, bool bMuscle)
		{
			UAssemblyShelfCardWidget* Card = GetOwningPlayer() ? CreateWidget<UAssemblyShelfCardWidget>(GetOwningPlayer(), ShelfCardClass)
				: CreateWidget<UAssemblyShelfCardWidget>(this, ShelfCardClass);
			if (!Card) return;
			Card->InitializeCard(ActiveWorkbench, TypeId, Name, bMuscle);
			Shelf->AddChild(Card);
			Cards.Add(Card);
		};
		for (const FAssemblyBoneDefinition& Bone : Bones) AddCard(Bone.TypeId, Bone.DisplayName, false);
		for (const FAssemblyMuscleDefinition& Muscle : Muscles) AddCard(Muscle.TypeId, Muscle.DisplayName, true);
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
		if (EditPanel) EditPanel->SetVisibility(bAssembly ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (PlayPanel) PlayPanel->SetVisibility(bAssembly ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		EAssemblyMuscleKey HighlightKey = Model->GetSelectedMuscleKey();
		const FAssemblySelection Selection = ActiveWorkbench->GetSelection();
		FString Description = TEXT("未选择组件。点击骨头、肌肉或关节。");
		if (Selection.Kind == EAssemblySelectionKind::Bone)
		{
			FAssemblyBoneInstance Bone;
			if (Model->GetBone(Selection.InstanceId, Bone))
				Description = FString::Printf(TEXT("骨头 %s\n位置 %.1f, %.1f · 角度 %.1f°"), *Bone.TypeId.ToString(), Bone.LocalPosition.X, Bone.LocalPosition.Y, Bone.RotationDegrees);
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
		for (int32 Index = 0; Index < KeyButtons.Num(); ++Index)
		{
			if (KeyButtons[Index]) KeyButtons[Index]->SetBackgroundColor(static_cast<int32>(HighlightKey) == Index + 1
				? FLinearColor(0.10f, 0.65f, 0.80f) : FLinearColor(0.16f, 0.20f, 0.27f));
		}
		RefreshShelf();
		RefreshJoints();
	}
	bRefreshing = false;
	OnHUDPresentationChanged();
}

void UAssemblyHUDWidget::HandleChanged() { RefreshPresentation(); }

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
			return Text(TEXT("拖动骨头到场景中。R 旋转 90°，滚轮微调；松开安装，Esc 或右键取消。"));
		case EAssemblyWorkbenchTool::MuscleFirst:
			return Text(TEXT("在骨头上选择肌肉第一连接点；从组件栏拖出时，在骨头上松开。Esc 或右键取消。"));
		case EAssemblyWorkbenchTool::MuscleSecond:
			return Text(TEXT("点击骨头上的第二连接点；同一骨头可选两个不同位置。Esc 或右键取消。"));
		default: break;
		}
		if (const UAssemblyComponent* Model = ActiveWorkbench->GetAssemblyComponent())
		{
			if (Model->GetPhase() == EAssemblyPhase::Playing)
				return Text(TEXT("WASD 按下请求收缩，松开请求舒张；Space 请求核心旋转。可点击“重新开始”回到组装。"));
		}
	}
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
	UAssemblySessionComponent* ActiveSession = Session.Get();
	UAssemblyWorkbenchComponent* ActiveWorkbench = GetWorkbench();
	if (!ActiveSession || !ActiveWorkbench) return;
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
	if (!bPlaceholderBuilt) return false;
	return (EditPanel && EditPanel->GetVisibility() == ESlateVisibility::Visible && EditPanel->GetCachedGeometry().IsUnderLocation(AbsolutePosition))
		|| (PlayPanel && PlayPanel->GetVisibility() == ESlateVisibility::Visible && PlayPanel->GetCachedGeometry().IsUnderLocation(AbsolutePosition));
}

bool UAssemblyHUDWidget::OwnsCapturedTool() const
{
	const UAssemblyWorkbenchComponent* ActiveWorkbench = CapturedWorkbench.Get();
	return bOwnsPointerCapture && CapturedToolId.IsValid() && ActiveWorkbench
		&& ActiveWorkbench->GetToolId() == CapturedToolId && ActiveWorkbench->GetTool() == EAssemblyWorkbenchTool::Bone;
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
		&& ActiveWorkbench->GetTool() == EAssemblyWorkbenchTool::Bone)
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
		// Returning a moved installed bone to the shelf requests a real removal, subject to connectivity.
		const FAssemblySelection Selection = ActiveWorkbench->GetSelection();
		const bool bReturningInstalledBone = ActiveWorkbench->GetTool() == EAssemblyWorkbenchTool::Bone && Selection.Kind == EAssemblySelectionKind::Bone
			&& Shelf && Shelf->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition());
		ActiveWorkbench->CancelTool();
		HandleMessage(FText::GetEmpty());
		if (bReturningInstalledBone)
		{
			FText Reason;
			PresentReason(ActiveWorkbench->RemoveSelected(Reason), Reason);
		}
	}
	else if (ActiveWorkbench->GetTool() == EAssemblyWorkbenchTool::Bone)
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
		if (ActiveWorkbench->GetTool() == EAssemblyWorkbenchTool::Bone)
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
