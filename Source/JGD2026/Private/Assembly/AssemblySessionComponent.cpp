#include "Assembly/AssemblySessionComponent.h"
#include "Progress/JGDLevelRatingComponent.h"
#include "Audio/JGDInputFeedbackLibrary.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyHUDWidget.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblyPhysicsMessages.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/AssemblyExperienceSettings.h"
#include "Assembly/AssemblyPlayerExperienceComponent.h"
#include "Assembly/AssemblyCameraBackgroundComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "UI/JGDLoadingScreenSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
bool UAssemblySessionComponent::CanOwnPhysicsLifecycle(const UAssemblyComponent* Model) const
{
	if (!HasPhysicsSessionClaim(Model)) { return false; }
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()); Character && bSuspendCharacterMovement)
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		return bCharacterMovementSuspended && IsValid(Movement) && SuspendedCharacterMovement.Get() == Movement
			&& Movement->MovementMode == MOVE_None && Movement->CustomMovementMode == 0 && !Movement->IsComponentTickEnabled();
	}
	return true;
}

bool UAssemblySessionComponent::HasPhysicsSessionClaim(const UAssemblyComponent* Model) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = BoundController.Get();
	if ((!bInitialized && !bInitializing) || bReleasing || bEndingPlay || bAutoInitializationSuppressed
		|| bProxyRestorePending || !IsValid(Model) || BoundAssembly.Get() != Model || Model->GetWorld() != GetWorld()
		|| !IsValid(Pawn) || Model->GetOwner() != Pawn || Pawn->IsActorBeingDestroyed() || !IsValid(PC) || !PC->IsLocalController()
		|| PC->GetPawn() != Pawn || Pawn->GetController() != PC)
	{
		return false;
	}
	return true;
}

bool UAssemblySessionComponent::ValidatePhysicsProxyReady(const UAssemblyComponent* Model,
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!HasPhysicsSessionClaim(Model) || Snapshot.Context.Assembly != Model || Snapshot.Context.World != GetWorld())
	{
		OutReason = LOCTEXT("PhysicsSessionNotClaimed", "正式物理需要由当前本地角色的 Assembly Session 接管；会话、模型或世界已失效。");
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) { return true; }
	if (!bSuspendCharacterMovement)
	{
		OutReason = LOCTEXT("FormalMovementProtectionDisabled", "正式物理不能关闭 Suspend Character Movement；角色移动组件会与组装刚体冲突。");
		return false;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!bCharacterMovementSuspended || !IsValid(Movement) || SuspendedCharacterMovement.Get() != Movement
		|| Movement->MovementMode != MOVE_None || Movement->CustomMovementMode != 0 || Movement->IsComponentTickEnabled())
	{
		OutReason = LOCTEXT("FormalMovementNotFrozen", "正式物理需要角色移动组件由 Session 接管、Movement Mode 为 None 且 Tick 关闭；物理或阶段回调不得恢复角色移动。");
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!IsValid(Capsule) || !Capsule->IsRegistered() || Capsule->GetWorld() != GetWorld())
	{
		OutReason = LOCTEXT("FormalCapsuleNotReady", "正式物理需要有效、已注册且属于当前世界的角色胶囊。");
		return false;
	}
	const bool bCapsuleIsRealBody = Snapshot.CoreBody == Capsule || Snapshot.BonePoses.ContainsByPredicate(
		[Capsule](const FAssemblyPhysicsBonePose& Pose) { return Pose.Body == Capsule || Pose.PoseSource == Capsule; });
	if (!bDisableCharacterCapsuleForPhysics)
	{
		OutReason = LOCTEXT("FormalCapsuleProtectionDisabled", "正式物理不能关闭 Disable Character Capsule For Physics；只有明确登记为核心或组装刚体的胶囊可保留碰撞。");
		return false;
	}
	if (bCapsuleIsRealBody) { return true; }
	if (Capsule->IsSimulatingPhysics() || Capsule->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
	{
		OutReason = LOCTEXT("FormalProxyCapsuleConflict", "角色代理胶囊必须关闭碰撞且不模拟物理；请登记真实核心/零件刚体，并由 Session 保护角色代理。");
		return false;
	}
	return true;
}

