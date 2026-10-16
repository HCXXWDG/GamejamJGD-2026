#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyPhysicsTestReceiver.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr EAutomationTestFlags MuscleSidePhysicsFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FMuscleSidePhysicsWorld
	{
		UGameInstance* GameInstance;
		UWorld* World;
		AActor* Owner;
		UBoxComponent* Core;
		UAssemblyComponent* Model;
		UAssemblyPhysicsBridgeComponent* Bridge;
		UJGDMessageSubsystem* Bus;
		UAssemblyPhysicsTestReceiver* Receiver;
		FJGDMessageListenerHandle Listener;

		FMuscleSidePhysicsWorld()
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone(FName(*FString::Printf(TEXT("MuscleSidePhysics_%s"), *FGuid::NewGuid().ToString())));
			World = GameInstance->GetWorld();
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			Core = NewObject<UBoxComponent>(Owner);
			Owner->AddInstanceComponent(Core);
			Owner->SetRootComponent(Core);
			Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Core->RegisterComponent();
			Model = NewObject<UAssemblyComponent>(Owner);
			Owner->AddInstanceComponent(Model);
			Model->RegisterComponent();
			Bridge = NewObject<UAssemblyPhysicsBridgeComponent>(Owner);
			Owner->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			Bus = UJGDMessageSubsystem::Get(Model);
			Receiver = NewObject<UAssemblyPhysicsTestReceiver>(GameInstance);
			FJGDMessageReceived Delegate;
			Delegate.BindDynamic(Receiver, &UAssemblyPhysicsTestReceiver::Receive);
			Listener = Bus->RegisterListener(AssemblyMessageTags::MuscleDriveRequested, Delegate);
		}

		~FMuscleSidePhysicsWorld()
		{
			Bridge->ClearBridge();
			Bus->UnregisterListener(Listener);
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}

		UBoxComponent* RegisterBody(FGuid BoneId, FText& Reason)
		{
			FTransform Pose;
			if (!Model->GetBoneWorldTransform(BoneId, Pose)) { return nullptr; }
			UBoxComponent* Body = NewObject<UBoxComponent>(Owner);
			Owner->AddInstanceComponent(Body);
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Body->RegisterComponent();
			Body->SetWorldTransform(Pose);
			return Model->RegisterBonePoseSource(BoneId, Body, Reason) ? Body : nullptr;
		}

		FAssemblyPhysicsMuscleDriveMessage LastDrive() const
		{
			const FInstancedStruct* Message = Receiver->Last(AssemblyMessageTags::MuscleDriveRequested);
			const FAssemblyPhysicsMuscleDriveMessage* Drive = Message ? Message->GetPtr<FAssemblyPhysicsMuscleDriveMessage>() : nullptr;
			return Drive ? *Drive : FAssemblyPhysicsMuscleDriveMessage();
		}
	};

	FAssemblyMuscleEndpoint SideAnchor(FGuid BoneId, double X, double Z)
	{
		FAssemblyMuscleEndpoint Result;
		Result.BoneId = BoneId;
		Result.BoneLocalPoint = FVector2D(X, Z);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyMuscleSidePhysicsTest,
	"JGD2026.Assembly.PhysicsBridge.InnerOuterAttachmentsRemainIndependent", MuscleSidePhysicsFlags)
