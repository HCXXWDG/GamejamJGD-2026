#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLifecycleTestProvider.h"
#include "AssemblyPhysicsTestReceiver.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "MessageBus/JGDMessageSubsystem.h"

namespace
{
	constexpr EAutomationTestFlags SessionProxyFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** Official local-player/component APIs only. There is no viewport, Slate interaction or PIE. */
	struct FSessionProxyWorld
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		ULocalPlayer* LocalPlayer = nullptr;
		APlayerController* Controller = nullptr;
		ACharacter* Character = nullptr;
		UAssemblyComponent* Model = nullptr;
		UAssemblyInteractionComponent* Interaction = nullptr;
		UAssemblySessionComponent* Session = nullptr;
		UAssemblyPhysicsBridgeComponent* Bridge = nullptr;
		UAssemblyLifecycleTestProvider* Provider = nullptr;
		UPrimitiveComponent* Core = nullptr;
		bool bReady = false;
		FText Reason;
		ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::NoCollision;

		explicit FSessionProxyWorld(bool bWithProvider = false, bool bCapsuleIsCore = false)
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone(FName(*FString::Printf(TEXT("AssemblySessionProxy_%s"), *FGuid::NewGuid().ToString())));
			World = GameInstance->GetWorld();
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			if (!World->SetGameMode(URL)) { Reason = FText::FromString(TEXT("Cannot create native GameMode for lifecycle fixture.")); return; }
			World->InitializeActorsForPlay(URL);
			Character = World->SpawnActor<ACharacter>();
			Model = NewObject<UAssemblyComponent>(Character);
			Model->bAutoEnterAssembly = false;
			Character->AddInstanceComponent(Model);
			Model->RegisterComponent();
			if (!Model->ConfigureFreeAssembly(FAssemblyFreeSettings(), { FAssemblyBoneDefinition() }, { FAssemblyMuscleDefinition() }, Reason)) { return; }
			InitialCapsuleCollision = Character->GetCapsuleComponent()->GetCollisionEnabled();
			if (bCapsuleIsCore)
			{
				Core = Character->GetCapsuleComponent();
			}
			else
			{
				UBoxComponent* SeparateCore = NewObject<UBoxComponent>(Character);
				Character->AddInstanceComponent(SeparateCore);
				SeparateCore->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				SeparateCore->RegisterComponent();
				Core = SeparateCore;
			}
			Bridge = NewObject<UAssemblyPhysicsBridgeComponent>(Character);
			Character->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			if (bWithProvider)
			{
				Provider = NewObject<UAssemblyLifecycleTestProvider>(Character);
				Character->AddInstanceComponent(Provider);
				Provider->Model = Model;
				Provider->Bridge = Bridge;
				Provider->RegisterComponent();
				if (!Bridge->SetPhysicsLifecycleProvider(Provider, Reason)) { return; }
				Bridge->bRequirePhysicsLifecycleProvider = true;
			}
			Interaction = NewObject<UAssemblyInteractionComponent>(Character);
			Interaction->ExistingCoreBody = Core;
			Interaction->bCreateCorePlaceholder = false;
			Character->AddInstanceComponent(Interaction);
			Interaction->RegisterComponent();
			Session = NewObject<UAssemblySessionComponent>(Character);
			Session->bAutoInitialize = false;
			Session->bCreateHUD = false;
			Session->bConfigureHUDInputMode = false;
			Character->AddInstanceComponent(Session);
			Session->RegisterComponent();
			if (Provider) { Provider->Session = Session; }
			Controller = World->SpawnActor<APlayerController>();
			// PlayerAdded supports a null viewport and initializes the actual local-player subsystems.
			// CreateLocalPlayer/SpawnPlayActor are deliberately not used: both require a viewport.
			LocalPlayer = NewObject<ULocalPlayer>(GEngine);
			LocalPlayer->PlayerAdded(nullptr, 0);
			Controller->SetPlayer(LocalPlayer);
			if (!Cast<UEnhancedPlayerInput>(Controller->PlayerInput))
			{
				Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
			}
			Controller->Possess(Character);
			World->BeginPlay();
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Character->GetCharacterMovement()->SetComponentTickEnabled(true);
			bReady = Character->HasActorBegunPlay() && Controller->IsLocalController()
				&& Controller->GetPawn() == Character && Character->GetController() == Controller
				&& LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() != nullptr;
			if (!bReady) { Reason = FText::FromString(TEXT("Native local-player fixture is not ready without a viewport.")); }
		}

		~FSessionProxyWorld()
		{
			if (Provider) { Provider->bShutdownSessionDuringPrepare = false; }
			if (IsValid(Session)) { Session->ShutdownSession(); }
			if (IsValid(Interaction)) { Interaction->ClearInteraction(); }
			if (IsValid(Bridge)) { Bridge->ClearBridge(); }
			if (IsValid(Controller)) { Controller->UnPossess(); }
			if (World) { World->EndPlay(EEndPlayReason::Quit); }
			if (IsValid(LocalPlayer)) { LocalPlayer->PlayerRemoved(); }
			if (World) { World->DestroyWorld(false); }
			GameInstance->Shutdown();
			if (World) { GEngine->DestroyWorldContext(World); }
		}

		bool Initialize() { return bReady && Session->InitializeSession(Controller, Reason); }
		bool IsMovementFrozen() const
		{
			const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
			return Movement->MovementMode == MOVE_None && !Movement->IsComponentTickEnabled();
		}
		UAssemblyPhysicsTestReceiver* ListenMuscleDrive()
		{
			UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(Model);
			if (!Bus) { return nullptr; }
			UAssemblyPhysicsTestReceiver* Receiver = NewObject<UAssemblyPhysicsTestReceiver>(GameInstance);
			FJGDMessageReceived Delegate;
			Delegate.BindDynamic(Receiver, &UAssemblyPhysicsTestReceiver::Receive);
			Bus->RegisterListener(AssemblyMessageTags::MuscleDriveRequested, Delegate);
			return Receiver;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyMovementTest, "JGD2026.Assembly.SessionProxy.MovementAndInputOwnership", SessionProxyFlags)