bool UAssemblySessionComponent::EnsurePhysicsProxyReady(const UAssemblyComponent* Model,
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!HasPhysicsSessionClaim(Model) || Snapshot.Context.Assembly != Model || Snapshot.Context.World != GetWorld())
	{
		OutReason = LOCTEXT("PhysicsSessionNotClaimed", "正式物理需要由当前本地角色的 Assembly Session 接管；会话、模型或世界已失效。");
		return false;
	}
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) { return true; }
	if (!bSuspendCharacterMovement)
	{
		OutReason = LOCTEXT("FormalMovementProtectionDisabled", "正式物理不能关闭 Suspend Character Movement；角色移动组件会与组装刚体冲突。");
		return false;
	}
	if (!bDisableCharacterCapsuleForPhysics)
	{
		OutReason = LOCTEXT("FormalCapsuleProtectionDisabled", "正式物理不能关闭 Disable Character Capsule For Physics；只有明确登记为核心或组装刚体的胶囊可保留碰撞。");
		return false;
	}
	SuspendCharacterMovement();
	if (!HasPhysicsSessionClaim(Model))
	{
		OutReason = LOCTEXT("PhysicsProxyFreezeCanceled", "冻结角色移动期间会话已关闭或控制权已变化。");
		return false;
	}
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!IsValid(Capsule) || !Capsule->IsRegistered() || Capsule->GetWorld() != GetWorld())
	{
		OutReason = LOCTEXT("FormalCapsuleNotReady", "正式物理需要有效、已注册且属于当前世界的角色胶囊。");
		return false;
	}
	const bool bCapsuleIsRealBody = Snapshot.CoreBody == Capsule || Snapshot.BonePoses.ContainsByPredicate(
		[Capsule](const FAssemblyPhysicsBonePose& Pose) { return Pose.Body == Capsule || Pose.PoseSource == Capsule; });
	if (bCapsuleIsRealBody)
	{
		// A provider may adopt the capsule during Prepare. Return collision that we suppressed as a proxy.
		RestoreCharacterCapsule();
	}
	else
	{
		if (Capsule->IsSimulatingPhysics())
		{
			OutReason = LOCTEXT("UnclaimedCapsuleSimulation", "角色胶囊正在模拟物理且未登记为真实组装刚体；请先由物理程序明确接管。");
			return false;
		}
		if (DisabledCharacterCapsule.Get() != Capsule) { RestoreCharacterCapsule(); }
		if (!HasPhysicsSessionClaim(Model))
		{
			OutReason = LOCTEXT("PhysicsProxyFreezeCanceled", "冻结角色移动期间会话已关闭或控制权已变化。");
			return false;
		}
		if (!bCharacterCapsuleDisabled)
		{
			DisabledCharacterCapsule = Capsule;
			PreviousCharacterCapsuleCollision = Capsule->GetCollisionEnabled();
			// Claim before SetCollisionEnabled: overlap callbacks can shut down this session.
			bCharacterCapsuleDisabled = true;
		}
		if (Capsule->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	return ValidatePhysicsProxyReady(Model, Snapshot, OutReason);
}
APlayerController* UAssemblySessionComponent::GetPlayerController() const { return BoundController.Get(); }
UAssemblyHUDWidget* UAssemblySessionComponent::GetHUD() const { return IsValid(HUD) ? HUD.Get() : nullptr; }

void UAssemblySessionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->FindComponentByClass<UJGDLevelRatingComponent>())
	{
		UJGDLevelRatingComponent* Rating = NewObject<UJGDLevelRatingComponent>(GetOwner());
		GetOwner()->AddInstanceComponent(Rating);
		Rating->RegisterComponent();
	}
	if (!GetOwner()->FindComponentByClass<UAssemblyPlayerExperienceComponent>())
	{
		UAssemblyPlayerExperienceComponent* Experience = NewObject<UAssemblyPlayerExperienceComponent>(GetOwner());
		GetOwner()->AddInstanceComponent(Experience);
		Experience->RegisterComponent();
	}
	if (!GetOwner()->FindComponentByClass<UAssemblyCameraBackgroundComponent>())
	{
		UAssemblyCameraBackgroundComponent* Background = NewObject<UAssemblyCameraBackgroundComponent>(GetOwner());
		GetOwner()->AddInstanceComponent(Background);
		Background->RegisterComponent();
	}
	// Actor Blueprint BeginPlay may still configure the frame/model after component BeginPlay.
}

