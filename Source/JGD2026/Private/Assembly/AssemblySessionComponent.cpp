#include "Assembly/AssemblySessionComponent.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyHUDWidget.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/CoreDelegates.h"
#include "Slate/SceneViewport.h"
#include "Slate/SGameLayerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AssemblySession"

namespace
{
	// A new pawn can initialize before the old pawn releases the same local-player IMC.
	// Count only Session registrations, without assuming that the asset enables UE's tracking mode.
	struct FSessionMappingRegistration
	{
		TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem;
		TWeakObjectPtr<UInputMappingContext> Mapping;
		TArray<TWeakObjectPtr<UAssemblySessionComponent>> Users;
		bool bAddedContext = false;
	};
	TArray<FSessionMappingRegistration> SessionMappings;

	void ReleaseUnusedSessionMappings()
	{
		for (int32 Index = SessionMappings.Num() - 1; Index >= 0; --Index)
		{
			FSessionMappingRegistration& Registration = SessionMappings[Index];
			Registration.Users.RemoveAll([](const TWeakObjectPtr<UAssemblySessionComponent>& User) { return !User.IsValid(); });
			if (!Registration.Subsystem.IsValid() || !Registration.Mapping.IsValid() || Registration.Users.IsEmpty())
			{
				const FSessionMappingRegistration Removed = Registration;
				SessionMappings.RemoveAtSwap(Index);
				if (Removed.bAddedContext && Removed.Subsystem.IsValid() && Removed.Mapping.IsValid())
				{
					Removed.Subsystem->RemoveMappingContext(Removed.Mapping.Get());
				}
				// Removal delegates may register/release sessions; resume from the updated array.
				Index = SessionMappings.Num();
			}
		}
	}

	void RegisterSessionMapping(UAssemblySessionComponent* Session, UEnhancedInputLocalPlayerSubsystem* Subsystem,
		UInputMappingContext* Mapping, int32 Priority)
	{
		ReleaseUnusedSessionMappings();
		FSessionMappingRegistration* Registration = SessionMappings.FindByPredicate([Subsystem, Mapping](const FSessionMappingRegistration& Entry)
		{
			return Entry.Subsystem.Get() == Subsystem && Entry.Mapping.Get() == Mapping;
		});
		if (!Registration)
		{
			Registration = &SessionMappings.AddDefaulted_GetRef();
			Registration->Subsystem = Subsystem;
			Registration->Mapping = Mapping;
		}
		const bool bNeedsContext = !Subsystem->HasMappingContext(Mapping);
		Registration->bAddedContext |= bNeedsContext;
		Registration->Users.AddUnique(Session);
		// Mapping delegates may reenter and change the registration table.
		if (bNeedsContext) { Subsystem->AddMappingContext(Mapping, Priority); }
	}

	void UnregisterSessionMapping(UAssemblySessionComponent* Session, UEnhancedInputLocalPlayerSubsystem* Subsystem,
		UInputMappingContext* Mapping)
	{
		if (FSessionMappingRegistration* Registration = SessionMappings.FindByPredicate([Subsystem, Mapping](const FSessionMappingRegistration& Entry)
			{ return Entry.Subsystem.Get() == Subsystem && Entry.Mapping.Get() == Mapping; }))
		{
			Registration->Users.RemoveAll([Session](const TWeakObjectPtr<UAssemblySessionComponent>& User)
				{ return !User.IsValid() || User.Get() == Session; });
		}
		ReleaseUnusedSessionMappings();
	}
}

