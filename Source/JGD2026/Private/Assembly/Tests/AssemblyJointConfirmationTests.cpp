#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyPhysicsTestReceiver.h"
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
	constexpr EAutomationTestFlags JointBridgeFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FJointBridgeWorld
	{
		UGameInstance* GameInstance;
		UWorld* World;
		AActor* Owner;
		UAssemblyComponent* Assembly;
		UAssemblyPhysicsBridgeComponent* Bridge;
		UBoxComponent* Core;
		UJGDMessageSubsystem* Bus;

		FJointBridgeWorld()
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone(FName(*FString::Printf(TEXT("AssemblyJointBridge_%s"), *FGuid::NewGuid().ToString())));
			World = GameInstance->GetWorld();
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			Core = NewObject<UBoxComponent>(Owner);
			Owner->AddInstanceComponent(Core);
			Owner->SetRootComponent(Core);
			Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Core->RegisterComponent();
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
			Bridge = NewObject<UAssemblyPhysicsBridgeComponent>(Owner);
			Owner->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			Bus = UJGDMessageSubsystem::Get(Assembly);
			FText Reason;
			Assembly->ConfigureFreeAssembly(Assembly->DefaultFreeSettings, Assembly->DefaultBones, Assembly->DefaultMuscles, Reason);
		}

		~FJointBridgeWorld()
		{
			Bridge->ClearBridge();
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}

		FGuid InstallCoreConnectedBone()
		{
			FText Reason;
			FGuid BoneId;
			Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, BoneId, Reason);
			const TArray<FAssemblyJointInstance> Joints = Assembly->GetJoints();
			return Joints.Num() == 1 ? Joints[0].InstanceId : FGuid();
		}

		UAssemblyPhysicsTestReceiver* ListenForRequests()
		{
			UAssemblyPhysicsTestReceiver* Receiver = NewObject<UAssemblyPhysicsTestReceiver>(GameInstance);
			FJGDMessageReceived Delegate;
			Delegate.BindDynamic(Receiver, &UAssemblyPhysicsTestReceiver::Receive);
			Bus->RegisterListener(AssemblyMessageTags::JointBreakRequested, Delegate);
			return Receiver;
		}

		void Confirm(FGuid JointId, const FAssemblyPhysicsContext& Context)
		{
			FAssemblyPhysicsJointBrokenMessage Message;
			Message.Context = Context;
			Message.JointId = JointId;
			Bus->BroadcastMessage(AssemblyMessageTags::JointBroken, FInstancedStruct::Make(Message));
		}

		void TickBridge()
		{
			Bridge->TickComponent(0.016f, LEVELTICK_All, nullptr);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPhysicsConfirmationTest,
	"JGD2026.Assembly.PhysicsBridge.JointBreakRequiresPhysicalConfirmation", JointBridgeFlags)
