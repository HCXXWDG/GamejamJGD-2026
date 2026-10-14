#include "AssemblyLifecycleTestProvider.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	bool SnapshotBodiesAreAlive(const FAssemblyPhysicsSnapshotMessage& Snapshot)
	{
		if (!IsValid(Snapshot.CoreBody) || !Snapshot.CoreBody->IsRegistered()) { return false; }
		for (const FAssemblyPhysicsBonePose& Pose : Snapshot.BonePoses)
		{
			if (!IsValid(Pose.Body) || !Pose.Body->IsRegistered()
				|| !IsValid(Pose.Body->GetOwner()) || Pose.Body->GetOwner()->IsActorBeingDestroyed()) { return false; }
		}
		return true;
	}
}

bool UAssemblyLifecycleTestProvider::PrepareForPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	++PrepareCount;
	Calls.Add(TEXT("Prepare"));
	Snapshots.Add(Snapshot);
	if ((bReplacePoseDuringPrepare || bReplaceCoreDuringPrepare) && Model && GetOwner())
	{
		UBoxComponent* NewBody = NewObject<UBoxComponent>(GetOwner());
		GetOwner()->AddInstanceComponent(NewBody);
		NewBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NewBody->RegisterComponent();
		TemporaryBody = NewBody;
		if (bReplacePoseDuringPrepare && !Snapshot.BonePoses.IsEmpty())
		{
			NewBody->SetWorldTransform(Snapshot.BonePoses[0].WorldTransform);
			FText RegistrationReason;
			Model->RegisterBonePoseSource(Snapshot.BonePoses[0].BoneId, NewBody, RegistrationReason);
		}
		if (bReplaceCoreDuringPrepare && Bridge) { Bridge->SetCoreBody(NewBody); }
	}
	if (bRestartDuringPrepare && Model)
	{
		FText NestedReason;
		bNestedRestartSucceeded = Model->RestartAssembly(NestedReason);
	}
	if (bClearDuringPrepare && Bridge) { Bridge->ClearBridge(); }
	if (bShutdownSessionDuringPrepare && Session)
	{
		Session->ShutdownSession();
		if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
			bMovementFrozenAfterSessionShutdown = Movement && Movement->MovementMode == MOVE_None && !Movement->IsComponentTickEnabled();
		}
	}
	OutReason = bPrepareSucceeds ? FText::GetEmpty() : FText::FromString(TEXT("Test executor rejected preparation."));
	return bPrepareSucceeds;
}

void UAssemblyLifecycleTestProvider::AbortPreparedPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot)
{
	++AbortCount;
	Calls.Add(TEXT("Abort"));
	Snapshots.Add(Snapshot);
	if (IsValid(TemporaryBody)) { TemporaryBody->DestroyComponent(); }
	TemporaryBody = nullptr;
}

bool UAssemblyLifecycleTestProvider::StopBeforeReset_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	++StopCount;
	Calls.Add(TEXT("Stop"));
	Snapshots.Add(Snapshot);
	bCoreAliveDuringStop = IsValid(Snapshot.CoreBody) && Snapshot.CoreBody->IsRegistered();
	bAllBodiesAliveDuringStop = SnapshotBodiesAreAlive(Snapshot);
	bAllPosesStillRegisteredDuringStop = Model != nullptr;
	for (const FAssemblyPhysicsBonePose& Pose : Snapshot.BonePoses)
	{
		bAllPosesStillRegisteredDuringStop &= Model && Model->GetBonePoseSource(Pose.BoneId) == Pose.PoseSource;
	}
	if (bRestartDuringStop && Model)
	{
		FText NestedReason;
		bNestedRestartSucceeded = Model->RestartAssembly(NestedReason);
	}
	if (bRequestInputDuringStop && Model)
	{
		bCoreInputAcceptedDuringStop = Model->SetCoreRotationInput(true);
		bMuscleInputAcceptedDuringStop = Model->SetMuscleInput(EAssemblyMuscleKey::W, true);
	}
	OutReason = bStopSucceeds ? FText::GetEmpty() : FText::FromString(TEXT("Test executor has not released constraints."));
	return bStopSucceeds;
}

bool UAssemblyLifecycleTestProvider::ApplyAssemblyPhase_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	++PhaseCount;
	Calls.Add(Snapshot.Phase == EAssemblyPhase::Playing ? TEXT("Phase.Playing") : TEXT("Phase.Assembly"));
	Snapshots.Add(Snapshot);
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		bMovementFrozenDuringPhase = Movement && Movement->MovementMode == MOVE_None && !Movement->IsComponentTickEnabled();
		bAnyPhaseBeforeMovementSuspension |= !bMovementFrozenDuringPhase;
		const bool bCapsuleDisabled = Character->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
		if (PhaseCount == 1) { bCapsuleDisabledDuringFirstPhase = bCapsuleDisabled; }
		if (Snapshot.Phase == EAssemblyPhase::Playing) { bCapsuleDisabledDuringPlayingApply = bCapsuleDisabled; }
	}
	if (Snapshot.Phase == EAssemblyPhase::Playing)
	{
		++PlayingApplyCount;
		if (bUseDefaultPlayingApply) { return IAssemblyPhysicsLifecycle::ApplyAssemblyPhase_Implementation(Snapshot, OutReason); }
		if (bRestartDuringPlayingApply && Model)
		{
			FText NestedReason;
			bNestedRestartSucceeded = Model->RestartAssembly(NestedReason);
		}
		if (bRequestInputDuringPlayingApply && Model)
		{
			bCoreInputAcceptedDuringPlayingApply = Model->SetCoreRotationInput(true);
			bMuscleInputAcceptedDuringPlayingApply = Model->SetMuscleInput(EAssemblyMuscleKey::W, true);
		}
		if (bClearDuringPlayingApply && Bridge) { Bridge->ClearBridge(); }
		if (bShutdownSessionDuringPlayingApply && Session) { Session->ShutdownSession(); }
		if (bDestroyDuringPlayingApply) { DestroyComponent(); }
		OutReason = bPlayingApplySucceeds ? FText::GetEmpty() : FText::FromString(TEXT("Test executor rejected activation."));
		return bPlayingApplySucceeds;
	}
	if (bAllocateResourcesDuringAssemblyApply) { bExecutorResourcesActive = true; }
	OutReason = bAssemblyApplySucceeds ? FText::GetEmpty() : FText::FromString(TEXT("Test executor failed while entering stable editing."));
	return bAssemblyApplySucceeds;
}