UAssemblySessionComponent::UAssemblySessionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	HUDClass = UAssemblyHUDWidget::StaticClass();
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> Mapping(TEXT("/Game/Character/Player/Input/IMC_Player.IMC_Player"));
	static ConstructorHelpers::FObjectFinder<UInputAction> W(TEXT("/Game/Character/Player/Input/Actions/IA_W.IA_W"));
	static ConstructorHelpers::FObjectFinder<UInputAction> A(TEXT("/Game/Character/Player/Input/Actions/IA_A.IA_A"));
	static ConstructorHelpers::FObjectFinder<UInputAction> S(TEXT("/Game/Character/Player/Input/Actions/IA_S.IA_S"));
	static ConstructorHelpers::FObjectFinder<UInputAction> D(TEXT("/Game/Character/Player/Input/Actions/IA_D.IA_D"));
	InputMappingContext = Mapping.Object;
	WAction = W.Object;
	AAction = A.Object;
	SAction = S.Object;
	DAction = D.Object;
}

UAssemblyWorkbenchComponent* UAssemblySessionComponent::GetWorkbench() const { return BoundWorkbench.Get(); }
UAssemblyComponent* UAssemblySessionComponent::GetAssemblyComponent() const { return BoundAssembly.Get(); }
APlayerController* UAssemblySessionComponent::GetPlayerController() const { return BoundController.Get(); }
UAssemblyHUDWidget* UAssemblySessionComponent::GetHUD() const { return IsValid(HUD) ? HUD.Get() : nullptr; }

void UAssemblySessionComponent::BeginPlay()
{
	Super::BeginPlay();
	// Actor Blueprint BeginPlay may still configure the frame/model after component BeginPlay.
}

bool UAssemblySessionComponent::InitializeSession(APlayerController* PC, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (bInitializing || bReleasing)
	{
		OutReason = LOCTEXT("SessionBusy", "组装会话正在初始化或关闭，请稍后重试。");
		return false;
	}
	if (!GetWorld() || !GetWorld()->IsGameWorld() || !IsValid(Pawn) || !Pawn->HasActorBegunPlay()
		|| !IsValid(PC) || !PC->IsLocalController() || PC->GetPawn() != Pawn || Pawn->GetController() != PC)
	{
		OutReason = LOCTEXT("InvalidLocalPawn", "请在角色 BeginPlay 完成且被本地玩家控制后初始化组装会话。");
		return false;
	}
	UAssemblyComponent* Model = Pawn->FindComponentByClass<UAssemblyComponent>();
	UAssemblyInteractionComponent* Interaction = Pawn->FindComponentByClass<UAssemblyInteractionComponent>();
	if (!IsValid(Model) || !IsValid(Interaction))
	{
		OutReason = LOCTEXT("MissingComponents", "角色需要已有的 Assembly 和 AssemblyInteraction 组件。");
		return false;
	}
	if (bInitialized && BoundController.Get() == PC && BoundAssembly.Get() == Model
		&& BoundInteraction.Get() == Interaction && BoundWorkbench.IsValid() && IsValid(SessionInput))
	{
		bAutoInitializationSuppressed = false;
		return EnsureHUD(OutReason);
	}
	TArray<UAssemblySessionComponent*> Sessions;
	Pawn->GetComponents(Sessions);
	for (const UAssemblySessionComponent* Other : Sessions)
	{
		if (IsValid(Other) && Other != this && (Other->bInitialized || Other->bInitializing))
		{
			OutReason = LOCTEXT("DuplicateSession", "同一角色已有活动的组装会话，请只保留一个 Session 组件。");
			return false;
		}
	}
	ReleaseSession();
	TGuardValue<bool> InitializingGuard(bInitializing, true);
	bAutoInitializationSuppressed = false;
	// An already configured round survives controller changes and repeated initialization.
	if (!Model->IsInitialized() && !Model->EnterAssembly(OutReason)) { return false; }
	if (!Interaction->InitializeInteraction(Model, OutReason)) { return false; }
	UAssemblyWorkbenchComponent* Workbench = Pawn->FindComponentByClass<UAssemblyWorkbenchComponent>();
	if (!Workbench)
	{
		Workbench = NewObject<UAssemblyWorkbenchComponent>(Pawn, NAME_None, RF_Transient);
		Pawn->AddInstanceComponent(Workbench);
		Workbench->RegisterComponent();
	}
	if (!Workbench->InitializeWorkbench(Model, Interaction, PC, OutReason)) { return false; }
	BoundAssembly = Model;
	BoundInteraction = Interaction;
	BoundWorkbench = Workbench;
	BoundController = PC;
	Model->OnAssemblyChanged.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleModelChanged);
	Model->OnAssemblyReset.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleModelReset);
	Model->OnPhaseChanged.AddUniqueDynamic(this, &UAssemblySessionComponent::HandlePhaseChanged);
	Workbench->OnWorkbenchChanged.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleWorkbenchChanged);
	Workbench->OnMessage.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleWorkbenchMessage);
	if (!BindSessionInput(OutReason)) { ReleaseSession(); return false; }
	bInitialized = true;
	UpdateMovementGate();
	DeactivationHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &UAssemblySessionComponent::HandleApplicationDeactivated);
	BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UAssemblySessionComponent::HandleApplicationDeactivated);
	if (!EnsureHUD(OutReason)) { ReleaseSession(); return false; }
	if (!bInitialized || bAutoInitializationSuppressed || BoundController.Get() != PC)
	{
		OutReason = LOCTEXT("InitializationCanceled", "组装会话在初始化期间已被关闭。");
		ReleaseSession();
		return false;
	}
	bViewportWasFocused = IsSessionViewportFocused();
	bModelRefreshPending = false;
	LastInitializationFailure = FText::GetEmpty();
	OnSessionChanged.Broadcast();
	return true;
}