bool FAssemblyJointPhysicsConfirmationTest::RunTest(const FString& Parameters)
{
	FJointBridgeWorld Fixture;
	FText Reason;
	const FGuid JointId = Fixture.InstallCoreConnectedBone();
	if (!TestTrue(TEXT("A bone endpoint creates one core joint"), JointId.IsValid())
		|| !TestTrue(TEXT("Initialize typed bridge"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	UAssemblyPhysicsTestReceiver* Receiver = Fixture.ListenForRequests();
	const int32 BoneStock = Fixture.Assembly->GetRemainingQuantity(TEXT("Bone"));
	TestFalse(TEXT("A normal hinge cannot request a break"), Fixture.Assembly->RequestJointBreak(JointId, Reason));
	TestTrue(TEXT("Configure a breakable hinge"), Fixture.Assembly->TrySetJointKind(JointId, EAssemblyJointKind::BreakableHinge, Reason));
	TestFalse(TEXT("A breakable hinge still cannot break during assembly"), Fixture.Assembly->RequestJointBreak(JointId, Reason));
	TestTrue(TEXT("Enter Playing"), Fixture.Assembly->TryStartPlaying(Reason));
	const FAssemblyPhysicsContext Context = Fixture.Bridge->GetPhysicsContext();
	Fixture.Confirm(JointId, Context);
	Fixture.TickBridge();
	TestEqual(TEXT("Unsolicited confirmation cannot remove a joint"), Fixture.Assembly->GetJoints().Num(), 1);
	TestTrue(TEXT("Gameplay sends a physical break request"), Fixture.Assembly->RequestJointBreak(JointId, Reason));
	TestEqual(TEXT("Exactly one typed break request is emitted"), Receiver->Count(AssemblyMessageTags::JointBreakRequested), 1);
	TestEqual(TEXT("Request alone preserves connectivity"), Fixture.Assembly->GetJoints().Num(), 1);
	const FInstancedStruct* Raw = Receiver->Last(AssemblyMessageTags::JointBreakRequested);
	const FAssemblyPhysicsJointBreakMessage* Request = Raw ? Raw->GetPtr<FAssemblyPhysicsJointBreakMessage>() : nullptr;
	if (TestNotNull(TEXT("Request uses the published fixed schema"), Request))
	{
		TestTrue(TEXT("Request addresses this physical source and joint"), Request->Context.SourceId == Context.SourceId && Request->Joint.InstanceId == JointId);
		TestTrue(TEXT("Core endpoint is explicitly represented by its body"), !Request->BodyA.BoneId.IsValid() && Request->BodyA.Body == Fixture.Core);
	}
	Fixture.TickBridge();
	TestEqual(TEXT("Elapsed bridge tick without reply does not fake a break"), Fixture.Assembly->GetJoints().Num(), 1);
	Fixture.Confirm(JointId, Context);
	Fixture.Confirm(JointId, Context);
	TestEqual(TEXT("Synchronous physical replies remain queued outside the model callback"), Fixture.Assembly->GetJoints().Num(), 1);
	Fixture.TickBridge();
	TestEqual(TEXT("Confirmed constraint disappears once on next tick"), Fixture.Assembly->GetJoints().Num(), 0);
	Fixture.Confirm(JointId, Context);
	Fixture.TickBridge();
	TestEqual(TEXT("Duplicate or late confirmation cannot change inventory"), Fixture.Assembly->GetRemainingQuantity(TEXT("Bone")), BoneStock);
	TestTrue(TEXT("Detached playing structure retains valid instance/inventory data"), Fixture.Assembly->ValidateAssembly().bValid);
	TestFalse(TEXT("The bridge never enables physical simulation itself"), Fixture.Core->IsSimulatingPhysics());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPhysicsReplyIsolationTest,
	"JGD2026.Assembly.PhysicsBridge.JointRepliesRespectSourceWorldAndRound", JointBridgeFlags)
bool FAssemblyJointPhysicsReplyIsolationTest::RunTest(const FString& Parameters)
{
	FJointBridgeWorld Fixture;
	FJointBridgeWorld OtherWorld;
	FText Reason;
	FGuid JointId = Fixture.InstallCoreConnectedBone();
	if (!TestTrue(TEXT("Install connected joint"), JointId.IsValid())
		|| !TestTrue(TEXT("Use breakable fixed joint"), Fixture.Assembly->TrySetJointKind(JointId, EAssemblyJointKind::BreakableFixed, Reason))
		|| !TestTrue(TEXT("Bind bridge"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason))
		|| !TestTrue(TEXT("Start"), Fixture.Assembly->TryStartPlaying(Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Request physical break"), Fixture.Assembly->RequestJointBreak(JointId, Reason));
	const FAssemblyPhysicsContext OldContext = Fixture.Bridge->GetPhysicsContext();
	FAssemblyPhysicsContext ForeignContext = OldContext;
	ForeignContext.World = OtherWorld.World;
	Fixture.Confirm(JointId, ForeignContext);
	Fixture.TickBridge();
	TestEqual(TEXT("Another world cannot confirm this joint"), Fixture.Assembly->GetJoints().Num(), 1);
	ForeignContext = OldContext;
	ForeignContext.Assembly = OtherWorld.Assembly;
	Fixture.Confirm(JointId, ForeignContext);
	Fixture.TickBridge();
	TestEqual(TEXT("Another model cannot confirm this joint"), Fixture.Assembly->GetJoints().Num(), 1);
	ForeignContext = OldContext;
	ForeignContext.SourceId = FGuid::NewGuid();
	Fixture.Confirm(JointId, ForeignContext);
	Fixture.TickBridge();
	TestEqual(TEXT("Another bridge source cannot confirm this joint"), Fixture.Assembly->GetJoints().Num(), 1);
	ForeignContext = OldContext;
	++ForeignContext.RoundGeneration;
	Fixture.Confirm(JointId, ForeignContext);
	Fixture.TickBridge();
	TestEqual(TEXT("Another round cannot confirm this joint"), Fixture.Assembly->GetJoints().Num(), 1);
	Fixture.Confirm(JointId, OldContext);
	TestTrue(TEXT("Restart while a valid reply is queued"), Fixture.Assembly->RestartAssembly(Reason));
	JointId = Fixture.InstallCoreConnectedBone();
	TestTrue(TEXT("Restart creates a fresh connected joint"), JointId.IsValid());
	TestTrue(TEXT("Configure fresh breakable joint"), Fixture.Assembly->TrySetJointKind(JointId, EAssemblyJointKind::BreakableFixed, Reason));
	TestTrue(TEXT("Start next round"), Fixture.Assembly->TryStartPlaying(Reason));
	TestTrue(TEXT("Request next-round break"), Fixture.Assembly->RequestJointBreak(JointId, Reason));
	Fixture.Confirm(JointId, OldContext);
	Fixture.TickBridge();
	TestEqual(TEXT("Restart clears queued replies and rejects old epoch even for current id"), Fixture.Assembly->GetJoints().Num(), 1);
	const FAssemblyPhysicsContext BeforeRebind = Fixture.Bridge->GetPhysicsContext();
	Fixture.Bridge->ClearBridge();
	TestTrue(TEXT("Rebind with a new source"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason));
	TestTrue(TEXT("Renew physical break request after rebinding"), Fixture.Assembly->RequestJointBreak(JointId, Reason));
	Fixture.Confirm(JointId, BeforeRebind);
	Fixture.TickBridge();
	TestEqual(TEXT("Old bridge replies cannot affect a replacement binding"), Fixture.Assembly->GetJoints().Num(), 1);
	Fixture.Confirm(JointId, Fixture.Bridge->GetPhysicsContext());
	Fixture.TickBridge();
	TestEqual(TEXT("A correct fresh-source reply is applied"), Fixture.Assembly->GetJoints().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyOrdinaryJointCannotBreakTest,
	"JGD2026.Assembly.PhysicsBridge.OrdinaryJointsRejectBreaksInPlaying", JointBridgeFlags)
bool FAssemblyOrdinaryJointCannotBreakTest::RunTest(const FString& Parameters)
{
	FJointBridgeWorld Fixture;
	FText Reason;
	if (!TestTrue(TEXT("Bind model to physics bus"), Fixture.Bridge->InitializeBridge(Fixture.Assembly, Fixture.Core, Reason)))
	{
		return false;
	}
	UAssemblyPhysicsTestReceiver* Receiver = Fixture.ListenForRequests();
	for (EAssemblyJointKind Kind : { EAssemblyJointKind::Hinge, EAssemblyJointKind::Fixed })
	{
		TestTrue(TEXT("Reset for an ordinary joint"), Fixture.Assembly->RestartAssembly(Reason));
		const FGuid JointId = Fixture.InstallCoreConnectedBone();
		TestTrue(TEXT("Create ordinary connected joint"), JointId.IsValid());
		TestTrue(TEXT("Set ordinary joint kind"), Fixture.Assembly->TrySetJointKind(JointId, Kind, Reason));
		TestTrue(TEXT("Enter Playing with ordinary joint"), Fixture.Assembly->TryStartPlaying(Reason));
		TestFalse(TEXT("Playing does not make an ordinary joint breakable"), Fixture.Assembly->RequestJointBreak(JointId, Reason));
		TestFalse(TEXT("Direct physics confirmation also cannot remove an ordinary joint"), Fixture.Assembly->ConfirmJointBroken(JointId, Reason));
		Fixture.Confirm(JointId, Fixture.Bridge->GetPhysicsContext());
		Fixture.TickBridge();
		TestEqual(TEXT("Ordinary constraints survive invalid confirmations"), Fixture.Assembly->GetJoints().Num(), 1);
		TestEqual(TEXT("Rejected breaks produce no physical request"), Receiver->Count(AssemblyMessageTags::JointBreakRequested), 0);
	}
	return true;
}

#endif