void UAssemblyLifecycleTestProvider::HandlePhaseChanged(EAssemblyPhase Phase)
{
	if (Phase == EAssemblyPhase::Assembly)
	{
		++AssemblyCompensationCount;
		return;
	}
	++PlayingNotificationCount;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (bRestoreMovementAfterPlayingNotification)
		{
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Character->GetCharacterMovement()->SetComponentTickEnabled(true);
		}
		if (bRestoreCapsuleAfterPlayingNotification)
		{
			Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
	}
}

void UAssemblyLifecycleTestProvider::DisruptSessionProtection()
{
	if (!Model || Model->GetPhase() != EAssemblyPhase::Playing || bSessionObserverTriggered) { return; }
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || (!bRestoreMovementAfterSessionNotification && !bRestoreCapsuleAfterSessionNotification)) { return; }
	bSessionObserverTriggered = true;
	if (bRestoreMovementAfterSessionNotification)
	{
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Character->GetCharacterMovement()->SetComponentTickEnabled(true);
	}
	if (bRestoreCapsuleAfterSessionNotification)
	{
		Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void UAssemblyLifecycleTestProvider::HandleSessionChanged() { DisruptSessionProtection(); }
void UAssemblyLifecycleTestProvider::HandleSessionMessage(FText Message) { DisruptSessionProtection(); }

void UAssemblyLifecycleTestProvider::ShutdownPhysics_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot)
{
	++ShutdownCount;
	Calls.Add(TEXT("Shutdown"));
	bExecutorResourcesActive = false;
	Snapshots.Add(Snapshot);
	bAllBodiesAliveDuringShutdown = SnapshotBodiesAreAlive(Snapshot);
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		bMovementFrozenDuringShutdown = Movement && Movement->MovementMode == MOVE_None && !Movement->IsComponentTickEnabled();
		bCapsuleDisabledDuringShutdown = Character->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
	}
}

void UAssemblyLifecycleTestProvider::HandleMovementModeChanged(ACharacter* Character,
	EMovementMode PreviousMode, uint8 PreviousCustomMode)
{
	if (!bRefreshOnMovementRestore || !IsValid(Character)
		|| Character->GetCharacterMovement()->MovementMode != MOVE_Flying) { return; }
	++MovementRestoreRefreshCount;
	FText Reason;
	bRefreshAcceptedOnMovementRestore = Bridge && Bridge->RefreshPhysicsLifecycle(Reason);
}

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyPhysicsTestReceiver.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr EAutomationTestFlags LifecycleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FLifecycleWorld
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UAssemblyComponent* Model = nullptr;
		UAssemblyPhysicsBridgeComponent* Bridge = nullptr;
		UAssemblyInteractionComponent* Interaction = nullptr;
		UAssemblyLifecycleTestProvider* Provider = nullptr;
		UBoxComponent* Core = nullptr;
		UJGDMessageSubsystem* Bus = nullptr;
		bool bReady = false;
		FText Reason;

		explicit FLifecycleWorld(bool bWithProvider = true)
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone(FName(*FString::Printf(TEXT("AssemblyLifecycle_%s"), *FGuid::NewGuid().ToString())));
			World = GameInstance->GetWorld();
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			if (!World->SetGameMode(URL)) { Reason = FText::FromString(TEXT("Cannot create native lifecycle test GameMode.")); return; }
			World->InitializeActorsForPlay(URL);
			Owner = World->SpawnActor<AActor>();
			Core = NewObject<UBoxComponent>(Owner);
			Owner->AddInstanceComponent(Core);
			Owner->SetRootComponent(Core);
			Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Core->RegisterComponent();
			Model = NewObject<UAssemblyComponent>(Owner);
			Model->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Model);
			Model->RegisterComponent();
			bReady = Model->ConfigureFreeAssembly(FAssemblyFreeSettings(), { FAssemblyBoneDefinition() }, { FAssemblyMuscleDefinition() }, Reason);
			Bridge = NewObject<UAssemblyPhysicsBridgeComponent>(Owner);
			Owner->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			if (bWithProvider)
			{
				Provider = AddProvider();
				bReady = bReady && Bridge->SetPhysicsLifecycleProvider(Provider, Reason);
			}
			Interaction = NewObject<UAssemblyInteractionComponent>(Owner);
			Interaction->bAutoInitialize = false;
			Interaction->ExistingCoreBody = Core;
			Interaction->bCreateCorePlaceholder = false;
			Owner->AddInstanceComponent(Interaction);
			Interaction->RegisterComponent();
			bReady = bReady && Interaction->InitializeInteraction(Model, Reason);
			Bridge->bRequirePhysicsLifecycleProvider = true;
			Bus = UJGDMessageSubsystem::Get(Model);
		}

		~FLifecycleWorld()
		{
			if (Provider) { Provider->bClearDuringPrepare = false; Provider->bRestartDuringPrepare = false; Provider->bRestartDuringStop = false; }
			if (IsValid(Interaction)) { Interaction->ClearInteraction(); }
			if (IsValid(Bridge)) { Bridge->ClearBridge(); }
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}

		UAssemblyLifecycleTestProvider* AddProvider()
		{
			UAssemblyLifecycleTestProvider* Result = NewObject<UAssemblyLifecycleTestProvider>(Owner);
			Owner->AddInstanceComponent(Result);
			Result->Model = Model;
			Result->Bridge = Bridge;
			Result->RegisterComponent();
			return Result;
		}

		bool InstallBone(FGuid& OutId)
		{
			return Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason)
				&& Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96.0, 0.0), Reason)
				&& Interaction->CommitBoneDrag(OutId, Reason);
		}

		UAssemblyPhysicsTestReceiver* ListenCore()
		{
			UAssemblyPhysicsTestReceiver* Receiver = NewObject<UAssemblyPhysicsTestReceiver>(GameInstance);
			FJGDMessageReceived Delegate;
			Delegate.BindDynamic(Receiver, &UAssemblyPhysicsTestReceiver::Receive);
			Bus->RegisterListener(AssemblyMessageTags::CoreRotationRequested, Delegate);
			return Receiver;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecyclePrepareFailureTest, "JGD2026.Assembly.Lifecycle.PrepareFailurePreservesAssembly", LifecycleTestFlags)
bool FAssemblyLifecyclePrepareFailureTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Install connected bone view"), T.InstallBone(Bone))) { AddError(T.Reason.ToString()); return false; }
	ANormalBone* View = T.Interaction->GetBoneActor(Bone);
	const int32 Stock = T.Model->GetRemainingQuantity(TEXT("Bone"));
	const FAssemblyPhysicsContext Before = T.Bridge->GetPhysicsContext();
	const int32 PhasesBefore = T.Provider->PhaseCount;
	T.Provider->bPrepareSucceeds = false;
	TestFalse(TEXT("Direct model start honors physics rejection"), T.Model->TryStartPlaying(T.Reason));
	TestFalse(TEXT("Executor rejection reaches caller"), T.Reason.IsEmpty());
	TestTrue(TEXT("Failure keeps Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestEqual(TEXT("Prepare executes once"), T.Provider->PrepareCount, 1);
	TestEqual(TEXT("Partial preparation is aborted once"), T.Provider->AbortCount, 1);
	TestEqual(TEXT("No Playing phase is applied"), T.Provider->PhaseCount, PhasesBefore);
	TestEqual(TEXT("Stock is preserved"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
	TestTrue(TEXT("Installed actor remains alive"), IsValid(View) && !View->IsActorBeingDestroyed());
	TestTrue(TEXT("Pose registration remains"), T.Model->GetBonePoseSource(Bone) == View->GetBoneBody());
	TestEqual(TEXT("No reset generation on rejection"), T.Bridge->GetPhysicsContext().RoundGeneration, Before.RoundGeneration);
	// A normal executor may allocate/register a body, then fail on another constraint.
	T.Provider->bReplacePoseDuringPrepare = true;
	T.Provider->bReplaceCoreDuringPrepare = true;
	TestFalse(TEXT("Partial physical preparation still rejects start"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Abort restores the original registered view pose"), T.Model->GetBonePoseSource(Bone) == View->GetBoneBody());
	FAssemblyPhysicsSnapshotMessage Restored;
	TestTrue(TEXT("Snapshot remains available after rollback"), T.Bridge->GetPhysicsSnapshot(Restored));
	TestTrue(TEXT("Rollback restores original physical core"), Restored.CoreBody == T.Core);
	TestTrue(TEXT("An existing bone remains draggable after failed preparation"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Bone, T.Reason));
	T.Interaction->CancelBoneDrag();
	T.Bridge->SetCoreBody(nullptr);
	TestFalse(TEXT("Partial preparation from an absent original core rejects start"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Nullable core rollback has a snapshot"), T.Bridge->GetPhysicsSnapshot(Restored));
	TestNull(TEXT("Abort preserves an original null core"), Restored.CoreBody.Get());
	TestTrue(TEXT("Null-core rollback also restores bone pose"), T.Model->GetBonePoseSource(Bone) == View->GetBoneBody());
	TestTrue(TEXT("Null-core rollback does not lock existing drag"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Bone, T.Reason));
	T.Interaction->CancelBoneDrag();
	T.Provider->bReplacePoseDuringPrepare = false;
	T.Provider->bReplaceCoreDuringPrepare = false;
	T.Bridge->SetCoreBody(T.Core);
	T.Provider->bPrepareSucceeds = true;
	TestTrue(TEXT("Retry may enter Playing"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Phase commits after success"), T.Model->GetPhase() == EAssemblyPhase::Playing);
	TestEqual(TEXT("Each of three failed preparations aborts once"), T.Provider->AbortCount, 3);
	TestTrue(TEXT("Repeated start is idempotent"), T.Model->TryStartPlaying(T.Reason));
	TestEqual(TEXT("Repeated Playing does not prepare again"), T.Provider->PrepareCount, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleBodiesTest, "JGD2026.Assembly.Lifecycle.RequiredProviderAndRegisteredBodies", LifecycleTestFlags)
bool FAssemblyLifecycleBodiesTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T(false);
	if (!TestTrue(TEXT("Fixture initializes without executor"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Install"), T.InstallBone(Bone))) { return false; }
	TestFalse(TEXT("Required executor prevents unsupported Playing"), T.Model->TryStartPlaying(T.Reason));
	TestFalse(TEXT("Missing executor has reason"), T.Reason.IsEmpty());
	T.Bridge->bRequirePhysicsLifecycleProvider = false;
	TestTrue(TEXT("Optional executor preserves the existing visual prototype"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Visual prototype may restart without an executor"), T.Model->RestartAssembly(T.Reason));
	TestTrue(TEXT("Restore connected view after prototype reset"), T.InstallBone(Bone));
	T.Bridge->bRequirePhysicsLifecycleProvider = true;
	T.Provider = T.AddProvider();
	TestTrue(TEXT("Explicit executor binds"), T.Bridge->SetPhysicsLifecycleProvider(T.Provider, T.Reason));
	T.Model->UnregisterBonePoseSource(Bone);
	TestFalse(TEXT("A provider cannot claim ready while a bone has no body"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Still assembling without body"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestEqual(TEXT("Body validation failure aborts partial work"), T.Provider->AbortCount, 1);
	ANormalBone* View = T.Interaction->GetBoneActor(Bone);
	if (!TestNotNull(TEXT("The geometry-valid installed view remains available"), View)) { return false; }
	USceneComponent* NonPhysicalPose = NewObject<USceneComponent>(T.Owner);
	T.Owner->AddInstanceComponent(NonPhysicalPose);
	NonPhysicalPose->RegisterComponent();
	NonPhysicalPose->SetWorldTransform(View->GetBoneBody()->GetComponentTransform());
	TestTrue(TEXT("Generic scene pose can be registered for rendering"), T.Model->RegisterBonePoseSource(Bone, NonPhysicalPose, T.Reason));
	TestFalse(TEXT("Scene pose is not a physical primitive body"), T.Model->TryStartPlaying(T.Reason));
	TestEqual(TEXT("Each rejected preparation has exactly one abort"), T.Provider->AbortCount, 2);
	TestTrue(TEXT("Restore registered primitive"), T.Model->RegisterBonePoseSource(Bone, View->GetBoneBody(), T.Reason));
	T.Bridge->SetCoreBody(nullptr);
	TestFalse(TEXT("Missing physical core is rejected"), T.Model->TryStartPlaying(T.Reason));
	T.Bridge->SetCoreBody(T.Core);
	TestTrue(TEXT("Real registered references allow Playing"), T.Model->TryStartPlaying(T.Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleResetFailureTest, "JGD2026.Assembly.Lifecycle.ResetFailureProtectsEveryEntry", LifecycleTestFlags)
bool FAssemblyLifecycleResetFailureTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady)) { return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Install"), T.InstallBone(Bone))) { return false; }
	ANormalBone* View = T.Interaction->GetBoneActor(Bone);
	FAssemblyMuscleEndpoint EndpointA, EndpointB;
	EndpointA.BoneId = EndpointB.BoneId = Bone;
	EndpointA.BoneLocalPoint = FVector2D(-20.0, 0.0);
	EndpointB.BoneLocalPoint = FVector2D(20.0, 0.0);
	FGuid Muscle;
	TestTrue(TEXT("Select W for real installed muscle"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::W, T.Reason));
	TestTrue(TEXT("Install a muscle for stop reentry check"), T.Interaction->InstallMuscleAtEndpoints(TEXT("Muscle"), EndpointA, EndpointB, Muscle, T.Reason));
	TestTrue(TEXT("Start"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Hold core before stop"), T.Model->SetCoreRotationInput(true));
	TestTrue(TEXT("Hold muscle before stop"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	T.Provider->bStopSucceeds = false;
	T.Provider->bRequestInputDuringStop = true;
	const FAssemblyPhysicsContext Before = T.Bridge->GetPhysicsContext();
	const int32 Stock = T.Model->GetRemainingQuantity(TEXT("Bone"));
	TestFalse(TEXT("Restart stops before reset"), T.Model->RestartAssembly(T.Reason));
	TestFalse(TEXT("Stop callback cannot re-press core"), T.Provider->bCoreInputAcceptedDuringStop);
	TestFalse(TEXT("Stop callback cannot re-press muscle"), T.Provider->bMuscleInputAcceptedDuringStop);
	TestFalse(TEXT("Core stays released when stopping was rejected"), T.Model->IsCoreRotationHeld());
	FAssemblyMuscleInstance ReleasedMuscle;
	TestTrue(TEXT("Rejected stop preserves muscle instance"), T.Model->GetMuscle(Muscle, ReleasedMuscle));
	TestFalse(TEXT("Muscle stays released when stopping was rejected"), ReleasedMuscle.bContracting);
	TestFalse(TEXT("EnterAssembly also honors stop rejection"), T.Model->EnterAssembly(T.Reason));
	TestFalse(TEXT("Free reconfiguration cannot bypass stop"), T.Model->ConfigureFreeAssembly(FAssemblyFreeSettings(), { FAssemblyBoneDefinition() }, { FAssemblyMuscleDefinition() }, T.Reason));
	TestFalse(TEXT("Grid reconfiguration cannot bypass stop"), T.Model->ConfigureAssembly(T.Model->DefaultGrid, T.Model->DefaultBones, T.Model->DefaultMuscles, T.Reason));
	TestEqual(TEXT("All four destructive entry points requested stop"), T.Provider->StopCount, 4);
	TestTrue(TEXT("Failed stop leaves phase unchanged"), T.Model->GetPhase() == EAssemblyPhase::Playing);
	TestTrue(TEXT("Failed stop leaves part alive"), IsValid(View) && !View->IsActorBeingDestroyed());
	TestEqual(TEXT("Failed stop keeps instance count"), T.Model->GetBones().Num(), 1);
	TestEqual(TEXT("Failed stop keeps stock"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
	TestTrue(TEXT("Failed stop keeps pose"), T.Model->GetBonePoseSource(Bone) == View->GetBoneBody());
	TestEqual(TEXT("Failed stop does not advance round"), T.Bridge->GetPhysicsContext().RoundGeneration, Before.RoundGeneration);
	T.Provider->bStopSucceeds = true;
	TestTrue(TEXT("After release restart succeeds"), T.Model->RestartAssembly(T.Reason));
	TestTrue(TEXT("Stop saw live core"), T.Provider->bCoreAliveDuringStop);
	TestTrue(TEXT("Stop saw live registered bones before destruction"), T.Provider->bAllBodiesAliveDuringStop);
	TestTrue(TEXT("Stop saw intact model pose registrations"), T.Provider->bAllPosesStillRegisteredDuringStop);
	TestTrue(TEXT("Successful reset destroys old actor only afterwards"), !IsValid(View) || View->IsActorBeingDestroyed());
	TestNull(TEXT("Old pose removed after successful stop"), T.Model->GetBonePoseSource(Bone));
	TestEqual(TEXT("Inventory is restored"), T.Model->GetRemainingQuantity(TEXT("Bone")), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleRepeatedRoundsTest, "JGD2026.Assembly.Lifecycle.ThreeRoundsAndShutdownOrder", LifecycleTestFlags)
bool FAssemblyLifecycleRepeatedRoundsTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady)) { return false; }
	const FAssemblyPhysicsContext Initial = T.Bridge->GetPhysicsContext();
	for (int32 Round = 0; Round < 3; ++Round)
	{
		FGuid Bone;
		if (!TestTrue(TEXT("Install in round"), T.InstallBone(Bone))) { AddError(T.Reason.ToString()); return false; }
		TestTrue(TEXT("Start in round"), T.Model->TryStartPlaying(T.Reason));
		TestTrue(TEXT("Start again does not duplicate"), T.Model->TryStartPlaying(T.Reason));
		TestTrue(TEXT("Playing accepts exactly one core press per round"), T.Model->SetCoreRotationInput(true));
		T.Model->SetCoreRotationInput(true);
		TestTrue(TEXT("Reset in round"), T.Model->RestartAssembly(T.Reason));
		TestEqual(TEXT("No installed leftovers"), T.Model->GetBones().Num(), 0);
		TestEqual(TEXT("Inventory restored each round"), T.Model->GetRemainingQuantity(TEXT("Bone")), 4);
	}
	TestEqual(TEXT("Exactly one preparation per round"), T.Provider->PrepareCount, 3);
	TestEqual(TEXT("Exactly one confirmed activation per round"), T.Provider->PlayingApplyCount, 3);
	TestEqual(TEXT("Successful activation leaves no aborted token"), T.Provider->AbortCount, 0);
	TestEqual(TEXT("Exactly one stop per round"), T.Provider->StopCount, 3);
	TestEqual(TEXT("Round generations advance once"), T.Bridge->GetPhysicsContext().RoundGeneration, Initial.RoundGeneration + 3);
	TestTrue(TEXT("Source is stable across resets"), T.Bridge->GetPhysicsContext().SourceId == Initial.SourceId);
	FGuid FinalBone;
	TestTrue(TEXT("Install final view before clear"), T.InstallBone(FinalBone));
	ANormalBone* FinalView = T.Interaction->GetBoneActor(FinalBone);
	T.Interaction->ClearInteraction();
	TestEqual(TEXT("Interaction synchronously shuts physics down once"), T.Provider->ShutdownCount, 1);
	TestTrue(TEXT("Shutdown happens while old bodies are alive"), T.Provider->bAllBodiesAliveDuringShutdown);
	TestTrue(TEXT("View is destroyed after shutdown"), !IsValid(FinalView) || FinalView->IsActorBeingDestroyed());
	T.Interaction->ClearInteraction();
	T.Bridge->ClearBridge();
	T.Bridge->ShutdownPhysicsLifecycle();
	TestEqual(TEXT("Repeated teardown is idempotent"), T.Provider->ShutdownCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleReentryTest, "JGD2026.Assembly.Lifecycle.ReentryAndBindingCancellation", LifecycleTestFlags)
bool FAssemblyLifecycleReentryTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady)) { return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Install"), T.InstallBone(Bone))) { return false; }
	T.Provider->bRestartDuringPrepare = true;
	TestTrue(TEXT("Outer preparation succeeds despite rejected nested reset"), T.Model->TryStartPlaying(T.Reason));
	TestFalse(TEXT("Prepare callback cannot reset model reentrantly"), T.Provider->bNestedRestartSucceeded);
	TestEqual(TEXT("No nested reset removed bone"), T.Model->GetBones().Num(), 1);
	T.Provider->bRestartDuringPrepare = false;
	T.Provider->bRestartDuringStop = true;
	TestTrue(TEXT("Outer stop/reset succeeds"), T.Model->RestartAssembly(T.Reason));
	TestFalse(TEXT("Stop callback cannot recursively reset"), T.Provider->bNestedRestartSucceeded);
	TestEqual(TEXT("Nested stop did not duplicate provider call"), T.Provider->StopCount, 1);
	T.Provider->bRestartDuringStop = false;
	TestTrue(TEXT("Install second round"), T.InstallBone(Bone));
	T.Provider->bClearDuringPrepare = true;
	TestFalse(TEXT("Clearing binding in prepare invalidates pending start"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Canceled callback cannot commit Playing"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestEqual(TEXT("Canceled preparation aborted once"), T.Provider->AbortCount, 1);
	TestEqual(TEXT("Deferred bridge clear shuts provider once"), T.Provider->ShutdownCount, 1);
	TestFalse(TEXT("Canceled bridge is detached"), T.Bridge->IsBridgeInitialized());
	TestEqual(TEXT("Binding cancellation did not remove model instance"), T.Model->GetBones().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleCorePhasesTest, "JGD2026.Assembly.Lifecycle.SpacePlayingOnlyAndReliableRelease", LifecycleTestFlags)
bool FAssemblyLifecycleCorePhasesTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady) || !TestNotNull(TEXT("Message bus"), T.Bus)) { return false; }
	UAssemblyPhysicsTestReceiver* Receiver = T.ListenCore();
	const FTransform CoreBefore = T.Core->GetComponentTransform();
	const int32 InitialPhases = T.Provider->PhaseCount;
	TestFalse(TEXT("Assembly rejects Space without emitting a drive"), T.Model->SetCoreRotationInput(true));
	T.Model->SetCoreRotationInput(true);
	T.Model->SetCoreRotationInput(false);
	TestEqual(TEXT("Assembly press/duplicate/release emits zero core messages"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 0);
	TestFalse(TEXT("Assembly has no latched core input"), T.Model->IsCoreRotationHeld());
	TestFalse(TEXT("Assembly does not drive muscles"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("Start with physical core"), T.Model->TryStartPlaying(T.Reason));
	TestEqual(TEXT("Playing applies provider phase once before use"), T.Provider->PhaseCount, InitialPhases + 1);
	TestTrue(TEXT("Playing permits Space"), T.Model->SetCoreRotationInput(true));
	T.Model->SetCoreRotationInput(true);
	TestEqual(TEXT("Repeated held input emits one request"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 1);
	const FInstancedStruct* PlayingRaw = Receiver->Last(AssemblyMessageTags::CoreRotationRequested);
	const FAssemblyPhysicsCoreRotationMessage* PlayingRequest = PlayingRaw ? PlayingRaw->GetPtr<FAssemblyPhysicsCoreRotationMessage>() : nullptr;
	if (TestNotNull(TEXT("Playing core request has typed payload"), PlayingRequest))
	{
		TestTrue(TEXT("Playing request identifies driven phase"), PlayingRequest->Phase == EAssemblyPhase::Playing);
		TestTrue(TEXT("Same real core provided to executor"), PlayingRequest->CoreBody == T.Core);
	}
	TestTrue(TEXT("Playing release succeeds"), T.Model->SetCoreRotationInput(false));
	T.Model->SetCoreRotationInput(false);
	TestEqual(TEXT("Duplicate release is coalesced"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 2);
	T.Model->SetCoreRotationInput(true);
	T.Model->ClearInputState(); // The exact public release entry used for focus loss.
	TestEqual(TEXT("Focus-loss clearing sends the held release once"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 4);
	TestFalse(TEXT("Focus loss clears the latch"), T.Model->IsCoreRotationHeld());
	T.Model->ClearInputState();
	TestEqual(TEXT("Repeated focus loss adds no duplicate release"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 4);
	T.Model->SetCoreRotationInput(true);
	TestTrue(TEXT("Restart"), T.Model->RestartAssembly(T.Reason));
	TestEqual(TEXT("Restart sends one release before returning to Assembly"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 6);
	TestEqual(TEXT("Assembly phase is applied again after reset"), T.Provider->PhaseCount, InitialPhases + 2);
	TestFalse(TEXT("Held core released during restart"), T.Model->IsCoreRotationHeld());
	TestFalse(TEXT("Space remains rejected in the next Assembly round"), T.Model->SetCoreRotationInput(true));
	TestEqual(TEXT("No stale Assembly core press after reset"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 6);
	TestTrue(TEXT("Bridge never substitutes kinematic rotation"), CoreBefore.Equals(T.Core->GetComponentTransform()));
	TestFalse(TEXT("Bridge does not enable real simulation"), T.Core->IsSimulatingPhysics());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleProviderBindingTest, "JGD2026.Assembly.Lifecycle.ProviderWorldAndDiscovery", LifecycleTestFlags)
bool FAssemblyLifecycleProviderBindingTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T(false);
	FLifecycleWorld Other;
	if (!TestTrue(TEXT("Both fixtures initialize"), T.bReady && Other.bReady)) { return false; }
	TestFalse(TEXT("Provider must implement lifecycle interface"), T.Bridge->SetPhysicsLifecycleProvider(T.Core, T.Reason));
	TestFalse(TEXT("Cross-world provider is rejected"), T.Bridge->SetPhysicsLifecycleProvider(Other.Provider, T.Reason));
	T.Provider = T.AddProvider();
	TestTrue(TEXT("One local executor may be discovered"), T.Bridge->RefreshPhysicsLifecycle(T.Reason));
	TestTrue(TEXT("Discovery finds owner component"), T.Bridge->GetPhysicsLifecycleProvider() == T.Provider);
	TestTrue(TEXT("Discovered executor satisfies required setting"), T.Bridge->HasPhysicsLifecycleProvider());
	TestTrue(TEXT("Playing with unique provider succeeds"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Reset"), T.Model->RestartAssembly(T.Reason));
	T.AddProvider();
	TestFalse(TEXT("Multiple automatic executors are ambiguous"), T.Bridge->RefreshPhysicsLifecycle(T.Reason));
	TestFalse(TEXT("Ambiguous executor cannot start the model"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Explicit binding is valid"), T.Bridge->SetPhysicsLifecycleProvider(T.Provider, T.Reason));
	TestTrue(TEXT("Repeating the same binding is idempotent"), T.Bridge->SetPhysicsLifecycleProvider(T.Provider, T.Reason));
	TestTrue(TEXT("Explicit choice resolves automatic ambiguity"), T.Model->TryStartPlaying(T.Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleActivationFailureTest, "JGD2026.Assembly.Lifecycle.ActivationFailureRestoresEditableSnapshot", LifecycleTestFlags)
bool FAssemblyLifecycleActivationFailureTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Install connected view"), T.InstallBone(Bone))) { return false; }
	ANormalBone* View = T.Interaction->GetBoneActor(Bone);
	const FAssemblyBoneInstance BeforeBone = T.Model->GetBones()[0];
	const TArray<FAssemblyJointInstance> BeforeJoints = T.Model->GetJoints();
	const FTransform BeforeFrame = T.Model->GetGridFrameTransform();
	const int32 BeforeStock = T.Model->GetRemainingQuantity(TEXT("Bone"));
	const FAssemblyPhysicsContext BeforeContext = T.Bridge->GetPhysicsContext();
	T.Model->OnPhaseChanged.AddDynamic(T.Provider, &UAssemblyLifecycleTestProvider::HandlePhaseChanged);
	T.Provider->bPlayingApplySucceeds = false;
	T.Provider->bReplacePoseDuringPrepare = true;
	T.Provider->bReplaceCoreDuringPrepare = true;
	TestFalse(TEXT("Prepared executor must also confirm actual activation"), T.Model->TryStartPlaying(T.Reason));
	TestFalse(TEXT("Activation rejection provides a reason"), T.Reason.IsEmpty());
	TestEqual(TEXT("Preparation reached the actual activation step"), T.Provider->PlayingApplyCount, 1);
	TestEqual(TEXT("Failed activation aborts exactly once"), T.Provider->AbortCount, 1);
	TestEqual(TEXT("Rejected activation is never advertised as Playing"), T.Provider->PlayingNotificationCount, 0);
	TestTrue(TEXT("Failed activation restores Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestEqual(TEXT("Bone inventory is preserved"), T.Model->GetRemainingQuantity(TEXT("Bone")), BeforeStock);
	TestEqual(TEXT("Installed bone count is preserved"), T.Model->GetBones().Num(), 1);
	TestTrue(TEXT("Bone identity and editable placement are restored"), T.Model->GetBones()[0].InstanceId == Bone
		&& T.Model->GetBones()[0].LocalVisualTransform.Equals(BeforeBone.LocalVisualTransform));
	TestEqual(TEXT("No connection is lost or duplicated"), T.Model->GetJoints().Num(), BeforeJoints.Num());
	for (int32 Index = 0; Index < BeforeJoints.Num(); ++Index)
	{
		TestTrue(TEXT("Original connection identity is restored"), T.Model->GetJoints()[Index].InstanceId == BeforeJoints[Index].InstanceId);
	}
	TestTrue(TEXT("Original frame remains stable"), T.Model->GetGridFrameTransform().Equals(BeforeFrame));
	TestTrue(TEXT("Original view actor survives"), IsValid(View) && !View->IsActorBeingDestroyed());
	TestTrue(TEXT("Temporary body replacement is rolled back"), T.Model->GetBonePoseSource(Bone) == View->GetBoneBody());
	FAssemblyPhysicsSnapshotMessage Snapshot;
	TestTrue(TEXT("Rollback has a readable physics snapshot"), T.Bridge->GetPhysicsSnapshot(Snapshot));
	TestTrue(TEXT("Original core is restored"), Snapshot.CoreBody == T.Core);
	TestNull(TEXT("Aborted temporary body is released"), T.Provider->TemporaryBody.Get());
	TestTrue(TEXT("Source identity is not replaced on failure"), T.Bridge->GetPhysicsContext().SourceId == BeforeContext.SourceId);
	TestEqual(TEXT("Failure does not advance the round"), T.Bridge->GetPhysicsContext().RoundGeneration, BeforeContext.RoundGeneration);
	TestTrue(TEXT("Bone is still editable after rollback"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Bone, T.Reason));
	T.Interaction->CancelBoneDrag();
	T.Provider->bReplacePoseDuringPrepare = false;
	T.Provider->bReplaceCoreDuringPrepare = false;
	T.Provider->bPlayingApplySucceeds = true;
	TestTrue(TEXT("Retry activates the restored assembly"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Repeated start keeps the committed token finalized"), T.Model->TryStartPlaying(T.Reason));
	TestEqual(TEXT("Only one activation per attempt"), T.Provider->PlayingApplyCount, 2);
	TestEqual(TEXT("Successful retry adds no abort"), T.Provider->AbortCount, 1);
	TestEqual(TEXT("Only confirmed retry is advertised as Playing"), T.Provider->PlayingNotificationCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleDefaultApplyTest, "JGD2026.Assembly.Lifecycle.UnimplementedActivationCannotClaimSuccess", LifecycleTestFlags)
bool FAssemblyLifecycleDefaultApplyTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady)) { return false; }
	T.Provider->bUseDefaultPlayingApply = true;
	TestFalse(TEXT("Interface default cannot silently approve unimplemented activation"), T.Model->TryStartPlaying(T.Reason));
	TestFalse(TEXT("Missing phase implementation explains rejection"), T.Reason.IsEmpty());
	TestTrue(TEXT("Unimplemented phase remains Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
	TestEqual(TEXT("Preparation occurred once"), T.Provider->PrepareCount, 1);
	TestEqual(TEXT("Default confirmation was called once"), T.Provider->PlayingApplyCount, 1);
	TestEqual(TEXT("Unconfirmed prepared resources are aborted once"), T.Provider->AbortCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleAssemblyApplyCleanupTest, "JGD2026.Assembly.Lifecycle.FailedAssemblyApplyShutsPartialResourcesDown", LifecycleTestFlags)
bool FAssemblyLifecycleAssemblyApplyCleanupTest::RunTest(const FString& Parameters)
{
	for (int32 Entry = 0; Entry < 2; ++Entry)
	{
		FLifecycleWorld T;
		if (!TestTrue(TEXT("Cleanup fixture initializes"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		FGuid Bone;
		if (!TestTrue(TEXT("Install editable part before Assembly activation failure"), T.InstallBone(Bone))) { return false; }
		const int32 Stock = T.Model->GetRemainingQuantity(TEXT("Bone"));
		USceneComponent* OriginalPose = T.Model->GetBonePoseSource(Bone);
		if (Entry == 0)
		{
			T.Bridge->ClearBridge();
			T.Provider->ShutdownCount = 0;
			TestTrue(TEXT("Explicit provider may be assigned before bridge initialization"), T.Bridge->SetPhysicsLifecycleProvider(T.Provider, T.Reason));
		}
		T.Provider->bAssemblyApplySucceeds = false;
		// Model a real executor that allocates resources before discovering a late failure.
		// A logical rejection is not proof that its physical resources have already stopped.
		T.Provider->bAllocateResourcesDuringAssemblyApply = true;
		const bool bAccepted = Entry == 0
			? T.Bridge->InitializeBridge(T.Model, T.Core, T.Reason)
			: T.Bridge->RefreshPhysicsLifecycle(T.Reason);
		TestFalse(TEXT("Assembly phase failure rejects initialization or refresh"), bAccepted);
		TestFalse(TEXT("Assembly phase failure provides a reason"), T.Reason.IsEmpty());
		TestEqual(TEXT("Failed Assembly activation requires one full shutdown"), T.Provider->ShutdownCount, 1);
		TestFalse(TEXT("Shutdown actually releases the partial executor resource"), T.Provider->bExecutorResourcesActive);
		TestEqual(TEXT("Assembly activation created no preparation token to abort"), T.Provider->AbortCount, 0);
		TestEqual(TEXT("Assembly phase failure never calls Playing preparation"), T.Provider->PrepareCount, 0);
		TestTrue(TEXT("Failed initialization or refresh keeps Assembly stable"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
		TestEqual(TEXT("Failure keeps installed inventory"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
		TestTrue(TEXT("Failure preserves editable body registration"), T.Model->GetBonePoseSource(Bone) == OriginalPose);
		T.Bridge->ShutdownPhysicsLifecycle();
		TestEqual(TEXT("Repeated explicit shutdown is idempotent"), T.Provider->ShutdownCount, 1);
		T.Provider->bAssemblyApplySucceeds = true;
		T.Provider->bAllocateResourcesDuringAssemblyApply = false;
		const bool bRecovered = Entry == 0
			? T.Bridge->InitializeBridge(T.Model, T.Core, T.Reason)
			: T.Bridge->RefreshPhysicsLifecycle(T.Reason);
		TestTrue(TEXT("After executor cleanup and repair, stable Assembly can activate"), bRecovered);
		TestTrue(TEXT("Retry does not leave a partial resource"), !T.Provider->bExecutorResourcesActive);
		TestEqual(TEXT("Retry retains exactly one editable bone"), T.Model->GetBones().Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleApplyReentryTest, "JGD2026.Assembly.Lifecycle.ActivationCallbackReentryAndCancellation", LifecycleTestFlags)
bool FAssemblyLifecycleApplyReentryTest::RunTest(const FString& Parameters)
{
	{
		FLifecycleWorld T;
		if (!TestTrue(TEXT("Reentry fixture initializes"), T.bReady)) { return false; }
		FGuid Bone;
		TestTrue(TEXT("Install before callback reentry"), T.InstallBone(Bone));
		T.Provider->bRestartDuringPlayingApply = true;
		T.Provider->bRequestInputDuringPlayingApply = true;
		TestTrue(TEXT("Outer activation succeeds while nested edits are rejected"), T.Model->TryStartPlaying(T.Reason));
		TestFalse(TEXT("Activation callback cannot restart the provisional round"), T.Provider->bNestedRestartSucceeded);
		TestFalse(TEXT("Activation callback cannot press provisional core input"), T.Provider->bCoreInputAcceptedDuringPlayingApply);
		TestFalse(TEXT("Activation callback cannot press provisional muscle input"), T.Provider->bMuscleInputAcceptedDuringPlayingApply);
		TestEqual(TEXT("Nested callback did not reset the bone"), T.Model->GetBones().Num(), 1);
		TestEqual(TEXT("Activation callback occurs once"), T.Provider->PlayingApplyCount, 1);
		TestEqual(TEXT("Rejected nested restart did not invoke reset executor"), T.Provider->StopCount, 0);
	}
	for (int32 Cancellation = 0; Cancellation < 2; ++Cancellation)
	{
		FLifecycleWorld T;
		if (!TestTrue(TEXT("Cancellation fixture initializes"), T.bReady)) { return false; }
		FGuid Bone;
		TestTrue(TEXT("Install before callback cancellation"), T.InstallBone(Bone));
		const int32 Stock = T.Model->GetRemainingQuantity(TEXT("Bone"));
		T.Provider->bClearDuringPlayingApply = Cancellation == 0;
		T.Provider->bDestroyDuringPlayingApply = Cancellation == 1;
		TestFalse(TEXT("Changing bridge/provider during activation cancels success"), T.Model->TryStartPlaying(T.Reason));
		TestFalse(TEXT("Canceled activation has a reason"), T.Reason.IsEmpty());
		TestTrue(TEXT("Canceled activation returns to Assembly"), T.Model->GetPhase() == EAssemblyPhase::Assembly);
		TestEqual(TEXT("Canceled callback preserves installed identity"), T.Model->GetBones().Num(), 1);
		TestEqual(TEXT("Canceled callback preserves inventory"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
		TestFalse(TEXT("Canceled callback leaves no held core input"), T.Model->IsCoreRotationHeld());
		if (Cancellation == 0)
		{
			TestEqual(TEXT("Bridge cancellation aborts prepared activation once"), T.Provider->AbortCount, 1);
			TestEqual(TEXT("Deferred bridge shutdown occurs once"), T.Provider->ShutdownCount, 1);
			TestFalse(TEXT("Deferred clear detaches the bridge"), T.Bridge->IsBridgeInitialized());
		}
		else { TestFalse(TEXT("Destroyed provider is not reported as usable"), T.Bridge->HasPhysicsLifecycleProvider()); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyLifecycleEndPlayReleaseTest, "JGD2026.Assembly.Lifecycle.EndPlayReleasesHeldDrivesBeforeShutdown", LifecycleTestFlags)
bool FAssemblyLifecycleEndPlayReleaseTest::RunTest(const FString& Parameters)
{
	FLifecycleWorld T;
	if (!TestTrue(TEXT("Fixture initializes"), T.bReady)) { return false; }
	T.World->BeginPlay();
	UAssemblyPhysicsTestReceiver* Receiver = T.ListenCore();
	FGuid Bone;
	TestTrue(TEXT("Install body before EndPlay"), T.InstallBone(Bone));
	TestTrue(TEXT("Confirmed activation before EndPlay"), T.Model->TryStartPlaying(T.Reason));
	TestTrue(TEXT("Hold core while playing"), T.Model->SetCoreRotationInput(true));
	TestEqual(TEXT("One held request before ending"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 1);
	T.World->EndPlay(EEndPlayReason::Quit);
	TestEqual(TEXT("EndPlay releases held core exactly once"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), 2);
	const FInstancedStruct* Raw = Receiver->Last(AssemblyMessageTags::CoreRotationRequested);
	const FAssemblyPhysicsCoreRotationMessage* Request = Raw ? Raw->GetPtr<FAssemblyPhysicsCoreRotationMessage>() : nullptr;
	if (TestNotNull(TEXT("Final core message remains typed"), Request)) { TestFalse(TEXT("Last message stops angular drive"), Request->bHeld); }
	TestEqual(TEXT("EndPlay releases physics lifecycle once"), T.Provider->ShutdownCount, 1);
	TestFalse(TEXT("EndPlay leaves no held core state"), T.Model->IsCoreRotationHeld());
	TestFalse(TEXT("Ended model rejects a new drive"), T.Model->SetCoreRotationInput(true));
	return true;
}

#endif