bool UAssemblySessionComponent::BindSessionInput(FText& OutReason)
{
	APlayerController* PC = BoundController.Get();
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem || !Subsystem->GetPlayerInput())
	{
		OutReason = LOCTEXT("NoEnhancedInput", "本地玩家的 Enhanced Input 尚未准备好。");
		return false;
	}
	if (!IsValid(InputMappingContext) || !IsValid(WAction) || !IsValid(AAction) || !IsValid(SAction) || !IsValid(DAction))
	{
		OutReason = LOCTEXT("MissingInputAssets", "请配置 IMC_Player 和 IA_W、IA_A、IA_S、IA_D 输入资产。");
		return false;
	}
	const TSet<const UInputAction*> UniqueActions = { WAction.Get(), AAction.Get(), SAction.Get(), DAction.Get() };
	if (UniqueActions.Num() != 4)
	{
		OutReason = LOCTEXT("DuplicateActions", "W/A/S/D 需要配置四个不同的输入动作。");
		return false;
	}
	BoundInputSubsystem = Subsystem;
	AddedMappingContext = InputMappingContext;
	RegisterSessionMapping(this, Subsystem, InputMappingContext, MappingPriority);
	if (BoundController.Get() != PC || bAutoInitializationSuppressed || PC->GetPawn() != GetOwner())
	{
		OutReason = LOCTEXT("InputInitializationCanceled", "本地控制在输入初始化期间已结束。");
		return false;
	}
	SessionInput = NewObject<UEnhancedInputComponent>(PC, NAME_None, RF_Transient);
	PC->AddInstanceComponent(SessionInput);
	SessionInput->Priority = 10;
	SessionInput->bBlockInput = false;
	SessionInput->RegisterComponent();
	auto BindMuscle = [this](UInputAction* Action, EAssemblyMuscleKey Key)
	{
		ActionBindingHandles.Add(SessionInput->BindAction(Action, ETriggerEvent::Started, this, &UAssemblySessionComponent::HandleMusclePressed, Key).GetHandle());
		ActionBindingHandles.Add(SessionInput->BindAction(Action, ETriggerEvent::Completed, this, &UAssemblySessionComponent::HandleMuscleReleased, Key).GetHandle());
		ActionBindingHandles.Add(SessionInput->BindAction(Action, ETriggerEvent::Canceled, this, &UAssemblySessionComponent::HandleMuscleReleased, Key).GetHandle());
	};
	BindMuscle(WAction, EAssemblyMuscleKey::W);
	BindMuscle(AAction, EAssemblyMuscleKey::A);
	BindMuscle(SAction, EAssemblyMuscleKey::S);
	BindMuscle(DAction, EAssemblyMuscleKey::D);
	UInputComponent* LegacyInput = SessionInput.Get();
	auto BindKey = [this, LegacyInput](FKey Key, EInputEvent Event, void (UAssemblySessionComponent::*Handler)())
	{
		// EnhancedInputComponent deletes the helper overload, but EnhancedPlayerInput still processes these bindings.
		FInputKeyBinding& Binding = LegacyInput->BindKey(Key, Event, this, Handler);
		Binding.bConsumeInput = false;
	};
	BindKey(EKeys::SpaceBar, IE_Pressed, &UAssemblySessionComponent::HandleSpacePressed);
	BindKey(EKeys::SpaceBar, IE_Released, &UAssemblySessionComponent::HandleSpaceReleased);
	BindKey(EKeys::R, IE_Pressed, &UAssemblySessionComponent::HandleRotatePressed);
	BindKey(EKeys::Escape, IE_Pressed, &UAssemblySessionComponent::HandleCancelPressed);
	BindKey(EKeys::Delete, IE_Pressed, &UAssemblySessionComponent::HandleDeletePressed);
	if (bBindBackspaceToRestart) { BindKey(EKeys::BackSpace, IE_Pressed, &UAssemblySessionComponent::HandleRestartPressed); }
	if (bBindJointBreakKey) { BindKey(EKeys::J, IE_Pressed, &UAssemblySessionComponent::HandleJointBreakPressed); }
	PC->PushInputComponent(SessionInput);
	return true;
}