bool FAssemblyMuscleSidePhysicsTest::RunTest(const FString& Parameters)
{
	FMuscleSidePhysicsWorld T;
	FText Reason;
	FAssemblyBoneDefinition Bone;
	FAssemblyMuscleDefinition Muscle;
	const FAssemblyBoneDefinition Round = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Round, 64.0f / 640.0f);
	if (!TestTrue(TEXT("Configure two bones around one round connector"),
		T.Model->ConfigureFreeAssembly(FAssemblyFreeSettings(), { Bone, Round }, { Muscle }, Reason))) { return false; }
	FGuid BoneA, Joint, BoneB, InnerId, OuterId;
	if (!TestTrue(TEXT("Root bone connects to the core"), T.Model->TryInstallFreeBone(Bone.TypeId, FVector2D(96, 0), 0, BoneA, Reason))
		|| !TestTrue(TEXT("Round connector attaches to the root bone end"), T.Model->TryInstallJointPart(Round.TypeId, FVector2D(192, 0), 0, Joint, Reason))
		|| !TestTrue(TEXT("Second bone attaches at a right angle"), T.Model->TryInstallFreeBone(Bone.TypeId, FVector2D(192, 96), -90, BoneB, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	FNormalBonePhysicalStats Stats;
	if (!TestTrue(TEXT("Bone dimensions provide actual side surfaces"), NormalBoneMetrics::Evaluate(Bone.BoneParameters, Stats, Reason))) { return false; }
	const double Side = Stats.Thickness * 0.5;
	// The pair of bone identities is shared; the four surface attachment positions are not.
	const FAssemblyMuscleEndpoint InnerA = SideAnchor(BoneA, 24.25, Side);
	const FAssemblyMuscleEndpoint InnerB = SideAnchor(BoneB, -28.75, Side);
	const FAssemblyMuscleEndpoint OuterA = SideAnchor(BoneA, 24.25, -Side);
	const FAssemblyMuscleEndpoint OuterB = SideAnchor(BoneB, -28.75, -Side);
	if (!TestTrue(TEXT("Select W for the inner muscle"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::W, Reason))
		|| !TestTrue(TEXT("Install inner-side muscle"), T.Model->TryInstallMuscle(Muscle.TypeId, InnerA, InnerB, InnerId, Reason))
		|| !TestTrue(TEXT("Select S for the outer muscle"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::S, Reason))
		|| !TestTrue(TEXT("Install another muscle on the same pair's outer sides"), T.Model->TryInstallMuscle(Muscle.TypeId, OuterA, OuterB, OuterId, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Both muscles own distinct valid instance identities"), InnerId.IsValid() && OuterId.IsValid() && InnerId != OuterId);
	TestEqual(TEXT("Both installations remain present"), T.Model->GetMuscles().Num(), 2);
	const int32 Remaining = Muscle.InitialQuantity - 2;
	TestEqual(TEXT("Each muscle consumes exactly one item"), T.Model->GetRemainingQuantity(Muscle.TypeId), Remaining);
	UBoxComponent* BodyA = T.RegisterBody(BoneA, Reason);
	UBoxComponent* BodyB = T.RegisterBody(BoneB, Reason);
	if (!TestNotNull(TEXT("First bone has a registered primitive pose source"), BodyA)
		|| !TestNotNull(TEXT("Second bone has a registered primitive pose source"), BodyB)
		|| !TestTrue(TEXT("Initialize the existing typed bridge"), T.Bridge->InitializeBridge(T.Model, T.Core, Reason))
		|| !TestTrue(TEXT("Connected structure can start Playing"), T.Model->TryStartPlaying(Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}

	auto CheckDrive = [&](const FAssemblyPhysicsMuscleDriveMessage& Drive, FGuid Id, EAssemblyMuscleKey Key,
		const FAssemblyMuscleEndpoint& A, const FAssemblyMuscleEndpoint& B, bool bContracting)
	{
		TestTrue(TEXT("Request retains the intended instance and independent binding"), Drive.Muscle.InstanceId == Id && Drive.Muscle.BindingKey == Key);
		TestTrue(TEXT("Request retains both bone identities"), Drive.Muscle.EndpointA.BoneId == A.BoneId && Drive.Muscle.EndpointB.BoneId == B.BoneId);
		TestTrue(TEXT("Request retains complete signed local attachment coordinates"),
			Drive.Muscle.EndpointA.BoneLocalPoint.Equals(A.BoneLocalPoint, 0.000001)
			&& Drive.Muscle.EndpointB.BoneLocalPoint.Equals(B.BoneLocalPoint, 0.000001));
		TestTrue(TEXT("Request identifies both actual registered bodies"),
			Drive.EndpointBoneA.Body == BodyA && Drive.EndpointBoneA.PoseSource == BodyA
			&& Drive.EndpointBoneB.Body == BodyB && Drive.EndpointBoneB.PoseSource == BodyB);
		TestTrue(TEXT("Request carries the latest primitive world poses"),
			Drive.EndpointBoneA.WorldTransform.Equals(BodyA->GetComponentTransform(), 0.000001)
			&& Drive.EndpointBoneB.WorldTransform.Equals(BodyB->GetComponentTransform(), 0.000001));
		const FVector ExpectedA = BodyA->GetComponentTransform().TransformPosition(FVector(A.BoneLocalPoint.X, 0, A.BoneLocalPoint.Y));
		const FVector ExpectedB = BodyB->GetComponentTransform().TransformPosition(FVector(B.BoneLocalPoint.X, 0, B.BoneLocalPoint.Y));
		TestTrue(TEXT("World anchors follow full local points through actual body poses"),
			Drive.EndpointWorldA.Equals(ExpectedA, 0.000001) && Drive.EndpointWorldB.Equals(ExpectedB, 0.000001));
		TestTrue(TEXT("Contraction or relaxation remains an explicit intent"), Drive.bContracting == bContracting && Drive.Muscle.bContracting == bContracting);
		TestTrue(TEXT("Request is scoped to this Playing model and world"),
			Drive.Context.Assembly == T.Model && Drive.Context.World == T.World && Drive.Phase == EAssemblyPhase::Playing);
	};

	TestTrue(TEXT("W independently contracts the inner muscle"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	const FAssemblyPhysicsMuscleDriveMessage InnerPress = T.LastDrive();
	CheckDrive(InnerPress, InnerId, EAssemblyMuscleKey::W, InnerA, InnerB, true);
	TestTrue(TEXT("S independently contracts the outer muscle"), T.Model->SetMuscleInput(EAssemblyMuscleKey::S, true));
	const FAssemblyPhysicsMuscleDriveMessage OuterPress = T.LastDrive();
	CheckDrive(OuterPress, OuterId, EAssemblyMuscleKey::S, OuterA, OuterB, true);
	TestFalse(TEXT("Inner and outer anchors produce distinct world force application points"), InnerPress.EndpointWorldA.Equals(OuterPress.EndpointWorldA));
	TestFalse(TEXT("Different signed anchors also produce different endpoint-line directions"),
		(InnerPress.EndpointWorldB - InnerPress.EndpointWorldA).GetSafeNormal().Equals(
			(OuterPress.EndpointWorldB - OuterPress.EndpointWorldA).GetSafeNormal(), 0.000001));
	TestTrue(TEXT("Release only the W binding"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, false));
	CheckDrive(T.LastDrive(), InnerId, EAssemblyMuscleKey::W, InnerA, InnerB, false);
	FAssemblyMuscleInstance StillHeld;
	TestTrue(TEXT("Releasing W leaves the S instance contracting"), T.Model->GetMuscle(OuterId, StillHeld) && StillHeld.bContracting);
	TestEqual(TEXT("Two presses and one release emit three distinct requests"), T.Receiver->Messages.Num(), 3);
	TestTrue(TEXT("Release S"), T.Model->SetMuscleInput(EAssemblyMuscleKey::S, false));
	CheckDrive(T.LastDrive(), OuterId, EAssemblyMuscleKey::S, OuterA, OuterB, false);

	// Pose sources stand in for the physical executor's moving bodies; this test never applies forces.
	BodyA->SetWorldTransform(FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(21.25)), FVector(316.5, 17, -48.25)));
	BodyB->SetWorldTransform(FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-58.5)), FVector(430.25, 17, 87.75)));
	TestTrue(TEXT("Outer muscle can drive again after both bodies move and rotate"), T.Model->SetMuscleInput(EAssemblyMuscleKey::S, true));
	const FAssemblyPhysicsMuscleDriveMessage MovedOuterPress = T.LastDrive();
	CheckDrive(MovedOuterPress, OuterId, EAssemblyMuscleKey::S, OuterA, OuterB, true);
	TestFalse(TEXT("New requests resolve current poses instead of cached original world anchors"),
		MovedOuterPress.EndpointWorldA.Equals(OuterPress.EndpointWorldA) || MovedOuterPress.EndpointWorldB.Equals(OuterPress.EndpointWorldB));
	TestTrue(TEXT("Inner muscle also retains its signed attachments after movement"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	CheckDrive(T.LastDrive(), InnerId, EAssemblyMuscleKey::W, InnerA, InnerB, true);
	TestTrue(TEXT("W relaxation after movement is independent"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, false));
	CheckDrive(T.LastDrive(), InnerId, EAssemblyMuscleKey::W, InnerA, InnerB, false);
	TestTrue(TEXT("The other muscle remains held after the second W release"), T.Model->GetMuscle(OuterId, StillHeld) && StillHeld.bContracting);
	TestTrue(TEXT("S relaxation after movement is independent"), T.Model->SetMuscleInput(EAssemblyMuscleKey::S, false));
	CheckDrive(T.LastDrive(), OuterId, EAssemblyMuscleKey::S, OuterA, OuterB, false);
	TestEqual(TEXT("Two complete independent press/release rounds emit eight requests"), T.Receiver->Messages.Num(), 8);
	TestEqual(TEXT("Pose updates and input never consume or restore inventory"), T.Model->GetRemainingQuantity(Muscle.TypeId), Remaining);
	TestEqual(TEXT("Driving never merges the two installed instances"), T.Model->GetMuscles().Num(), 2);
	return true;
}

#endif
