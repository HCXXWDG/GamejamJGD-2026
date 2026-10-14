#include "AssemblyPhysicsTestReceiver.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblySessionComponent.h"

void UAssemblyPhysicsTestReceiver::Receive(FGameplayTag Channel, const FInstancedStruct& Message)
{
	Channels.Add(Channel);
	Messages.Add(Message);
	if (bTriggered) { return; }
	if (const FAssemblyPhysicsCoreRotationMessage* Core = Message.GetPtr<FAssemblyPhysicsCoreRotationMessage>(); Core && Core->bHeld
		&& (bClearInputOnHeldCore || bClearBridgeOnHeldCore))
	{
		bTriggered = true;
		if (bClearBridgeOnHeldCore && Bridge) { Bridge->ClearBridge(); }
		if (Model) { Model->ClearInputState(); }
	}
	if (const FAssemblyPhysicsMuscleDriveMessage* Drive = Message.GetPtr<FAssemblyPhysicsMuscleDriveMessage>(); Drive && Drive->bContracting
		&& (bClearInputOnContractingMuscle || bShutdownSessionOnContractingMuscle))
	{
		bTriggered = true;
		if (bShutdownSessionOnContractingMuscle && Session) { Session->ShutdownSession(); }
		else if (Model) { Model->ClearInputState(); }
	}
}

int32 UAssemblyPhysicsTestReceiver::Count(FGameplayTag Channel) const
{
	int32 Result = 0;
	for (FGameplayTag Value : Channels)
	{
		Result += Value == Channel ? 1 : 0;
	}
	return Result;
}

const FInstancedStruct* UAssemblyPhysicsTestReceiver::Last(FGameplayTag Channel) const
{
	for (int32 Index = Channels.Num() - 1; Index >= 0; --Index)
	{
		if (Channels[Index] == Channel)
		{
			return &Messages[Index];
		}
	}
	return nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr EAutomationTestFlags BridgeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	struct FBridgeTestWorld
	{
		UGameInstance* GameInstance;
		UWorld* World;
		AActor* Owner;
		UAssemblyComponent* Assembly;
		UAssemblyPhysicsBridgeComponent* Bridge;
		UBoxComponent* Core;
		UJGDMessageSubsystem* Bus;
		FBridgeTestWorld()
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone(FName(*FString::Printf(TEXT("AssemblyBridgeTest_%s"), *FGuid::NewGuid().ToString())));
			World = GameInstance->GetWorld();
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			Core = NewObject<UBoxComponent>(Owner);
			Owner->AddInstanceComponent(Core);
			Owner->SetRootComponent(Core);
			Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Core->RegisterComponent();
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
			Bridge = NewObject<UAssemblyPhysicsBridgeComponent>(Owner);
			Owner->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			Bus = UJGDMessageSubsystem::Get(Assembly);
			FText Reason;
			Assembly->ConfigureAssembly(Assembly->DefaultGrid, Assembly->DefaultBones, Assembly->DefaultMuscles, Reason);
		}
		~FBridgeTestWorld()
		{
			Bridge->ClearBridge();
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}
		UAssemblyPhysicsTestReceiver* Listen()
		{
			UAssemblyPhysicsTestReceiver* Receiver = NewObject<UAssemblyPhysicsTestReceiver>(GameInstance);
			FJGDMessageReceived Delegate;
			Delegate.BindDynamic(Receiver, &UAssemblyPhysicsTestReceiver::Receive);
			const FGameplayTag Tags[] = {
				AssemblyMessageTags::AssemblyChanged, AssemblyMessageTags::AssemblyReset,
				AssemblyMessageTags::AssemblyPhaseChanged, AssemblyMessageTags::PhysicsRebuildRequested,
				AssemblyMessageTags::MuscleDriveRequested, AssemblyMessageTags::CoreRotationRequested,
				AssemblyMessageTags::JointBreakRequested
			};
			for (FGameplayTag Tag : Tags)
			{
				Bus->RegisterListener(Tag, Delegate);
			}
			return Receiver;
		}
		void TickBridge()
		{
			Bridge->TickComponent(0.016f, LEVELTICK_All, nullptr);
		}
	};
	FAssemblyMuscleEndpoint MakeEndpoint(FGuid Bone, double X)
	{
		FAssemblyMuscleEndpoint Endpoint;
		Endpoint.BoneId = Bone;
		Endpoint.BoneLocalPoint = FVector2D(X, 0);
		return Endpoint;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyTagBridgeTest, "JGD2026.Assembly.PhysicsBridge.TypedFanoutAndLifecycle", BridgeTestFlags)