void UAssemblySessionComponent::UnbindSessionInput()
{
	if (IsValid(SessionInput))
	{
		if (BoundController.IsValid()) { BoundController->PopInputComponent(SessionInput); }
		for (const uint32 Handle : ActionBindingHandles) { SessionInput->RemoveBindingByHandle(Handle); }
		// ClearBindingsForObject does not remove legacy KeyBindings in UE 5.8.
		SessionInput->KeyBindings.RemoveAll([this](const FInputKeyBinding& Binding) { return Binding.KeyDelegate.IsBoundToObject(this); });
		if (BoundController.IsValid()) { BoundController->RemoveInstanceComponent(SessionInput); }
		SessionInput->DestroyComponent();
	}
	SessionInput = nullptr;
	ActionBindingHandles.Reset();
	UnregisterSessionMapping(this, BoundInputSubsystem.Get(), AddedMappingContext);
	AddedMappingContext = nullptr;
	BoundInputSubsystem.Reset();
}

bool UAssemblySessionComponent::EnsureHUD(FText& OutReason)
{
	if (!IsValid(HUD))
	{
		HUD = nullptr;
		bOwnedHUD = false;
		if (!bCreateHUD) { return true; }
		if (!HUDClass)
		{
			OutReason = LOCTEXT("NoHUDClass", "请指定 HUDClass，或关闭 Create HUD 并使用现有蓝图界面。");
			return false;
		}
		HUD = CreateWidget<UAssemblyHUDWidget>(BoundController.Get(), HUDClass);
		if (!HUD)
		{
			OutReason = LOCTEXT("HUDCreationFailed", "无法创建组装 HUD，请检查所配置的 Widget 类。");
			return false;
		}
		bOwnedHUD = true;
	}
	if (bOwnedHUD) { HUD->SetOwningPlayer(BoundController.Get()); }
	HUD->InitializeHUD(this);
	if (!bInitialized || bReleasing || !BoundController.IsValid())
	{
		OutReason = LOCTEXT("HUDInitializationCanceled", "组装会话在 HUD 初始化期间已被关闭。");
		return false;
	}
	if (bOwnedHUD && bCreateHUD && !HUD->IsInViewport())
	{
		if (!HUD->AddToPlayerScreen(HUDZOrder))
		{
			OutReason = LOCTEXT("HUDViewportFailed", "本地玩家视口尚未准备好，无法显示组装 HUD。");
			return false;
		}
		bHUDAddedToViewport = true;
	}
	if (bOwnedHUD && bCreateHUD && bConfigureHUDInputMode && !bHUDInputConfigured && BoundController.IsValid())
	{
		APlayerController* PC = BoundController.Get();
		bPreviousShowMouseCursor = PC->bShowMouseCursor;
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UGameViewportClient* Viewport = LocalPlayer->ViewportClient.Get())
			{
				SavedHUDViewport = Viewport;
				PreviousMouseCaptureMode = Viewport->GetMouseCaptureMode();
				PreviousMouseLockMode = Viewport->GetMouseLockMode();
				bPreviousHideCursorDuringCapture = Viewport->HideCursorDuringCapture();
				bPreviousViewportIgnoreInput = Viewport->IgnoreInput();
			}
		}
		if (FSlateApplication::IsInitialized()) { PreviousKeyboardFocus = FSlateApplication::Get().GetKeyboardFocusedWidget(); }
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
		bHUDInputConfigured = true;
	}
	return true;
}