bool UAssemblySessionComponent::InitializeSession(APlayerController* PC, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (bInitializing || bReleasing || bTransitioning || bEndingPlay)
	{
		OutReason = LOCTEXT("SessionBusy", "组装会话正在初始化或关闭，请稍后重试。");
		return false;
	}
	if (!TryRestoreCharacterProxy())
	{
		OutReason = LOCTEXT("PreviousPhysicsStopping", "上一组装会话的物理执行器仍在回调或运行，请完成物理关闭后再初始化。");
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
		&& BoundInteraction.Get() == Interaction && BoundWorkbench.IsValid() && IsValid(SessionInput)
		&& IsCurrentSessionBinding(Model, Interaction, BoundWorkbench.Get(), PC))
	{
		bAutoInitializationSuppressed = false;
		return EnsureHUD(OutReason);
	}
	TArray<UAssemblySessionComponent*> Sessions;
	Pawn->GetComponents(Sessions);
	for (const UAssemblySessionComponent* Other : Sessions)
	{
		if (IsValid(Other) && Other != this && (Other->bInitialized || Other->bInitializing || Other->bProxyRestorePending))
		{
			OutReason = LOCTEXT("DuplicateSession", "同一角色已有活动的组装会话，请只保留一个 Session 组件。");
			return false;
		}
	}
	ReleaseSession();
	if (!TryRestoreCharacterProxy())
	{
		OutReason = LOCTEXT("ReleasedPhysicsStopping", "旧物理执行器尚未关闭，角色代理保持冻结，请在同步回调结束后重试。");
		return false;
	}
	TGuardValue<bool> InitializingGuard(bInitializing, true);
	bAutoInitializationSuppressed = false;
	// Keep cleanup targets before callbacks can cancel initialization. No input or HUD is active yet.
	BoundAssembly = Model;
	BoundInteraction = Interaction;
	BoundController = PC;
	auto CapturePhysicsBridge = [this, Model, Interaction]()
	{
		if (!IsValid(Model) || !IsValid(Interaction)) { return; }
		UAssemblyPhysicsBridgeComponent* Bridge = Interaction->GetPhysicsBridge();
		if (IsValid(Bridge) && (!Bridge->IsBridgeInitialized() || Bridge->GetPhysicsContext().Assembly == Model))
		{
			BoundPhysicsBridge = Bridge;
			bHadPhysicsBridgeBinding = true;
		}
	};
	CapturePhysicsBridge();
	auto InitializationStillValid = [this, Pawn, PC, Model, Interaction]()
	{
		return bInitializing && !bEndingPlay && !bAutoInitializationSuppressed && !bReleasing
			&& IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && IsValid(PC) && PC->GetPawn() == Pawn
			&& Pawn->GetController() == PC && IsValid(Model) && IsValid(Interaction)
			&& BoundAssembly.Get() == Model && BoundInteraction.Get() == Interaction && BoundController.Get() == PC;
	};
	// Bridge initialization can immediately Prepare/Apply even for a model already in Playing.
	// Suspend locomotion first; capsule ownership waits until the bridge supplies its actual snapshot.
	SuspendCharacterMovement();
	if (!InitializationStillValid())
	{
		OutReason = LOCTEXT("EarlyProxyInitializationCanceled", "组装会话在冻结角色移动时已被关闭或更换。");
		ReleaseSession();
		return false;
	}
	// An already configured round survives controller changes and repeated initialization.
	if (!Model->IsInitialized() && !Model->EnterAssembly(OutReason)) { ReleaseSession(); return false; }
	Model->ApplyInitialGameplayScale(GetDefault<UAssemblyExperienceSettings>()->PlayerSizeScale);
	if (!InitializationStillValid())
	{
		OutReason = LOCTEXT("ModelInitializationCanceled", "组装会话在模型初始化期间已被关闭或更换。");
		ReleaseSession();
		return false;
	}
	const bool bInteractionInitialized = Interaction->InitializeInteraction(Model, OutReason);
	// A provider can create the bridge and cancel this session within the same synchronous call.
	CapturePhysicsBridge();
	if (!bInteractionInitialized) { ReleaseSession(); return false; }
	if (!InitializationStillValid())
	{
		OutReason = LOCTEXT("InteractionInitializationCanceled", "组装会话在展示初始化期间已被关闭或更换。");
		ReleaseSession();
		return false;
	}
	UAssemblyWorkbenchComponent* Workbench = Pawn->FindComponentByClass<UAssemblyWorkbenchComponent>();
	if (!Workbench)
	{
		Workbench = NewObject<UAssemblyWorkbenchComponent>(Pawn, NAME_None, RF_Transient);
		Pawn->AddInstanceComponent(Workbench);
		BoundWorkbench = Workbench;
		Workbench->RegisterComponent();
	}
	if (!InitializationStillValid() || !IsValid(Workbench))
	{
		OutReason = LOCTEXT("WorkbenchRegistrationCanceled", "组装会话在工作台注册期间已被关闭或更换。");
		ReleaseSession();
		return false;
	}
	BoundWorkbench = Workbench;
	const bool bWorkbenchInitialized = Workbench->InitializeWorkbench(Model, Interaction, PC, OutReason);
	if (IsValid(Workbench)) { BoundWorkbench = Workbench; }
	if (!bWorkbenchInitialized) { ReleaseSession(); return false; }
	if (!InitializationStillValid() || !IsValid(Workbench))
	{
		OutReason = LOCTEXT("WorkbenchInitializationCanceled", "组装会话在工作台初始化期间已被关闭或更换。");
		ReleaseSession();
		return false;
	}
	BoundAssembly = Model;
	BoundInteraction = Interaction;
	BoundPhysicsBridge = Interaction->GetPhysicsBridge();
	bHadPhysicsBridgeBinding = BoundPhysicsBridge.IsValid();
	BoundWorkbench = Workbench;
	BoundController = PC;
	Model->OnAssemblyChanged.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleModelChanged);
	Model->OnAssemblyReset.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleModelReset);
	Model->OnPhaseChanged.AddUniqueDynamic(this, &UAssemblySessionComponent::HandlePhaseChanged);
	Workbench->OnWorkbenchChanged.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleWorkbenchChanged);
	Workbench->OnMessage.AddUniqueDynamic(this, &UAssemblySessionComponent::HandleWorkbenchMessage);
	if (!BindSessionInput(OutReason)) { ReleaseSession(); return false; }
	bInitialized = true;
	DeactivationHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &UAssemblySessionComponent::HandleApplicationDeactivated);
	BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UAssemblySessionComponent::HandleApplicationDeactivated);
	if (!EnsureHUD(OutReason)) { ReleaseSession(); return false; }
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("InitializationCanceled", "组装会话在初始化期间已被关闭。");
		ReleaseSession();
		return false;
	}
	// The character proxy must be suspended before an adapter enables real assembly simulation.
	UpdateMovementGate();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("ProxyInitializationCanceled", "组装会话在角色代理初始化期间已被关闭。");
		ReleaseSession();
		return false;
	}
	if (UAssemblyPhysicsBridgeComponent* Bridge = BoundPhysicsBridge.Get())
	{
		if (!Bridge->RefreshPhysicsLifecycle(OutReason)) { ReleaseSession(); return false; }
	}
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("PhysicsInitializationCanceled", "组装会话在物理阶段初始化期间已被关闭。");
		ReleaseSession();
		return false;
	}
	UpdateMovementGate();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("PhysicsProxyInitializationCanceled", "组装会话在物理代理初始化期间已被关闭。");
		ReleaseSession();
		return false;
	}
	bViewportWasFocused = IsSessionViewportFocused();
	bModelRefreshPending = false;
	LastInitializationFailure = FText::GetEmpty();
	OnSessionChanged.Broadcast();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("InitializationNotificationCanceled", "组装会话在初始化通知期间已被关闭或更换。");
		ReleaseSession();
		return false;
	}
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
	const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UJGDLoadingScreenSubsystem* Loading = Instance ? Instance->GetSubsystem<UJGDLoadingScreenSubsystem>() : nullptr;
	if (Loading && Loading->IsLoadingScreenVisible())
	{
		OutReason = LOCTEXT("LoadingMap", "正在加载关卡，请稍候。");
		return false;
	}
	if (bTransitioning || bInitializing || bReleasing)
	{
		OutReason = LOCTEXT("SessionTransitionBusy", "组装会话正在切换阶段、初始化或关闭，请稍后重试。");
		return false;
	}
	if (!IsCurrentSessionBinding(BoundAssembly.Get(), BoundInteraction.Get(),
		BoundWorkbench.Get(), BoundController.Get()))
	{
		OutReason = LOCTEXT("SessionUnavailable", "请先初始化当前本地角色的组装会话。");
		return false;
	}
	return true;
}