bool FAssemblySessionProxyMovementTest::RunTest(const FString& Parameters)
{
	FSessionProxyWorld T;
	if (!TestTrue(TEXT("Native fixture initializes without a viewport"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	TestFalse(TEXT("Default automatic Interaction waits for the existing Session"), T.Bridge->IsBridgeInitialized());
	T.Controller->SetIgnoreMoveInput(true); // Another module's input gate.
	if (!TestTrue(TEXT("Session initializes through its public entry"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
	TestTrue(TEXT("Assembly freezes CharacterMovement"), T.IsMovementFrozen());
	TestTrue(TEXT("Assembly ignores character locomotion"), T.Controller->IsMoveInputIgnored());
	TestTrue(TEXT("Repeated initialization is valid"), T.Initialize());
	TestTrue(TEXT("Start through session"), T.Session->StartPlaying(T.Reason));
	TestTrue(TEXT("Playing also freezes CharacterMovement"), T.IsMovementFrozen());
	TestTrue(TEXT("Playing keeps WASD locomotion gated"), T.Controller->IsMoveInputIgnored());
	TestTrue(TEXT("Restart through session"), T.Session->RestartSession(T.Reason));
	TestTrue(TEXT("Returning to Assembly keeps CharacterMovement frozen"), T.IsMovementFrozen());
	T.Session->ShutdownSession();
	TestTrue(TEXT("Shutdown restores the saved movement mode"), T.Character->GetCharacterMovement()->MovementMode == MOVE_Flying);
	TestTrue(TEXT("Shutdown restores the saved movement tick"), T.Character->GetCharacterMovement()->IsComponentTickEnabled());
	TestTrue(TEXT("Shutdown preserves another owner's ignore count"), T.Controller->IsMoveInputIgnored());
	T.Controller->SetIgnoreMoveInput(false);
	TestFalse(TEXT("Removing the external count leaves no duplicate session count"), T.Controller->IsMoveInputIgnored());
	T.Session->ShutdownSession();
	TestFalse(TEXT("Repeated shutdown does not change the count"), T.Controller->IsMoveInputIgnored());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyBodiesTest, "JGD2026.Assembly.SessionProxy.RealBodyCollisionOwnership", SessionProxyFlags)
bool FAssemblySessionProxyBodiesTest::RunTest(const FString& Parameters)
{
	{
		FSessionProxyWorld T(true, false);
		if (!TestTrue(TEXT("Separate-core fixture is ready"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		TestFalse(TEXT("Automatic Interaction has not bound physics before Session"), T.Bridge->IsBridgeInitialized());
		TestEqual(TEXT("No physical phase precedes Session initialization"), T.Provider->PhaseCount, 0);
		TestTrue(TEXT("Legacy explicit Interaction initialization remains compatible"), T.Interaction->InitializeInteraction(T.Model, T.Reason));
		TestTrue(TEXT("Legacy initialization may bind the bridge"), T.Bridge->IsBridgeInitialized());
		TestEqual(TEXT("Legacy initialization still defers actual physics phase"), T.Provider->PhaseCount, 0);
		TestTrue(TEXT("Physics remains shut down until Session owns the model"), T.Bridge->IsPhysicsLifecycleShutdown());
		TestFalse(TEXT("Direct model start cannot bypass the pending Session gate"), T.Model->TryStartPlaying(T.Reason));
		TestTrue(TEXT("Premature start leaves Assembly intact"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
		TestEqual(TEXT("Premature start does not invoke the physical executor"), T.Provider->PrepareCount, 0);
		if (!TestTrue(TEXT("Separate-core session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
		TestTrue(TEXT("Session activates the previously deferred physics phase"), T.Provider->PhaseCount > 0);
		TestFalse(TEXT("Even the first physical phase waits for CMC suspension"), T.Provider->bAnyPhaseBeforeMovementSuspension);
		TestTrue(TEXT("The first executor phase already sees the disposable capsule disabled"), T.Provider->bCapsuleDisabledDuringFirstPhase);
		TestTrue(TEXT("A disposable character capsule is disabled"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("Real core collision remains enabled"), T.Core->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
		TestTrue(TEXT("Start with provider"), T.Session->StartPlaying(T.Reason));
		TestTrue(TEXT("Playing confirmation executes with proxy collision disabled"), T.Provider->bCapsuleDisabledDuringPlayingApply);
		TestTrue(TEXT("Provider Playing keeps CMC frozen"), T.IsMovementFrozen());
		T.Provider->bRefreshOnMovementRestore = true;
		T.Character->MovementModeChangedDelegate.AddDynamic(T.Provider, &UAssemblyLifecycleTestProvider::HandleMovementModeChanged);
		T.Session->ShutdownSession();
		TestEqual(TEXT("Movement restoration executes the reentry callback once"), T.Provider->MovementRestoreRefreshCount, 1);
		TestFalse(TEXT("Movement restoration callback cannot reactivate physics"), T.Provider->bRefreshAcceptedOnMovementRestore);
		TestTrue(TEXT("Physics remains shut down after movement restoration reentry"), T.Bridge->IsPhysicsLifecycleShutdown());
		TestTrue(TEXT("Physics shutdown sees movement still suspended"), T.Provider->bMovementFrozenDuringShutdown);
		TestTrue(TEXT("Physics shutdown sees proxy collision still disabled"), T.Provider->bCapsuleDisabledDuringShutdown);
		TestTrue(TEXT("Only after physics shutdown is capsule collision restored"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == T.InitialCapsuleCollision);
		TestTrue(TEXT("Only after physics shutdown is CMC restored"), T.Character->GetCharacterMovement()->MovementMode == MOVE_Flying && T.Character->GetCharacterMovement()->IsComponentTickEnabled());
	}
	{
		FSessionProxyWorld T(true, true);
		if (!TestTrue(TEXT("Capsule-core fixture is ready"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		TestFalse(TEXT("Capsule-core automatic Interaction also waits for Session"), T.Bridge->IsBridgeInitialized());
		TestEqual(TEXT("Capsule-core phase waits for Session initialization"), T.Provider->PhaseCount, 0);
		if (!TestTrue(TEXT("Capsule-as-core session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
		TestTrue(TEXT("A capsule registered as the real core retains collision"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == T.InitialCapsuleCollision);
		TestTrue(TEXT("CMC remains independently suspended"), T.IsMovementFrozen());
		TestTrue(TEXT("Start with capsule core"), T.Session->StartPlaying(T.Reason));
		TestFalse(TEXT("Executor confirmation retains registered core capsule collision"), T.Provider->bCapsuleDisabledDuringPlayingApply);
		TestTrue(TEXT("Playing never disables real core collision"), T.Core->GetCollisionEnabled() == T.InitialCapsuleCollision);
		T.Session->ShutdownSession();
		TestTrue(TEXT("Real core collision is preserved through shutdown"), T.Core->GetCollisionEnabled() == T.InitialCapsuleCollision);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyReentryTest, "JGD2026.Assembly.SessionProxy.CallbackShutdownDefersRestore", SessionProxyFlags)
bool FAssemblySessionProxyReentryTest::RunTest(const FString& Parameters)
{
	FSessionProxyWorld T(true, false);
	if (!TestTrue(TEXT("Callback fixture is ready"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	TestFalse(TEXT("Callback fixture defers default automatic Interaction"), T.Bridge->IsBridgeInitialized());
	TestEqual(TEXT("Callback fixture starts without an applied physical phase"), T.Provider->PhaseCount, 0);
	if (!TestTrue(TEXT("Session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
	T.Provider->bShutdownSessionDuringPrepare = true;
	TestFalse(TEXT("Shutdown inside provider cancels pending session start"), T.Session->StartPlaying(T.Reason));
	TestTrue(TEXT("Model remains Assembly after canceled preparation"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestTrue(TEXT("Inside Prepare the character proxy is still frozen"), T.Provider->bMovementFrozenAfterSessionShutdown);
	TestTrue(TEXT("Deferred physical shutdown sees frozen character movement"), T.Provider->bMovementFrozenDuringShutdown);
	TestTrue(TEXT("Deferred physical shutdown sees disabled disposable capsule"), T.Provider->bCapsuleDisabledDuringShutdown);
	TestEqual(TEXT("One canceled preparation aborts once"), T.Provider->AbortCount, 1);
	TestEqual(TEXT("Physical shutdown occurs once"), T.Provider->ShutdownCount, 1);
	T.World->Tick(LEVELTICK_All, 0.016f);
	TestTrue(TEXT("Post-callback tick restores saved mode"), T.Character->GetCharacterMovement()->MovementMode == MOVE_Flying);
	TestTrue(TEXT("Post-callback tick restores saved movement tick"), T.Character->GetCharacterMovement()->IsComponentTickEnabled());
	TestTrue(TEXT("Post-callback tick restores disposable capsule collision"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == T.InitialCapsuleCollision);
	TestFalse(TEXT("Session's input gate is released"), T.Controller->IsMoveInputIgnored());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyFormalOwnershipTest, "JGD2026.Assembly.SessionProxy.FormalCharacterRequiresProtectedOwnedSession", SessionProxyFlags)
bool FAssemblySessionProxyFormalOwnershipTest::RunTest(const FString& Parameters)
{
	{
		FSessionProxyWorld T(true, false);
		if (!TestTrue(TEXT("No-session fixture is ready"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		T.Session->DestroyComponent();
		T.Session = nullptr;
		T.Provider->Session = nullptr;
		T.Interaction->InitializeInteraction(T.Model, T.Reason);
		TestFalse(TEXT("A formal ACharacter cannot bypass missing session ownership"), T.Model->TryStartPlaying(T.Reason));
		TestFalse(TEXT("Missing session rejection explains the reason"), T.Reason.IsEmpty());
		TestTrue(TEXT("Missing session leaves Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
		TestEqual(TEXT("No unowned activation is sent to the executor"), T.Provider->PlayingApplyCount, 0);
	}
	for (int32 OptOut = 0; OptOut < 3; ++OptOut)
	{
		FSessionProxyWorld T(true, OptOut == 2);
		if (!TestTrue(TEXT("Protection opt-out fixture is ready"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		T.Session->bSuspendCharacterMovement = OptOut != 0;
		T.Session->bDisableCharacterCapsuleForPhysics = OptOut == 0;
		const bool bInitialized = T.Initialize();
		if (bInitialized)
		{
			TestFalse(TEXT("Formal start rejects an explicitly disabled required protection"), T.Session->StartPlaying(T.Reason));
		}
		TestFalse(TEXT("Protection rejection has a reason"), T.Reason.IsEmpty());
		TestTrue(TEXT("Protection rejection leaves the model Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
		TestEqual(TEXT("Disabled protection prevents physical activation"), T.Provider->PlayingApplyCount, 0);
		TestFalse(TEXT("Rejected entry never holds core drive"), T.Model->IsCoreRotationHeld());
		if (OptOut == 2)
		{
			TestTrue(TEXT("Real core capsule keeps its original collision despite flag rejection"), T.Core->GetCollisionEnabled() == T.InitialCapsuleCollision);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyLateVerifyTest, "JGD2026.Assembly.SessionProxy.PhaseListenersCannotUndoProxyProtection", SessionProxyFlags)
bool FAssemblySessionProxyLateVerifyTest::RunTest(const FString& Parameters)
{
	for (int32 Conflict = 0; Conflict < 2; ++Conflict)
	{
		FSessionProxyWorld T(true, false);
		if (!TestTrue(TEXT("Conflict fixture is ready"), T.bReady)
			|| !TestTrue(TEXT("Protected session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
		FGuid Bone;
		if (!TestTrue(TEXT("Install editable body before notification conflict"),
			T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
			&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96,0), T.Reason)
			&& T.Interaction->CommitBoneDrag(Bone, T.Reason))) { AddError(T.Reason.ToString()); return false; }
		USceneComponent* OriginalPose = T.Model->GetBonePoseSource(Bone);
		const int32 OriginalStock = T.Model->GetRemainingQuantity(TEXT("Bone"));
		// Register after session/bridge listeners so this callback genuinely changes the final state.
		T.Model->OnPhaseChanged.AddDynamic(T.Provider, &UAssemblyLifecycleTestProvider::HandlePhaseChanged);
		T.Provider->bRestoreMovementAfterPlayingNotification = Conflict == 0;
		T.Provider->bRestoreCapsuleAfterPlayingNotification = Conflict == 1;
		TestFalse(TEXT("Final notification state must be validated before returning success"), T.Session->StartPlaying(T.Reason));
		TestFalse(TEXT("Late proxy violation produces a failure reason"), T.Reason.IsEmpty());
		TestTrue(TEXT("Late violation rolls model back to Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
		TestEqual(TEXT("Apply reached provisional activation exactly once"), T.Provider->PlayingApplyCount, 1);
		TestEqual(TEXT("Late violation aborts the prepared activation once"), T.Provider->AbortCount, 1);
		TestEqual(TEXT("The listener received one provisional notification"), T.Provider->PlayingNotificationCount, 1);
		TestEqual(TEXT("Rollback emits one Assembly compensation notification"), T.Provider->AssemblyCompensationCount, 1);
		TestEqual(TEXT("Late rollback keeps stock"), T.Model->GetRemainingQuantity(TEXT("Bone")), OriginalStock);
		TestTrue(TEXT("Late rollback keeps editable body registration"), T.Model->GetBonePoseSource(Bone) == OriginalPose);
		TestEqual(TEXT("Late rollback keeps exactly the existing body"), T.Model->GetBones().Num(), 1);
		TestTrue(TEXT("Late rollback has no latched drive"), !T.Model->IsCoreRotationHeld());
		T.Provider->bRestoreMovementAfterPlayingNotification = false;
		T.Provider->bRestoreCapsuleAfterPlayingNotification = false;
		TestTrue(TEXT("After conflicting listener is fixed, retry can activate"), T.Session->StartPlaying(T.Reason));
		TestEqual(TEXT("Retry adds one activation with no duplicate notification activation"), T.Provider->PlayingApplyCount, 2);
		TestEqual(TEXT("Retry retains only the earlier abort"), T.Provider->AbortCount, 1);
		TestTrue(TEXT("Confirmed retry leaves movement safely frozen"), T.IsMovementFrozen());
		TestTrue(TEXT("Confirmed retry keeps proxy capsule disabled"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		T.World->Tick(LEVELTICK_All, 0.016f);
		T.Session->ShutdownSession();
		TestTrue(TEXT("Refreezing never overwrites initial Flying state"), T.Character->GetCharacterMovement()->MovementMode == MOVE_Flying);
		TestTrue(TEXT("Refreezing never overwrites initial TickOn state"), T.Character->GetCharacterMovement()->IsComponentTickEnabled());
		TestTrue(TEXT("Shutdown restores original disposable capsule collision"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == T.InitialCapsuleCollision);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyApplyCancellationTest, "JGD2026.Assembly.SessionProxy.ActivationCallbackShutdownDefersRestore", SessionProxyFlags)
bool FAssemblySessionProxyApplyCancellationTest::RunTest(const FString& Parameters)
{
	FSessionProxyWorld T(true, false);
	if (!TestTrue(TEXT("Activation callback fixture is ready"), T.bReady)
		|| !TestTrue(TEXT("Session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
	T.Provider->bShutdownSessionDuringPlayingApply = true;
	TestFalse(TEXT("Session shutdown during final activation prevents success"), T.Session->StartPlaying(T.Reason));
	TestTrue(TEXT("Canceled activation restores Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestEqual(TEXT("Canceled activation executes once"), T.Provider->PlayingApplyCount, 1);
	TestEqual(TEXT("Canceled activation aborts exactly once"), T.Provider->AbortCount, 1);
	TestEqual(TEXT("Canceled session shuts physical lifecycle down once"), T.Provider->ShutdownCount, 1);
	TestTrue(TEXT("Deferred shutdown sees frozen movement"), T.Provider->bMovementFrozenDuringShutdown);
	TestTrue(TEXT("Deferred shutdown sees disabled proxy collision"), T.Provider->bCapsuleDisabledDuringShutdown);
	T.World->Tick(LEVELTICK_All, 0.016f);
	TestTrue(TEXT("Post-callback release restores the original mode"), T.Character->GetCharacterMovement()->MovementMode == MOVE_Flying);
	TestTrue(TEXT("Post-callback release restores the original tick"), T.Character->GetCharacterMovement()->IsComponentTickEnabled());
	TestTrue(TEXT("Post-callback release restores collision only after shutdown"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == T.InitialCapsuleCollision);
	TestFalse(TEXT("Post-callback release removes only its input gate"), T.Controller->IsMoveInputIgnored());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyRefreshFailureTest, "JGD2026.Assembly.SessionProxy.FailedPlayingRefreshReturnsToStableAssembly", SessionProxyFlags)
bool FAssemblySessionProxyRefreshFailureTest::RunTest(const FString& Parameters)
{
	FSessionProxyWorld T(true, false);
	if (!TestTrue(TEXT("Refresh fixture is ready"), T.bReady)
		|| !TestTrue(TEXT("Session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Install before refresh rejection"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
		&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96,0), T.Reason)
		&& T.Interaction->CommitBoneDrag(Bone, T.Reason))) { return false; }
	const int32 Stock = T.Model->GetRemainingQuantity(TEXT("Bone"));
	USceneComponent* Pose = T.Model->GetBonePoseSource(Bone);
	TArray<bool> CoreEdges;
	const FDelegateHandle InputHandle = T.Model->OnCoreRotationRequestedNative.AddLambda([&](bool bHeld) { CoreEdges.Add(bHeld); });
	TestTrue(TEXT("Confirmed activation before refresh failure"), T.Session->StartPlaying(T.Reason));
	TestTrue(TEXT("Hold core before refresh failure"), T.Model->SetCoreRotationInput(true));
	T.Provider->bPlayingApplySucceeds = false;
	const int32 PhasesBeforeRejectedRefresh = T.Provider->PhaseCount;
	TestFalse(TEXT("A Playing refresh cannot silently accept failed activation"), T.Bridge->RefreshPhysicsLifecycle(T.Reason));
	TestEqual(TEXT("Refresh genuinely requests and receives failed executor activation"), T.Provider->PlayingApplyCount, 2);
	TestEqual(TEXT("Refresh aborts its new preparation token exactly once"), T.Provider->AbortCount, 1);
	TestEqual(TEXT("Failed refresh compensation cannot reactivate Assembly physics"), T.Provider->PhaseCount, PhasesBeforeRejectedRefresh + 1);
	TestEqual(TEXT("Failed refresh completely shuts the executor down once"), T.Provider->ShutdownCount, 1);
	TestTrue(TEXT("Failure compensation leaves physical lifecycle shut down"), T.Bridge->IsPhysicsLifecycleShutdown());
	TestFalse(TEXT("Failed refresh has a reason"), T.Reason.IsEmpty());
	TestTrue(TEXT("Failed refresh recovers stable Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestFalse(TEXT("Failed refresh releases held core drive"), T.Model->IsCoreRotationHeld());
	TestTrue(TEXT("Held core was actually followed by a false edge"), CoreEdges.Num() == 2 && CoreEdges[0] && !CoreEdges[1]);
	TestEqual(TEXT("Refresh recovery preserves stock"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
	TestEqual(TEXT("Refresh recovery preserves the installed part"), T.Model->GetBones().Num(), 1);
	TestTrue(TEXT("Refresh recovery preserves pose registration"), T.Model->GetBonePoseSource(Bone) == Pose);
	TestTrue(TEXT("Refresh recovery retains safe CMC protection"), T.IsMovementFrozen());
	TestTrue(TEXT("Refresh recovery retains safe capsule protection"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	T.Provider->bPlayingApplySucceeds = true;
	TestTrue(TEXT("Recovered Assembly can refresh after executor is fixed"), T.Bridge->RefreshPhysicsLifecycle(T.Reason));
	TestTrue(TEXT("Recovered editable structure can enter Playing again"), T.Session->StartPlaying(T.Reason));
	TestTrue(TEXT("Retry genuinely commits Playing"), T.Model->GetPhase() == EAssemblyPhase::Playing);
	T.Model->OnCoreRotationRequestedNative.Remove(InputHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionProxyFinalObserverTest, "JGD2026.Assembly.SessionProxy.FinalSessionObserversCannotReportUnsafeSuccess", SessionProxyFlags)
bool FAssemblySessionProxyFinalObserverTest::RunTest(const FString& Parameters)
{
	for (int32 Notification = 0; Notification < 2; ++Notification)
	{
		for (int32 Conflict = 0; Conflict < 2; ++Conflict)
		{
			FSessionProxyWorld T(true, false);
			if (!TestTrue(TEXT("Final observer fixture is ready"), T.bReady)
				|| !TestTrue(TEXT("Protected session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
			FGuid Bone;
			if (!TestTrue(TEXT("Install editable body before final observer conflict"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
				&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96,0), T.Reason)
				&& T.Interaction->CommitBoneDrag(Bone, T.Reason))) { return false; }
			USceneComponent* OriginalPose = T.Model->GetBonePoseSource(Bone);
			const int32 OriginalStock = T.Model->GetRemainingQuantity(TEXT("Bone"));
			T.Provider->bRestoreMovementAfterSessionNotification = Conflict == 0;
			T.Provider->bRestoreCapsuleAfterSessionNotification = Conflict == 1;
			if (Notification == 0) { T.Session->OnMessage.AddDynamic(T.Provider, &UAssemblyLifecycleTestProvider::HandleSessionMessage); }
			else { T.Session->OnSessionChanged.AddDynamic(T.Provider, &UAssemblyLifecycleTestProvider::HandleSessionChanged); }
			const int32 PhasesBeforeRejectedStart = T.Provider->PhaseCount;
			TestFalse(TEXT("Unsafe ShowMessage/OnSessionChanged callback cannot leave successful Playing"), T.Session->StartPlaying(T.Reason));
			TestTrue(TEXT("The observer really modified the final character proxy"), T.Provider->bSessionObserverTriggered);
			TestFalse(TEXT("Final observer rejection has a reason"), T.Reason.IsEmpty());
			TestTrue(TEXT("Final observer conflict recovers Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
			TestEqual(TEXT("Final observer conflict shuts committed physics down once"), T.Provider->ShutdownCount, 1);
			TestEqual(TEXT("Final observer compensation cannot reactivate Assembly physics"), T.Provider->PhaseCount, PhasesBeforeRejectedStart + 1);
			TestTrue(TEXT("Final observer recovery preserves completed physical shutdown"), T.Bridge->IsPhysicsLifecycleShutdown());
			TestEqual(TEXT("Already finalized token is never aborted as pending preparation"), T.Provider->AbortCount, 0);
			TestEqual(TEXT("Model reached one actual confirmed activation before the late observer"), T.Provider->PlayingApplyCount, 1);
			TestEqual(TEXT("Recovery retains installed stock"), T.Model->GetRemainingQuantity(TEXT("Bone")), OriginalStock);
			TestEqual(TEXT("Recovery retains the existing body"), T.Model->GetBones().Num(), 1);
			TestTrue(TEXT("Recovery retains editable body registration"), T.Model->GetBonePoseSource(Bone) == OriginalPose);
			TestFalse(TEXT("Late observer failure leaves no held core drive"), T.Model->IsCoreRotationHeld());
			T.Provider->bRestoreMovementAfterSessionNotification = false;
			T.Provider->bRestoreCapsuleAfterSessionNotification = false;
			T.Provider->bSessionObserverTriggered = false;
			TestTrue(TEXT("Fixed final observer allows a normal retry"), T.Session->StartPlaying(T.Reason));
			TestTrue(TEXT("Retry has actually entered Playing"), T.Model->GetPhase() == EAssemblyPhase::Playing);
			TestTrue(TEXT("Retry validates frozen movement"), T.IsMovementFrozen());
			TestTrue(TEXT("Retry validates disabled proxy collision"), T.Character->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			TestEqual(TEXT("Retry activates once, with no hidden duplicate Apply"), T.Provider->PlayingApplyCount, 2);
			TestEqual(TEXT("Retry does not add a fake abort"), T.Provider->AbortCount, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionNativeMuscleShutdownTest, "JGD2026.Assembly.SessionProxy.NativeMuscleCallbackShutdownCannotLeaveHeldDrive", SessionProxyFlags)
bool FAssemblySessionNativeMuscleShutdownTest::RunTest(const FString& Parameters)
{
	FSessionProxyWorld T(true, false);
	if (!TestTrue(TEXT("Native shutdown fixture is ready"), T.bReady)
		|| !TestTrue(TEXT("Protected session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
	FGuid Bone, MuscleA, MuscleB;
	if (!TestTrue(TEXT("Install connected body"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
		&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96,0), T.Reason)
		&& T.Interaction->CommitBoneDrag(Bone, T.Reason))) { return false; }
	FAssemblyMuscleEndpoint A, B;
	A.BoneId = B.BoneId = Bone;
	A.BoneLocalPoint = FVector2D(-20,0);
	B.BoneLocalPoint = FVector2D(20,0);
	TestTrue(TEXT("Select W for both muscles"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::W, T.Reason));
	TestTrue(TEXT("Install first W muscle"), T.Interaction->InstallMuscleAtEndpoints(TEXT("Muscle"), A, B, MuscleA, T.Reason));
	TestTrue(TEXT("Install second W muscle"), T.Interaction->InstallMuscleAtEndpoints(TEXT("Muscle"), A, B, MuscleB, T.Reason));
	if (!TestTrue(TEXT("Confirm Playing before native shutdown"), T.Session->StartPlaying(T.Reason))) { return false; }
	UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(T.Model);
	if (!TestNotNull(TEXT("Runtime message bus is present"), Bus)) { return false; }
	UAssemblyPhysicsTestReceiver* Receiver = NewObject<UAssemblyPhysicsTestReceiver>(T.GameInstance);
	FJGDMessageReceived Listener;
	Listener.BindDynamic(Receiver, &UAssemblyPhysicsTestReceiver::Receive);
	Bus->RegisterListener(AssemblyMessageTags::MuscleDriveRequested, Listener);
	int32 NativePressCount = 0;
	const FDelegateHandle NativeHandle = T.Model->OnMuscleDriveRequestedNative.AddLambda([&](FGuid Id, bool bContracting)
	{
		if (bContracting)
		{
			++NativePressCount;
			T.Session->ShutdownSession();
		}
	});
	TestTrue(TEXT("The outer W press entered its synchronous observer"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestEqual(TEXT("Shutdown prevents stale second native contraction"), NativePressCount, 1);
	TMap<FGuid, bool> LastIntent;
	for (const FInstancedStruct& Message : Receiver->Messages)
	{
		if (const FAssemblyPhysicsMuscleDriveMessage* Drive = Message.GetPtr<FAssemblyPhysicsMuscleDriveMessage>())
		{
			LastIntent.Add(Drive->Muscle.InstanceId, Drive->bContracting);
		}
	}
	for (const FGuid Id : { MuscleA, MuscleB })
	{
		FAssemblyMuscleInstance Muscle;
		TestTrue(TEXT("Shutdown preserves muscle data for an explicit later session entry"), T.Model->GetMuscle(Id, Muscle));
		TestFalse(TEXT("Every same-key muscle is relaxed after callback shutdown"), Muscle.bContracting);
		TestTrue(TEXT("Every muscle ends with a typed stop after session shutdown"), LastIntent.Contains(Id) && !LastIntent[Id]);
	}
	TestEqual(TEXT("Callback shutdown invokes complete physics shutdown once"), T.Provider->ShutdownCount, 1);
	TestEqual(TEXT("Shutdown does not silently return installed muscle stock"), T.Model->GetRemainingQuantity(TEXT("Muscle")), 1);
	const int32 CountAfterShutdown = Receiver->Count(AssemblyMessageTags::MuscleDriveRequested);
	TestFalse(TEXT("A shut-down session cannot refresh held contractions"), T.Bridge->RefreshPhysicsLifecycle(T.Reason));
	TestEqual(TEXT("Rejected refresh emits no stale contraction"), Receiver->Count(AssemblyMessageTags::MuscleDriveRequested), CountAfterShutdown);
	T.Model->OnMuscleDriveRequestedNative.Remove(NativeHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySessionTypedMuscleShutdownTest, "JGD2026.Assembly.SessionProxy.TypedMuscleFanoutShutdownPreservesFinalStops", SessionProxyFlags)
bool FAssemblySessionTypedMuscleShutdownTest::RunTest(const FString& Parameters)
{
	FSessionProxyWorld T(true, false);
	if (!TestTrue(TEXT("Typed shutdown fixture is ready"), T.bReady)
		|| !TestTrue(TEXT("Protected session initializes"), T.Initialize())) { AddError(T.Reason.ToString()); return false; }
	FGuid Bone, MuscleA, MuscleB;
	if (!TestTrue(TEXT("Install connected body"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
		&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96,0), T.Reason)
		&& T.Interaction->CommitBoneDrag(Bone, T.Reason))) { return false; }
	FAssemblyMuscleEndpoint A, B;
	A.BoneId = B.BoneId = Bone;
	A.BoneLocalPoint = FVector2D(-20,0);
	B.BoneLocalPoint = FVector2D(20,0);
	TestTrue(TEXT("Select shared W key"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::W, T.Reason));
	TestTrue(TEXT("Install first muscle"), T.Interaction->InstallMuscleAtEndpoints(TEXT("Muscle"), A, B, MuscleA, T.Reason));
	TestTrue(TEXT("Install second muscle"), T.Interaction->InstallMuscleAtEndpoints(TEXT("Muscle"), A, B, MuscleB, T.Reason));
	if (!TestTrue(TEXT("Confirm Playing before typed shutdown"), T.Session->StartPlaying(T.Reason))) { return false; }
	const FAssemblyPhysicsContext OriginalContext = T.Bridge->GetPhysicsContext();
	UAssemblyPhysicsTestReceiver* First = T.ListenMuscleDrive();
	UAssemblyPhysicsTestReceiver* Second = T.ListenMuscleDrive();
	if (!TestNotNull(TEXT("First typed receiver registered"), First) || !TestNotNull(TEXT("Second typed executor registered"), Second)) { return false; }
	First->Model = T.Model;
	First->Session = T.Session;
	First->bShutdownSessionOnContractingMuscle = true;
	TestTrue(TEXT("W press reaches a real synchronous typed fanout"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("First typed listener really closed Session"), First->bTriggered);
	TestEqual(TEXT("Reentrant typed shutdown executes physical shutdown once"), T.Provider->ShutdownCount, 1);
	TestTrue(TEXT("Typed shutdown fully stops the physical lifecycle"), T.Bridge->IsPhysicsLifecycleShutdown());
	TestNull(TEXT("Typed shutdown releases Session's model binding"), T.Session->GetAssemblyComponent());
	TestTrue(TEXT("Caller-owned Interaction keeps its reusable bridge binding"), T.Bridge->IsBridgeInitialized()
		&& T.Interaction->GetPhysicsBridge() == T.Bridge);
	TMap<FGuid, TArray<bool>> Edges;
	for (const FInstancedStruct& Message : Second->Messages)
	{
		if (const FAssemblyPhysicsMuscleDriveMessage* Request = Message.GetPtr<FAssemblyPhysicsMuscleDriveMessage>())
		{
			Edges.FindOrAdd(Request->Muscle.InstanceId).Add(Request->bContracting);
			TestTrue(TEXT("Deferred shutdown stops retain original SourceId"), Request->Context.SourceId == OriginalContext.SourceId);
			TestEqual(TEXT("Deferred shutdown stops retain original round"), Request->Context.RoundGeneration, OriginalContext.RoundGeneration);
			TestTrue(TEXT("Deferred stops retain original world and model"), Request->Context.World == T.World && Request->Context.Assembly == T.Model);
		}
	}
	for (const FGuid Id : { MuscleA, MuscleB })
	{
		FAssemblyMuscleInstance Muscle;
		TestTrue(TEXT("Shutdown preserves each model instance"), T.Model->GetMuscle(Id, Muscle));
		TestFalse(TEXT("Each model muscle finishes relaxed"), Muscle.bContracting);
		const TArray<bool>* Requests = Edges.Find(Id);
		TestTrue(TEXT("Second executor ends with a typed stop for each muscle"), Requests && !Requests->IsEmpty() && !Requests->Last());
		if (Requests)
		{
			bool bStopped = false;
			for (bool bContracting : *Requests)
			{
				if (!bContracting) { bStopped = true; }
				else { TestFalse(TEXT("Outer fanout cannot overwrite a nested shutdown stop"), bStopped); }
			}
		}
	}
	TestFalse(TEXT("Typed session shutdown leaves no held core state"), T.Model->IsCoreRotationHeld());
	TestEqual(TEXT("Typed shutdown preserves installed muscle inventory"), T.Model->GetRemainingQuantity(TEXT("Muscle")), 1);
	const int32 BeforeRefresh = Second->Count(AssemblyMessageTags::MuscleDriveRequested);
	TestFalse(TEXT("Closed session cannot replay pending drive intents"), T.Bridge->RefreshPhysicsLifecycle(T.Reason));
	TestEqual(TEXT("Rejected refresh adds no stale drive"), Second->Count(AssemblyMessageTags::MuscleDriveRequested), BeforeRefresh);
	return true;
}

#endif