void UAssemblySessionComponent::RestoreHUDInput()
{
	if (!bHUDInputConfigured) { return; }
	if (BoundController.IsValid() && (!BoundController->GetPawn() || BoundController->GetPawn() == GetOwner()))
	{
		APlayerController* PC = BoundController.Get();
		if (PC->bShowMouseCursor) { PC->bShowMouseCursor = bPreviousShowMouseCursor; }
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (SavedHUDViewport.IsValid() && LocalPlayer->ViewportClient.Get() == SavedHUDViewport.Get())
			{
				SavedHUDViewport->SetMouseCaptureMode(PreviousMouseCaptureMode);
				SavedHUDViewport->SetMouseLockMode(PreviousMouseLockMode);
				SavedHUDViewport->SetHideCursorDuringCapture(bPreviousHideCursorDuringCapture);
				SavedHUDViewport->SetIgnoreInput(bPreviousViewportIgnoreInput);
			}
		}
		// Restore the settings actually changed by our mode; UE exposes no complete input-mode snapshot.
		if (FSlateApplication::IsInitialized() && IsSessionViewportFocused())
		{
			if (TSharedPtr<SWidget> Previous = PreviousKeyboardFocus.Pin())
			{
				FSlateApplication::Get().SetKeyboardFocus(Previous, EFocusCause::SetDirectly);
			}
			else if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
			{
				FSlateApplication::Get().SetUserFocusToGameViewport(LocalPlayer->GetControllerId(), EFocusCause::SetDirectly);
			}
		}
	}
	PreviousKeyboardFocus.Reset();
	SavedHUDViewport.Reset();
	bHUDInputConfigured = false;
}

void UAssemblySessionComponent::DetachHUD()
{
	RestoreHUDInput();
	if (IsValid(HUD))
	{
		HUD->DetachHUD();
		if (bOwnedHUD && bHUDAddedToViewport) { HUD->RemoveFromParent(); }
	}
	bHUDAddedToViewport = false;
}

void UAssemblySessionComponent::SetHUD(UAssemblyHUDWidget* InHUD)
{
	if (IsValid(InHUD) && InHUD->GetWorld() != GetWorld())
	{
		ShowMessage(LOCTEXT("HUDDifferentWorld", "提供的 HUD 必须属于当前游戏世界。"));
		return;
	}
	if (HUD == InHUD)
	{
		if (bInitialized && IsValid(HUD)) { HUD->InitializeHUD(this); }
		return;
	}
	DetachHUD();
	HUD = InHUD;
	bOwnedHUD = false;
	if (bInitialized && IsValid(HUD)) { HUD->InitializeHUD(this); }
	OnSessionChanged.Broadcast();
}