bool UAssemblySessionComponent::IsCurrentSessionBinding(const UAssemblyComponent* Model,
	const UAssemblyInteractionComponent* Interaction, const UAssemblyWorkbenchComponent* Workbench,
	const APlayerController* PC) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return bInitialized && !bReleasing && !bAutoInitializationSuppressed && IsValid(Model)
		&& IsValid(Interaction) && IsValid(Workbench) && IsValid(PC) && IsValid(Pawn)
		&& Model->IsInitialized() && Interaction->IsInteractionInitialized() && Workbench->IsWorkbenchInitialized()
		&& Interaction->GetAssemblyComponent() == Model && Workbench->GetAssemblyComponent() == Model
		&& Workbench->GetInteractionComponent() == Interaction && Workbench->GetPlayerController() == PC
		&& (!bHadPhysicsBridgeBinding || (BoundPhysicsBridge.IsValid() && BoundPhysicsBridge->IsBridgeInitialized()
			&& BoundPhysicsBridge->GetPhysicsContext().Assembly == Model && BoundPhysicsBridge->GetPhysicsContext().World == GetWorld()))
		&& BoundPhysicsBridge.Get() == Interaction->GetPhysicsBridge()
		&& !Pawn->IsActorBeingDestroyed() && BoundAssembly.Get() == Model && BoundInteraction.Get() == Interaction
		&& BoundWorkbench.Get() == Workbench && BoundController.Get() == PC && PC->IsLocalController()
		&& PC->GetPawn() == Pawn && Pawn->GetController() == PC;
}