bool FAssemblyTagBridgeTest::RunTest(const FString& Parameters)
{
	FBridgeTestWorld Fixture;
	if (!TestNotNull(TEXT("Runtime GameInstance owns the existing message bus"), Fixture.Bus))
	{
		return false;
	}
	UAssemblyPhysicsTestReceiver* First = Fixture.Listen();
	UAssemblyPhysicsTestReceiver* Second = Fixture.Listen();
	FText Reason;
	FGuid Bone, MuscleA, MuscleB;
	TestTrue(TEXT("Install existing grid bone for backward-compatible message test"),
		Fixture.Assembly->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, Bone, Reason));
	TestTrue(TEXT("Select W"), Fixture.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestTrue(TEXT("Install first W muscle"), Fixture.Assembly->TryInstallMuscle(TEXT("Muscle"), MakeEndpoint(Bone, -20), MakeEndpoint(Bone, 20), MuscleA, Reason));
	TestTrue(TEXT("Install second W muscle"), Fixture.Assembly->TryInstallMuscle(TEXT("Muscle"), MakeEndpoint(Bone, -10), MakeEndpoint(Bone, 10), MuscleB, Reason));
	const int32 BoneStock = Fixture.Assembly->GetRemainingQuantity(TEXT("Bone"));
	const int32 MuscleStock = Fixture.Assembly->GetRemainingQuantity(TEXT("Muscle"));
	TestTrue(TEXT("Initialize bridge"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason));
	const FAssemblyPhysicsContext OriginalContext = Fixture.Bridge->GetPhysicsContext();
	TestTrue(TEXT("Native source identifier valid"), OriginalContext.SourceId.IsValid());
	const int32 InitialEvents = First->Channels.Num();
	TestTrue(TEXT("Same initialization is idempotent"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason));
	TestEqual(TEXT("Repeated initialization emits no additional event"), First->Channels.Num(), InitialEvents);
	UAssemblyPhysicsBridgeComponent* Duplicate = NewObject<UAssemblyPhysicsBridgeComponent>(Fixture.Owner);
	Fixture.Owner->AddInstanceComponent(Duplicate);
	Duplicate->RegisterComponent();
	TestFalse(TEXT("Second bridge for same model rejected"), Duplicate->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason));
	TestFalse(TEXT("Duplicate failure explains the cause"), Reason.IsEmpty());
	TestFalse(TEXT("Core intent rejected while assembling"), Fixture.Assembly->SetCoreRotationInput(true));
	Fixture.Assembly->SetCoreRotationInput(false);
	TestEqual(TEXT("Rejected Assembly input emits no drive edges"), First->Count(AssemblyMessageTags::CoreRotationRequested), 0);
	const FTransform OriginalCoreTransform = Fixture.Core->GetComponentTransform();
	TestTrue(TEXT("Start Playing"), Fixture.Assembly->TryStartPlaying(Reason));
	TestTrue(TEXT("Playing permits core press"), Fixture.Assembly->SetCoreRotationInput(true));
	Fixture.Assembly->SetCoreRotationInput(false);
	TestEqual(TEXT("Both valid Playing core edges are forwarded"), First->Count(AssemblyMessageTags::CoreRotationRequested), 2);
	TestTrue(TEXT("Press W contracts two bound instances"), Fixture.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true));
	Fixture.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true);
	TestEqual(TEXT("Held duplicate is coalesced"), First->Count(AssemblyMessageTags::MuscleDriveRequested), 2);
	TestTrue(TEXT("Release W relaxes two bound instances"), Fixture.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, false));
	Fixture.Assembly->SetMuscleInput(EAssemblyMuscleKey::A, true);
	TestEqual(TEXT("Unbound A emits no muscle requests"), First->Count(AssemblyMessageTags::MuscleDriveRequested), 4);
	TestEqual(TEXT("Two independent listeners both receive every message"), First->Channels.Num(), Second->Channels.Num());
	const FInstancedStruct* LastDrive = First->Last(AssemblyMessageTags::MuscleDriveRequested);
	const FAssemblyPhysicsMuscleDriveMessage* Drive = LastDrive ? LastDrive->GetPtr<FAssemblyPhysicsMuscleDriveMessage>() : nullptr;
	if (TestNotNull(TEXT("Drive has fixed typed payload"), Drive))
	{
		TestFalse(TEXT("Release requests relaxation"), Drive->bContracting);
		TestFalse(TEXT("Instance intent agrees with message"), Drive->Muscle.bContracting);
		TestTrue(TEXT("Bindings carried in payload"), Drive->Muscle.BindingKey == EAssemblyMuscleKey::W);
		TestTrue(TEXT("Same assembly source"), Drive->Context.Assembly == Fixture.Assembly && Drive->Context.SourceId == OriginalContext.SourceId);
		TestTrue(TEXT("Same runtime world"), Drive->Context.World == Fixture.World);
	}
	TestEqual(TEXT("Physics request does not consume bone stock"), Fixture.Assembly->GetRemainingQuantity(TEXT("Bone")), BoneStock);
	TestEqual(TEXT("Physics request does not consume muscle stock"), Fixture.Assembly->GetRemainingQuantity(TEXT("Muscle")), MuscleStock);
	TestTrue(TEXT("Bridge never moves core to simulate rotation"), Fixture.Core->GetComponentTransform().Equals(OriginalCoreTransform));
	TestFalse(TEXT("Bridge never enables simulation"), Fixture.Core->IsSimulatingPhysics());
	Fixture.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true);
	Fixture.Assembly->SetCoreRotationInput(true);
	const int32 BeforeResetDrive = First->Count(AssemblyMessageTags::MuscleDriveRequested);
	const int32 BeforeResetCore = First->Count(AssemblyMessageTags::CoreRotationRequested);
	TestTrue(TEXT("Restart restores Assembly"), Fixture.Assembly->RestartAssembly(Reason));
	TestEqual(TEXT("Restart releases both muscle requests"), First->Count(AssemblyMessageTags::MuscleDriveRequested), BeforeResetDrive + 2);
	TestEqual(TEXT("Restart releases held core"), First->Count(AssemblyMessageTags::CoreRotationRequested), BeforeResetCore + 1);
	TestEqual(TEXT("Restart advances round epoch"), Fixture.Bridge->GetPhysicsContext().RoundGeneration, OriginalContext.RoundGeneration + 1);
	TestTrue(TEXT("Restart keeps source identifier"), Fixture.Bridge->GetPhysicsContext().SourceId == OriginalContext.SourceId);
	TestTrue(TEXT("Restart inventory invariant"), Fixture.Assembly->ValidateAssembly().bValid);
	Fixture.Bridge->ClearBridge();
	const int32 EventsAfterClear = First->Channels.Num();
	Fixture.Bridge->ClearBridge();
	Fixture.Assembly->SetCoreRotationInput(true);
	Fixture.Assembly->RestartAssembly(Reason);
	TestEqual(TEXT("Clear removes model listeners and is safe twice"), First->Channels.Num(), EventsAfterClear);
	TestFalse(TEXT("Clear unregisters physics confirmation listener"), Fixture.Bus->HasListeners(AssemblyMessageTags::JointBroken));
	TestTrue(TEXT("Bridge can bind again"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason));
	TestTrue(TEXT("Rebinding gives a fresh source identifier"), Fixture.Bridge->GetPhysicsContext().SourceId != OriginalContext.SourceId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyTagBridgePoseTest, "JGD2026.Assembly.PhysicsBridge.RegisteredPoseAndSourceSwitch", BridgeTestFlags)