void UAssemblySessionComponent::ShowMessage(FText Message)
{
	OnMessage.Broadcast(Message);
}

bool UAssemblySessionComponent::CanUseSession(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!bInitialized || bReleasing || !BoundAssembly.IsValid() || !BoundAssembly->IsInitialized()
		|| !BoundWorkbench.IsValid() || !BoundController.IsValid() || !BoundController->IsLocalController()
		|| BoundController->GetPawn() != GetOwner())
	{
		OutReason = LOCTEXT("SessionUnavailable", "请先初始化当前本地角色的组装会话。");
		return false;
	}
	return true;
}

bool UAssemblySessionComponent::StartPlaying(FText& OutReason)
{
	if (!CanUseSession(OutReason)) { ShowMessage(OutReason); return false; }
	BoundWorkbench->CancelTool();
	// Keep the selection so a chosen breakable joint can still be controlled while playing.
	if (!BoundAssembly->TryStartPlaying(OutReason)) { ShowMessage(OutReason); return false; }
	ApplyHeldMuscleInput();
	bReplayHeldMusclesPending = false;
	UpdateMovementGate();
	ShowMessage(FText::GetEmpty());
	OnSessionChanged.Broadcast();
	return true;
}

bool UAssemblySessionComponent::RestartSession(FText& OutReason)
{
	if (!CanUseSession(OutReason)) { ShowMessage(OutReason); return false; }
	ClearHeldInput();
	BoundWorkbench->CancelTool();
	if (!BoundAssembly->RestartAssembly(OutReason)) { ShowMessage(OutReason); return false; }
	BoundWorkbench->ClearSelection();
	// Interaction owns safe spawn-pose restoration; physical bodies/constraints remain external.
	if (BoundInteraction.IsValid())
	{
		if (UAssemblyPhysicsBridgeComponent* Bridge = BoundInteraction->GetPhysicsBridge()) { Bridge->RequestPhysicsRebuild(); }
	}
	UpdateMovementGate();
	ShowMessage(FText::GetEmpty());
	OnSessionChanged.Broadcast();
	return true;
}

void UAssemblySessionComponent::ClearHeldInput()
{
	HeldMuscleKeys.Reset();
	bSpaceHeld = false;
	bReplayHeldMusclesPending = false;
	if (BoundAssembly.IsValid()) { BoundAssembly->ClearInputState(); }
}

void UAssemblySessionComponent::UpdateMovementGate()
{
	if (!BoundController.IsValid()) { bIgnoreMoveInputAdded = false; return; }
	const bool bShouldIgnore = bInitialized && BoundAssembly.IsValid() && BoundAssembly->GetPhase() == EAssemblyPhase::Assembly;
	if (bShouldIgnore != bIgnoreMoveInputAdded)
	{
		// SetIgnoreMoveInput uses a count. Release only the count added by this session.
		BoundController->SetIgnoreMoveInput(bShouldIgnore);
		bIgnoreMoveInputAdded = bShouldIgnore;
	}
}

void UAssemblySessionComponent::ApplyHeldMuscleInput()
{
	if (!CanAcceptInput() || BoundAssembly->GetPhase() != EAssemblyPhase::Playing) { return; }
	const TArray<EAssemblyMuscleKey> Keys = HeldMuscleKeys.Array();
	for (const EAssemblyMuscleKey Key : Keys)
	{
		if (!CanAcceptInput()) { break; }
		if (HeldMuscleKeys.Contains(Key)) { BoundAssembly->SetMuscleInput(Key, true); }
	}
}

bool UAssemblySessionComponent::IsSessionViewportFocused() const
{
	const APlayerController* PC = BoundController.Get();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	const FSceneViewport* Viewport = ViewportClient ? ViewportClient->GetGameViewport() : nullptr;
	if (!Viewport || !FSlateApplication::IsInitialized() || !Viewport->IsForegroundWindow()) { return false; }
	if (Viewport->HasFocus()) { return true; }
	// A HUD click shifts focus away from SViewport, but remains within the game's layer tree.
	if (TSharedPtr<IGameLayerManager> Layers = ViewportClient->GetGameLayerManager())
	{
		if (Layers->AsWidget()->HasAnyUserFocusOrFocusedDescendants()) { return true; }
	}
	return IsValid(HUD) && (HUD->HasAnyUserFocus() || HUD->HasFocusedDescendants());
}