bool UAssemblySessionComponent::StartPlaying(FText& OutReason)
{
	if (!CanUseSession(OutReason))
	{
		if (!bTransitioning && !bInitializing && !bReleasing) { ShowMessage(OutReason); }
		return false;
	}
	TGuardValue<bool> TransitionGuard(bTransitioning, true);
	UAssemblyComponent* Model = BoundAssembly.Get();
	UAssemblyInteractionComponent* Interaction = BoundInteraction.Get();
	UAssemblyWorkbenchComponent* Workbench = BoundWorkbench.Get();
	APlayerController* PC = BoundController.Get();
	const TWeakObjectPtr<UAssemblyPhysicsBridgeComponent> StartedBridge = BoundPhysicsBridge;
	auto RecoverFailedStart = [&]()
	{
		const FText Failure = OutReason;
		HeldMuscleKeys.Reset();
		bSpaceHeld = false;
		bReplayHeldMusclesPending = false;
		// The model has finalized this activation. Stop it through Shutdown, never fake a second Abort.
		// Keep the original model/bridge even if a notification closed and cleared the Session binding.
		if (UAssemblyPhysicsBridgeComponent* Bridge = StartedBridge.Get()) { Bridge->ShutdownPhysicsLifecycle(); }
		if (IsValid(Model) && Model->IsInitialized() && Model->GetPhase() == EAssemblyPhase::Playing)
		{
			FText RecoveryReason;
			Model->RecoverAssemblyAfterPhysicsFailure(RecoveryReason);
		}
		OutReason = Failure;
		// Returning the reason avoids another user callback restarting the failed physical lifecycle.
		return false;
	};
	auto VerifyStartedPhysics = [&]()
	{
		if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC) || Model->GetPhase() != EAssemblyPhase::Playing)
		{
			OutReason = LOCTEXT("StartNotificationCanceled", "组装会话在开始操控通知期间已被关闭、更换或退出操控。");
			return false;
		}
		UAssemblyPhysicsBridgeComponent* Bridge = StartedBridge.Get();
		return !Bridge || Bridge->ValidateCommittedPlaying(OutReason);
	};
	Workbench->CancelTool();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("StartCanceled", "组装会话在开始操控前已被关闭或更换。");
		return false;
	}
	// The model's gate prepares physics before changing Phase, including direct model calls.
	// Keep the selection so a chosen breakable joint remains controllable while playing.
	if (!Model->TryStartPlaying(OutReason)) { ShowMessage(OutReason); return false; }
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("StartPhysicsCanceled", "组装会话在开始操控期间已被关闭或更换。");
		return RecoverFailedStart();
	}
	UpdateMovementGate();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("StartProxyCanceled", "组装会话在切换角色代理时已被关闭。");
		return RecoverFailedStart();
	}
	if (!VerifyStartedPhysics()) { return RecoverFailedStart(); }
	// Replay already-held muscle intents after phase application while rejecting external input reentry.
	ApplyHeldMuscleInput(true);
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("StartInputCanceled", "组装会话在发送操控输入时已被关闭。");
		return RecoverFailedStart();
	}
	if (!VerifyStartedPhysics()) { return RecoverFailedStart(); }
	bReplayHeldMusclesPending = false;
	ShowMessage(FText::GetEmpty());
	if (!VerifyStartedPhysics()) { return RecoverFailedStart(); }
	OnSessionChanged.Broadcast();
	if (!VerifyStartedPhysics()) { return RecoverFailedStart(); }
	Model->CaptureBuildCheckpoint();
	return true;
}