bool FAssemblyTagBridgePoseTest::RunTest(const FString& Parameters)
{
	FBridgeTestWorld Fixture;
	UAssemblyPhysicsTestReceiver* Receiver = Fixture.Listen();
	FText Reason;
	TestTrue(TEXT("Initialize"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason));
	FGuid Bone;
	TestTrue(TEXT("Install"), Fixture.Assembly->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, Bone, Reason));
	UBoxComponent* Body = NewObject<UBoxComponent>(Fixture.Owner);
	Fixture.Owner->AddInstanceComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->RegisterComponent();
	FTransform BoneTransform;
	Fixture.Assembly->GetBoneWorldTransform(Bone, BoneTransform);
	Body->SetWorldTransform(BoneTransform);
	TestTrue(TEXT("Register physical pose source"), Fixture.Assembly->RegisterBonePoseSource(Bone, Body, Reason));
	Fixture.Bridge->RequestPhysicsRebuild();
	Fixture.Bridge->RequestPhysicsRebuild();
	Fixture.TickBridge();
	TestEqual(TEXT("Multiple edits/rebuild requests coalesce"), Receiver->Count(AssemblyMessageTags::PhysicsRebuildRequested), 1);
	const FInstancedStruct* Raw = Receiver->Last(AssemblyMessageTags::PhysicsRebuildRequested);
	const FAssemblyPhysicsSnapshotMessage* Snapshot = Raw ? Raw->GetPtr<FAssemblyPhysicsSnapshotMessage>() : nullptr;
	if (TestNotNull(TEXT("Snapshot has fixed schema"), Snapshot))
	{
		TestEqual(TEXT("Every bone contributes a pose"), Snapshot->BonePoses.Num(), 1);
		if (Snapshot->BonePoses.Num() == 1)
		{
			TestTrue(TEXT("Snapshot includes registered physical body"), Snapshot->BonePoses[0].Body == Body);
			TestTrue(TEXT("Snapshot includes actual world pose"), Snapshot->BonePoses[0].WorldTransform.Equals(BoneTransform));
		}
		TestEqual(TEXT("Inventory remains in the model, bones are identities"), Snapshot->Bones.Num(), 1);
		TestTrue(TEXT("Core body preserved"), Snapshot->CoreBody == Fixture.Core);
	}
	UAssemblyComponent* Other = NewObject<UAssemblyComponent>(Fixture.Owner);
	Fixture.Owner->AddInstanceComponent(Other);
	Other->RegisterComponent();
	Other->ConfigureAssembly(Other->DefaultGrid, Other->DefaultBones, Other->DefaultMuscles, Reason);
	const FGuid OldSource = Fixture.Bridge->GetPhysicsContext().SourceId;
	TestTrue(TEXT("Explicit source switch succeeds"), Fixture.Bridge->InitializeBridge(Other, Fixture.Core, Reason));
	TestTrue(TEXT("Source switch has new identity"), Fixture.Bridge->GetPhysicsContext().SourceId != OldSource);
	TestTrue(TEXT("Old model may still change its own input in Playing"), Fixture.Assembly->TryStartPlaying(Reason));
	TestTrue(TEXT("New model enters Playing before drive test"), Other->TryStartPlaying(Reason));
	const int32 Count = Receiver->Channels.Num();
	Fixture.Assembly->SetCoreRotationInput(true);
	TestEqual(TEXT("Old model no longer forwards input"), Receiver->Channels.Num(), Count);
	Other->SetCoreRotationInput(true);
	TestEqual(TEXT("New model forwards once"), Receiver->Channels.Num(), Count + 1);
	Fixture.Bridge->DestroyComponent();
	const int32 AfterDestroy = Receiver->Channels.Num();
	Other->SetCoreRotationInput(false);
	TestEqual(TEXT("Component destruction unregisters model delegates"), Receiver->Channels.Num(), AfterDestroy);
	TestFalse(TEXT("Component destruction unregisters bus"), Fixture.Bus->HasListeners(AssemblyMessageTags::JointBroken));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyTagNativeCoreReleaseTest, "JGD2026.Assembly.PhysicsBridge.NativeCoreStopCannotBeOverwrittenByOuterPress", BridgeTestFlags)