bool UAssemblySessionComponent::CanAcceptInput() const
{
	return bInitialized && !bReleasing && BoundController.IsValid() && BoundAssembly.IsValid()
		&& GetWorld() && !GetWorld()->IsPaused() && BoundController->GetPawn() == GetOwner() && IsSessionViewportFocused();
}

void UAssemblySessionComponent::HandleApplicationDeactivated()
{
	if (!bInitialized) { return; }
	ClearHeldInput();
	if (BoundWorkbench.IsValid()) { BoundWorkbench->CancelTool(); }
	bViewportWasFocused = false;
}

void UAssemblySessionComponent::HandleMusclePressed(EAssemblyMuscleKey Key)
{
	if (!CanAcceptInput() || HeldMuscleKeys.Contains(Key)) { return; }
	HeldMuscleKeys.Add(Key);
	if (BoundAssembly->GetPhase() == EAssemblyPhase::Playing) { BoundAssembly->SetMuscleInput(Key, true); }
	else if (BoundWorkbench.IsValid())
	{
		FText Reason;
		BoundWorkbench->SelectBindingKey(Key, Reason);
	}
}

void UAssemblySessionComponent::HandleMuscleReleased(EAssemblyMuscleKey Key)
{
	HeldMuscleKeys.Remove(Key);
	if (BoundAssembly.IsValid()) { BoundAssembly->SetMuscleInput(Key, false); }
}

void UAssemblySessionComponent::HandleSpacePressed()
{
	if (!CanAcceptInput() || bSpaceHeld) { return; }
	bSpaceHeld = BoundAssembly->SetCoreRotationInput(true);
}

void UAssemblySessionComponent::HandleSpaceReleased()
{
	bSpaceHeld = false;
	if (BoundAssembly.IsValid()) { BoundAssembly->SetCoreRotationInput(false); }
}

void UAssemblySessionComponent::HandleRotatePressed()
{
	if (!CanAcceptInput() || !BoundWorkbench.IsValid()) { return; }
	FText Reason;
	BoundWorkbench->RotateTool(RotationStep, Reason);
}

void UAssemblySessionComponent::HandleCancelPressed()
{
	if (!CanAcceptInput()) { return; }
	if (BoundWorkbench.IsValid()) { BoundWorkbench->CancelTool(); }
	ClearHeldInput();
}

void UAssemblySessionComponent::HandleDeletePressed()
{
	if (!CanAcceptInput() || !BoundWorkbench.IsValid()) { return; }
	FText Reason;
	BoundWorkbench->RemoveSelected(Reason);
}

void UAssemblySessionComponent::HandleRestartPressed()
{
	if (!CanAcceptInput()) { return; }
	FText Reason;
	RestartSession(Reason);
}

void UAssemblySessionComponent::HandleJointBreakPressed()
{
	if (!CanAcceptInput() || !BoundWorkbench.IsValid()) { return; }
	FText Reason;
	BoundWorkbench->RequestSelectedJointBreak(Reason);
}

void UAssemblySessionComponent::HandleModelChanged() { bModelRefreshPending = true; }
void UAssemblySessionComponent::HandleWorkbenchChanged() { bModelRefreshPending = true; }
void UAssemblySessionComponent::HandleWorkbenchMessage(FText Message) { ShowMessage(Message); }
void UAssemblySessionComponent::HandlePhaseChanged(EAssemblyPhase Phase)
{
	// This only adjusts the controller's input count; it never mutates the notifying model.
	UpdateMovementGate();
	bReplayHeldMusclesPending = Phase == EAssemblyPhase::Playing;
	bModelRefreshPending = true;
}