bool UAssemblySessionComponent::RestartSession(FText& OutReason)
{
	if (!CanUseSession(OutReason))
	{
		if (!bTransitioning && !bInitializing && !bReleasing) { ShowMessage(OutReason); }
		return false;
	}
	TGuardValue<bool> TransitionGuard(bTransitioning, true);
	UAssemblyComponent* Model = BoundAssembly.Get();
	UAssemblyInteractionComponent* Interaction = BoundInteraction.Get();
	UAssemblyWorkbenchComponent* Workbench = BoundWorkbench.Get();
	APlayerController* PC = BoundController.Get();
	const bool bClearBuild = Model->GetPhase() == EAssemblyPhase::Assembly;
	ClearHeldInput();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("RestartInputCanceled", "组装会话在停止操控输入时已被关闭。");
		return false;
	}
	Workbench->CancelTool();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("RestartCanceled", "组装会话在重新组装前已被关闭或更换。");
		return false;
	}
	// StopBeforeReset is enforced by the model before pose sources/data/views are cleared.
	const bool bRestarted = bClearBuild ? Model->RestartAssembly(OutReason) : Model->ReturnToAssemblyKeepingBuild(OutReason);
	if (!bRestarted) { ShowMessage(OutReason); return false; }
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("RestartPhysicsCanceled", "组装会话在重新组装期间已被关闭或更换。");
		return false;
	}
	Workbench->ClearSelection();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("RestartSelectionCanceled", "组装会话在清除选择期间已被关闭。");
		return false;
	}
	// Interaction owns safe spawn-pose restoration; physical bodies/constraints remain external.
	if (UAssemblyPhysicsBridgeComponent* Bridge = BoundPhysicsBridge.Get()) { Bridge->RequestPhysicsRebuild(); }
	UpdateMovementGate();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("RestartProxyCanceled", "组装会话在恢复组装角色代理时已被关闭。");
		return false;
	}
	ShowMessage(FText::GetEmpty());
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("RestartMessageCanceled", "组装会话在重新组装通知期间已被关闭或更换。");
		return false;
	}
	OnSessionChanged.Broadcast();
	if (!IsCurrentSessionBinding(Model, Interaction, Workbench, PC))
	{
		OutReason = LOCTEXT("RestartNotificationCanceled", "组装会话在重新组装通知期间已被关闭或更换。");
		return false;
	}
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
	// WASD belongs only to muscles. A controller must not drive the character proxy in either phase.
	const bool bShouldIgnore = bInitialized && BoundAssembly.IsValid();
	if (bShouldIgnore != bIgnoreMoveInputAdded)
	{
		// SetIgnoreMoveInput uses a count. Release only the count added by this session.
		BoundController->SetIgnoreMoveInput(bShouldIgnore);
		bIgnoreMoveInputAdded = bShouldIgnore;
	}
	UpdateCharacterProxyState();
}

void UAssemblySessionComponent::SuspendCharacterMovement()
{
	// DisableMovement may notify Blueprint code that reenters this function. One attempt is enough;
	// the caller verifies the resulting state instead of recursively fighting an external owner.
	if (bSuspendingCharacterMovement) { return; }
	TGuardValue<bool> SuspendingGuard(bSuspendingCharacterMovement, true);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	auto CanSuspend = [this, Character]()
	{
		return (bInitialized || bInitializing) && !bEndingPlay && !bReleasing && !bAutoInitializationSuppressed
			&& !bProxyRestorePending && IsValid(Character) && BoundAssembly.IsValid() && BoundInteraction.IsValid()
			&& BoundController.IsValid() && BoundController->GetPawn() == Character && Character->GetController() == BoundController.Get();
	};
	if (!CanSuspend()) { return; }
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	UAssemblyPhysicsBridgeComponent* Bridge = BoundPhysicsBridge.Get();
	const bool bFormalPhysics = IsValid(Bridge) && Bridge->IsBridgeInitialized()
		&& (Bridge->HasPhysicsLifecycleProvider() || Bridge->bRequirePhysicsLifecycleProvider);
	// Changing a protection flag cannot restore the proxy beneath an active physical owner.
	// The formal entry will reject the flag and shutdown remains responsible for restoration.
	if (!bSuspendCharacterMovement && bFormalPhysics) { return; }
	if (!bSuspendCharacterMovement || SuspendedCharacterMovement.Get() != Movement)
	{
		RestoreCharacterMovement();
	}
	if (!CanSuspend()) { return; }
	if (bSuspendCharacterMovement && IsValid(Movement))
	{
		if (!bCharacterMovementSuspended)
		{
			SuspendedCharacterMovement = Movement;
			PreviousCharacterMovementMode = static_cast<uint8>(Movement->MovementMode);
			PreviousCharacterCustomMovementMode = Movement->CustomMovementMode;
			bPreviousCharacterMovementTick = Movement->IsComponentTickEnabled();
			// Preserve the initial state across later freezes; claim before callbacks can release it.
			bCharacterMovementSuspended = true;
		}
		if (Movement->MovementMode != MOVE_None || Movement->CustomMovementMode != 0 || Movement->IsComponentTickEnabled())
		{
			Character->StopJumping();
			Character->ConsumeMovementInputVector();
			Movement->StopMovementImmediately();
			Movement->ClearAccumulatedForces();
			Movement->SetComponentTickEnabled(false);
			Movement->DisableMovement();
		}
	}
	// Capsule collision requires the actual physics snapshot, which may not exist during early initialization.
}

void UAssemblySessionComponent::UpdateCharacterProxyState()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!bInitialized || bReleasing || !IsValid(Character)) { return; }
	UAssemblyPhysicsBridgeComponent* Bridge = BoundPhysicsBridge.Get();
	FAssemblyPhysicsSnapshotMessage Snapshot;
	if (IsValid(Bridge) && Bridge->IsBridgeInitialized()
		&& (Bridge->HasPhysicsLifecycleProvider() || Bridge->bRequirePhysicsLifecycleProvider))
	{
		// Never restore a previously claimed proxy just because a formal binding/snapshot is failing.
		// Prepare/Commit propagate the precise reason; shutdown releases the physical owner first.
		if (Bridge->GetPhysicsSnapshot(Snapshot))
		{
			FText Reason;
			EnsurePhysicsProxyReady(BoundAssembly.Get(), Snapshot, Reason);
		}
		return;
	}
	SuspendCharacterMovement();
	if (!bInitialized || bReleasing) { return; }
	RestoreCharacterCapsule();
}