bool FAssemblyTagNativeCoreReleaseTest::RunTest(const FString& Parameters)
{
	FBridgeTestWorld T;
	FText Reason;
	if (!TestTrue(TEXT("Bridge initializes"), T.Bridge->InitializeBridge(T.Assembly, T.Core, Reason))
		|| !TestTrue(TEXT("Enter Playing"), T.Assembly->TryStartPlaying(Reason))) { AddError(Reason.ToString()); return false; }
	UAssemblyPhysicsTestReceiver* Receiver = T.Listen();
	bool bNativeStoppedCore = false;
	const FDelegateHandle Handle = T.Assembly->OnCoreRotationRequestedNative.AddLambda([&](bool bHeld)
	{
		if (bHeld)
		{
			bNativeStoppedCore = T.Assembly->SetCoreRotationInput(false);
		}
	});
	TestTrue(TEXT("Outer press is accepted before its synchronous listener stops it"), T.Assembly->SetCoreRotationInput(true));
	TestTrue(TEXT("Native listener immediately releases core"), bNativeStoppedCore);
	TestFalse(TEXT("Native stop is authoritative after outer press unwinds"), T.Assembly->IsCoreRotationHeld());
	const FInstancedStruct* Raw = Receiver->Last(AssemblyMessageTags::CoreRotationRequested);
	const FAssemblyPhysicsCoreRotationMessage* Last = Raw ? Raw->GetPtr<FAssemblyPhysicsCoreRotationMessage>() : nullptr;
	if (TestNotNull(TEXT("A typed core stop reaches physics"), Last)) { TestFalse(TEXT("Final typed request is stop, never the stale outer press"), Last->bHeld); }
	bool bSawStop = false;
	for (const FInstancedStruct& Message : Receiver->Messages)
	{
		if (const FAssemblyPhysicsCoreRotationMessage* Core = Message.GetPtr<FAssemblyPhysicsCoreRotationMessage>())
		{
			if (!Core->bHeld) { bSawStop = true; }
			else { TestFalse(TEXT("No stale held request may follow the native stop"), bSawStop); }
		}
	}
	TestTrue(TEXT("The receiver recorded a real stop"), bSawStop);
	const int32 CountBeforeRefresh = Receiver->Count(AssemblyMessageTags::CoreRotationRequested);
	TestTrue(TEXT("Healthy bridge can refresh after immediate stop"), T.Bridge->RefreshPhysicsLifecycle(Reason));
	TestEqual(TEXT("Refresh cannot replay the stopped core"), Receiver->Count(AssemblyMessageTags::CoreRotationRequested), CountBeforeRefresh);
	T.Assembly->OnCoreRotationRequestedNative.Remove(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyTagNativeMuscleClearTest, "JGD2026.Assembly.PhysicsBridge.NativeClearReleasesEverySameKeyMuscle", BridgeTestFlags)
bool FAssemblyTagNativeMuscleClearTest::RunTest(const FString& Parameters)
{
	FBridgeTestWorld T;
	FText Reason;
	FGuid Bone, MuscleA, MuscleB;
	if (!TestTrue(TEXT("Install connected test bone"), T.Assembly->TryInstallBone(TEXT("Bone"), FIntPoint(3,2), 0, Bone, Reason))) { return false; }
	TestTrue(TEXT("Select W"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestTrue(TEXT("Install first W muscle"), T.Assembly->TryInstallMuscle(TEXT("Muscle"), MakeEndpoint(Bone,-20), MakeEndpoint(Bone,20), MuscleA, Reason));
	TestTrue(TEXT("Install second W muscle"), T.Assembly->TryInstallMuscle(TEXT("Muscle"), MakeEndpoint(Bone,-10), MakeEndpoint(Bone,10), MuscleB, Reason));
	if (!TestTrue(TEXT("Bridge initializes"), T.Bridge->InitializeBridge(T.Assembly, T.Core, Reason))
		|| !TestTrue(TEXT("Enter Playing"), T.Assembly->TryStartPlaying(Reason))) { return false; }
	UAssemblyPhysicsTestReceiver* Receiver = T.Listen();
	int32 NativePressCount = 0;
	const FDelegateHandle Handle = T.Assembly->OnMuscleDriveRequestedNative.AddLambda([&](FGuid Id, bool bContracting)
	{
		if (bContracting)
		{
			++NativePressCount;
			T.Assembly->ClearInputState();
		}
	});
	TestTrue(TEXT("Outer W press is accepted"), T.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestEqual(TEXT("Stale second same-key true never reaches native observers"), NativePressCount, 1);
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
		TestTrue(TEXT("Native clearing preserves each installed muscle"), T.Assembly->GetMuscle(Id, Muscle));
		TestFalse(TEXT("Every same-key muscle ends relaxed"), Muscle.bContracting);
		TestTrue(TEXT("Each same-key muscle has a final typed stop"), LastIntent.Contains(Id) && !LastIntent[Id]);
	}
	TestEqual(TEXT("Clearing does not return or duplicate muscle stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 1);
	const int32 CountBeforeRefresh = Receiver->Count(AssemblyMessageTags::MuscleDriveRequested);
	TestTrue(TEXT("Refresh after native clearing succeeds"), T.Bridge->RefreshPhysicsLifecycle(Reason));
	TestEqual(TEXT("Refresh cannot replay cleared muscle contraction"), Receiver->Count(AssemblyMessageTags::MuscleDriveRequested), CountBeforeRefresh);
	T.Assembly->OnMuscleDriveRequestedNative.Remove(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyTagTypedCoreFanoutTest, "JGD2026.Assembly.PhysicsBridge.TypedCoreFanoutPreservesStopAfterReentrantClear", BridgeTestFlags)
bool FAssemblyTagTypedCoreFanoutTest::RunTest(const FString& Parameters)
{
	for (int32 ClearMode = 0; ClearMode < 2; ++ClearMode)
	{
		FBridgeTestWorld T;
		FText Reason;
		if (!TestTrue(TEXT("Bridge initializes"), T.Bridge->InitializeBridge(T.Assembly, T.Core, Reason))
			|| !TestTrue(TEXT("Enter Playing"), T.Assembly->TryStartPlaying(Reason))) { AddError(Reason.ToString()); return false; }
		const FAssemblyPhysicsContext OriginalContext = T.Bridge->GetPhysicsContext();
		UAssemblyPhysicsTestReceiver* First = T.Listen();
		UAssemblyPhysicsTestReceiver* Second = T.Listen();
		First->Model = T.Assembly;
		First->Bridge = T.Bridge;
		First->bClearInputOnHeldCore = ClearMode == 0;
		First->bClearBridgeOnHeldCore = ClearMode == 1;
		TestTrue(TEXT("Outer core press reaches synchronous typed receivers"), T.Assembly->SetCoreRotationInput(true));
		TestTrue(TEXT("First typed receiver really canceled the held drive"), First->bTriggered);
		TestFalse(TEXT("Reentrant typed receiver leaves no held model core state"), T.Assembly->IsCoreRotationHeld());
		TestEqual(TEXT("Second receiver receives the original press and final release exactly once"), Second->Count(AssemblyMessageTags::CoreRotationRequested), 2);
		TArray<bool> Edges;
		for (const FInstancedStruct& Message : Second->Messages)
		{
			if (const FAssemblyPhysicsCoreRotationMessage* Request = Message.GetPtr<FAssemblyPhysicsCoreRotationMessage>())
			{
				Edges.Add(Request->bHeld);
				TestTrue(TEXT("Queued stops retain the original source after binding clear"), Request->Context.SourceId == OriginalContext.SourceId);
				TestTrue(TEXT("Queued stops retain the original world and model"), Request->Context.World == T.World && Request->Context.Assembly == T.Assembly);
				TestEqual(TEXT("Queued stop belongs to the original round"), Request->Context.RoundGeneration, OriginalContext.RoundGeneration);
			}
		}
		TestTrue(TEXT("Every downstream receiver ends with true then false, never false then stale true"), Edges.Num() == 2 && Edges[0] && !Edges[1]);
		if (ClearMode == 1)
		{
			TestFalse(TEXT("Typed callback actually detached the bridge"), T.Bridge->IsBridgeInitialized());
			const int32 EventsBeforeRepeatedClear = Second->Channels.Num();
			T.Bridge->ClearBridge();
			TestEqual(TEXT("Repeated clear cannot duplicate queued stops"), Second->Channels.Num(), EventsBeforeRepeatedClear);
		}
		else
		{
			const int32 StopsBeforeRefresh = Second->Count(AssemblyMessageTags::CoreRotationRequested);
			TestTrue(TEXT("Cleared core may keep a healthy bridge"), T.Bridge->RefreshPhysicsLifecycle(Reason));
			TestEqual(TEXT("Refresh never replays the canceled press"), Second->Count(AssemblyMessageTags::CoreRotationRequested), StopsBeforeRefresh);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyTagTypedMuscleFanoutTest, "JGD2026.Assembly.PhysicsBridge.TypedMuscleFanoutPreservesEverySameKeyStop", BridgeTestFlags)
bool FAssemblyTagTypedMuscleFanoutTest::RunTest(const FString& Parameters)
{
	FBridgeTestWorld T;
	FText Reason;
	FGuid Bone, MuscleA, MuscleB;
	if (!TestTrue(TEXT("Install connected bone"), T.Assembly->TryInstallBone(TEXT("Bone"), FIntPoint(3,2), 0, Bone, Reason))) { return false; }
	TestTrue(TEXT("Select shared W binding"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestTrue(TEXT("Install first same-key muscle"), T.Assembly->TryInstallMuscle(TEXT("Muscle"), MakeEndpoint(Bone,-20), MakeEndpoint(Bone,20), MuscleA, Reason));
	TestTrue(TEXT("Install second same-key muscle"), T.Assembly->TryInstallMuscle(TEXT("Muscle"), MakeEndpoint(Bone,-10), MakeEndpoint(Bone,10), MuscleB, Reason));
	if (!TestTrue(TEXT("Bridge initializes"), T.Bridge->InitializeBridge(T.Assembly, T.Core, Reason))
		|| !TestTrue(TEXT("Enter Playing"), T.Assembly->TryStartPlaying(Reason))) { return false; }
	UAssemblyPhysicsTestReceiver* First = T.Listen();
	UAssemblyPhysicsTestReceiver* Second = T.Listen();
	First->Model = T.Assembly;
	First->bClearInputOnContractingMuscle = true;
	TestTrue(TEXT("W press reaches first synchronous typed receiver"), T.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("Typed receiver genuinely clears input"), First->bTriggered);
	TMap<FGuid, TArray<bool>> Edges;
	for (const FInstancedStruct& Message : Second->Messages)
	{
		if (const FAssemblyPhysicsMuscleDriveMessage* Request = Message.GetPtr<FAssemblyPhysicsMuscleDriveMessage>())
		{
			Edges.FindOrAdd(Request->Muscle.InstanceId).Add(Request->bContracting);
		}
	}
	for (const FGuid Id : { MuscleA, MuscleB })
	{
		FAssemblyMuscleInstance Muscle;
		TestTrue(TEXT("Both installed same-key muscles survive typed clearing"), T.Assembly->GetMuscle(Id, Muscle));
		TestFalse(TEXT("Each model instance ends relaxed"), Muscle.bContracting);
		const TArray<bool>* Requests = Edges.Find(Id);
		TestTrue(TEXT("Every downstream executor receives final relaxation for each instance"), Requests && !Requests->IsEmpty() && !Requests->Last());
		if (Requests)
		{
			bool bStopped = false;
			for (bool bContracting : *Requests)
			{
				if (!bContracting) { bStopped = true; }
				else { TestFalse(TEXT("Nested false is never overwritten by outer stale contraction"), bStopped); }
			}
		}
	}
	const int32 BeforeRefresh = Second->Count(AssemblyMessageTags::MuscleDriveRequested);
	TestTrue(TEXT("Cleared muscle bridge may refresh"), T.Bridge->RefreshPhysicsLifecycle(Reason));
	TestEqual(TEXT("Refresh emits no stale shared-key contraction"), Second->Count(AssemblyMessageTags::MuscleDriveRequested), BeforeRefresh);
	return true;
}

#endif