void UAssemblySessionComponent::HandleModelReset()
{
	// The model has already released held intents. Only clear local bookkeeping in this callback.
	HeldMuscleKeys.Reset();
	bSpaceHeld = false;
	bModelRefreshPending = true;
}

void UAssemblySessionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* DesiredController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (DesiredController && (!DesiredController->IsLocalController() || DesiredController->GetPawn() != Pawn)) { DesiredController = nullptr; }
	if (bInitialized && (BoundController.Get() != DesiredController || !BoundAssembly.IsValid()
		|| !BoundInteraction.IsValid() || !BoundWorkbench.IsValid() || !IsValid(SessionInput)))
	{
		ReleaseSession();
	}
	if (!bInitialized && bAutoInitialize && !bAutoInitializationSuppressed && IsValid(Pawn)
		&& Pawn->HasActorBegunPlay() && DesiredController)
	{
		FText Reason;
		if (!InitializeSession(DesiredController, Reason) && !Reason.EqualTo(LastInitializationFailure))
		{
			LastInitializationFailure = Reason;
			ShowMessage(Reason);
		}
	}
	if (!bInitialized) { return; }
	const bool bFocused = GetWorld() && !GetWorld()->IsPaused() && IsSessionViewportFocused();
	if (!bFocused && bViewportWasFocused) { HandleApplicationDeactivated(); }
	bViewportWasFocused = bFocused;
	UpdateMovementGate();
	if (bReplayHeldMusclesPending)
	{
		bReplayHeldMusclesPending = false;
		ApplyHeldMuscleInput();
	}
	if (bModelRefreshPending)
	{
		bModelRefreshPending = false;
		// Model notifications use a mutation guard; publish UI/session updates only after it has unwound.
		OnSessionChanged.Broadcast();
	}
}

void UAssemblySessionComponent::ReleaseSession()
{
	if (bReleasing) { return; }
	TGuardValue<bool> ReleasingGuard(bReleasing, true);
	ClearHeldInput();
	FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivationHandle);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
	DeactivationHandle.Reset();
	BackgroundHandle.Reset();
	DetachHUD();
	UnbindSessionInput();
	if (bIgnoreMoveInputAdded && BoundController.IsValid()) { BoundController->SetIgnoreMoveInput(false); }
	bIgnoreMoveInputAdded = false;
	if (BoundAssembly.IsValid())
	{
		BoundAssembly->OnAssemblyChanged.RemoveDynamic(this, &UAssemblySessionComponent::HandleModelChanged);
		BoundAssembly->OnAssemblyReset.RemoveDynamic(this, &UAssemblySessionComponent::HandleModelReset);
		BoundAssembly->OnPhaseChanged.RemoveDynamic(this, &UAssemblySessionComponent::HandlePhaseChanged);
	}
	if (BoundWorkbench.IsValid())
	{
		BoundWorkbench->OnWorkbenchChanged.RemoveDynamic(this, &UAssemblySessionComponent::HandleWorkbenchChanged);
		BoundWorkbench->OnMessage.RemoveDynamic(this, &UAssemblySessionComponent::HandleWorkbenchMessage);
		BoundWorkbench->ShutdownWorkbench();
	}
	BoundWorkbench.Reset();
	BoundInteraction.Reset();
	BoundAssembly.Reset();
	BoundController.Reset();
	bInitialized = false;
	bViewportWasFocused = false;
	bModelRefreshPending = false;
	bReplayHeldMusclesPending = false;
}

void UAssemblySessionComponent::ShutdownSession()
{
	bAutoInitializationSuppressed = true;
	ReleaseSession();
	OnSessionChanged.Broadcast();
}

void UAssemblySessionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseSession();
	Super::EndPlay(EndPlayReason);
}

void UAssemblySessionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ReleaseSession();
	HUD = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#undef LOCTEXT_NAMESPACE