void UAssemblySessionComponent::RestoreCharacterMovement()
{
	UCharacterMovementComponent* Movement = SuspendedCharacterMovement.Get();
	const bool bRestore = bCharacterMovementSuspended && IsValid(Movement);
	// Clear ownership before any callback can attempt another release.
	bCharacterMovementSuspended = false;
	SuspendedCharacterMovement.Reset();
	if (!bRestore) { return; }
	const bool bOwnsMode = Movement->MovementMode == MOVE_None && Movement->CustomMovementMode == 0;
	const bool bOwnsTick = !Movement->IsComponentTickEnabled();
	if (bOwnsMode && bOwnsTick)
	{
		if (APawn* Pawn = Cast<APawn>(GetOwner())) { Pawn->ConsumeMovementInputVector(); }
		Movement->StopMovementImmediately();
		Movement->ClearAccumulatedForces();
	}
	// Do not restore cached velocity or overwrite a mode/tick state another owner has replaced.
	if (bOwnsTick) { Movement->SetComponentTickEnabled(bPreviousCharacterMovementTick); }
	if (bOwnsMode)
	{
		Movement->SetMovementMode(static_cast<EMovementMode>(PreviousCharacterMovementMode), PreviousCharacterCustomMovementMode);
	}
}

void UAssemblySessionComponent::RestoreCharacterCapsule()
{
	UCapsuleComponent* Capsule = DisabledCharacterCapsule.Get();
	const bool bRestore = bCharacterCapsuleDisabled && IsValid(Capsule)
		&& Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
	bCharacterCapsuleDisabled = false;
	DisabledCharacterCapsule.Reset();
	if (bRestore) { Capsule->SetCollisionEnabled(PreviousCharacterCapsuleCollision); }
}

bool UAssemblySessionComponent::TryRestoreCharacterProxy()
{
	if (!bProxyRestorePending) { return true; }
	if (const UAssemblyPhysicsBridgeComponent* Bridge = PendingProxyShutdownBridge.Get())
	{
		// A shutdown requested inside a provider callback is deferred until that callback unwinds.
		// An externally resumed bridge still owns physical movement; never restore its character proxy.
		if (Bridge->IsPhysicsLifecycleBusy() || !Bridge->IsPhysicsLifecycleShutdown()) { return false; }
	}
	TGuardValue<bool> RestoringGuard(bReleasing, true);
	bProxyRestorePending = false;
	PendingProxyShutdownBridge.Reset();
	RestoreCharacterCapsule();
	RestoreCharacterMovement();
	return true;
}

void UAssemblySessionComponent::ApplyHeldMuscleInput(bool bAllowTransition)
{
	if (!CanAcceptInput(bAllowTransition) || BoundAssembly->GetPhase() != EAssemblyPhase::Playing) { return; }
	UAssemblyComponent* Model = BoundAssembly.Get();
	const TArray<EAssemblyMuscleKey> Keys = HeldMuscleKeys.Array();
	for (const EAssemblyMuscleKey Key : Keys)
	{
		if (!CanAcceptInput(bAllowTransition) || BoundAssembly.Get() != Model || Model->GetPhase() != EAssemblyPhase::Playing) { break; }
		if (HeldMuscleKeys.Contains(Key)) { Model->SetMuscleInput(Key, true); }
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

bool UAssemblySessionComponent::CanAcceptInput(bool bAllowTransition) const
{
	const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UJGDLoadingScreenSubsystem* Loading = Instance ? Instance->GetSubsystem<UJGDLoadingScreenSubsystem>() : nullptr;
	if (Loading && Loading->IsLoadingScreenVisible()) return false;
	const UAssemblyPlayerExperienceComponent* Experience = GetOwner()->FindComponentByClass<UAssemblyPlayerExperienceComponent>();
	if (Experience && Experience->IsRestartTransitionActive()) return false;
	return !bInitializing && (!bTransitioning || bAllowTransition)
		&& IsCurrentSessionBinding(BoundAssembly.Get(), BoundInteraction.Get(), BoundWorkbench.Get(), BoundController.Get())
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
	UJGDInputFeedbackLibrary::PlayInputPress(this);
	if (BoundAssembly->GetPhase() == EAssemblyPhase::Playing) { BoundAssembly->SetMuscleInput(Key, true); }
	else if (BoundWorkbench.IsValid())
	{
		FText Reason;
		BoundWorkbench->SelectBindingKey(Key, Reason);
	}
}

void UAssemblySessionComponent::HandleMuscleReleased(EAssemblyMuscleKey Key)
{
	if (HeldMuscleKeys.Contains(Key) && CanAcceptInput()) UJGDInputFeedbackLibrary::PlayInputRelease(this);
	HeldMuscleKeys.Remove(Key);
	if (BoundAssembly.IsValid()) { BoundAssembly->SetMuscleInput(Key, false); }
}

void UAssemblySessionComponent::HandleSpacePressed()
{
	if (!CanAcceptInput() || bSpaceHeld || BoundAssembly->GetPhase() != EAssemblyPhase::Playing) { return; }
	UAssemblyComponent* Model = BoundAssembly.Get();
	const bool bAccepted = Model->SetCoreRotationInput(true);
	// A synchronous drive listener may release input or close the Session before the call returns.
	bSpaceHeld = bAccepted && BoundAssembly.Get() == Model
		&& IsCurrentSessionBinding(Model, BoundInteraction.Get(), BoundWorkbench.Get(), BoundController.Get())
		&& Model->GetPhase() == EAssemblyPhase::Playing && Model->IsCoreRotationHeld();
	if (bSpaceHeld) UJGDInputFeedbackLibrary::PlayInputPress(this);
}

void UAssemblySessionComponent::HandleSpaceReleased()
{
	if (bSpaceHeld && CanAcceptInput()) UJGDInputFeedbackLibrary::PlayInputRelease(this);
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
	// Maintain proxy ownership without mutating the notifying model; physics phases belong to the provider.
	UpdateMovementGate();
	if (Phase != EAssemblyPhase::Playing) { bSpaceHeld = false; }
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
	// Shutdown sessions still tick: return the proxy only after their old physical owner has stopped.
	if (!bEndingPlay) { TryRestoreCharacterProxy(); }
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* DesiredController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (DesiredController && (!DesiredController->IsLocalController() || DesiredController->GetPawn() != Pawn)) { DesiredController = nullptr; }
	if (bInitialized && (BoundController.Get() != DesiredController || !IsValid(SessionInput)
		|| !IsCurrentSessionBinding(BoundAssembly.Get(), BoundInteraction.Get(), BoundWorkbench.Get(), BoundController.Get())))
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
	TWeakObjectPtr<UAssemblyPhysicsBridgeComponent> ReleasedBridge = BoundPhysicsBridge;
	if (!ReleasedBridge.IsValid() && bInitializing && BoundInteraction.IsValid())
	{
		// InitializeInteraction may be inside a newly created bridge's synchronous provider callback.
		UAssemblyPhysicsBridgeComponent* Candidate = BoundInteraction->GetPhysicsBridge();
		if (IsValid(Candidate) && (!Candidate->IsBridgeInitialized()
			|| Candidate->GetPhysicsContext().Assembly == BoundAssembly.Get()))
		{
			ReleasedBridge = Candidate;
		}
	}
	// Stop real bodies/constraints before restoring the character proxy, even when views are caller-owned.
	if (UAssemblyPhysicsBridgeComponent* Bridge = ReleasedBridge.Get()) { Bridge->ShutdownPhysicsLifecycle(); }
	FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivationHandle);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
	DeactivationHandle.Reset();
	BackgroundHandle.Reset();
	DetachHUD();
	UnbindSessionInput();
	if (bIgnoreMoveInputAdded && BoundController.IsValid()) { BoundController->SetIgnoreMoveInput(false); }
	bIgnoreMoveInputAdded = false;
	if ((bCharacterMovementSuspended || bCharacterCapsuleDisabled) && !bProxyRestorePending)
	{
		PendingProxyShutdownBridge = ReleasedBridge;
		bProxyRestorePending = true;
	}
	TryRestoreCharacterProxy();
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
	BoundPhysicsBridge.Reset();
	bHadPhysicsBridgeBinding = false;
	BoundAssembly.Reset();
	BoundController.Reset();
	bInitialized = false;
	bViewportWasFocused = false;
	bModelRefreshPending = false;
	bReplayHeldMusclesPending = false;
}

void UAssemblySessionComponent::ShutdownSession()
{
	const bool bNotify = !bAutoInitializationSuppressed || bInitialized;
	bAutoInitializationSuppressed = true;
	if (bReleasing) { return; }
	ReleaseSession();
	if (bNotify) { OnSessionChanged.Broadcast(); }
}

void UAssemblySessionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ReleaseSession();
	Super::EndPlay(EndPlayReason);
}

void UAssemblySessionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true;
	ReleaseSession();
	HUD = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#undef LOCTEXT_NAMESPACE
