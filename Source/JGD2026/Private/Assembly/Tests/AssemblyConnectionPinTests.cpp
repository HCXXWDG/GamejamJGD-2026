#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Assembly/AssemblyAppearanceData.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include <limits>

namespace AssemblyConnectionPinTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UPaperSprite* MakeSprite()
	{
		UTexture2D* Texture = NewObject<UTexture2D>();
		TArray<uint8> Pixels;
		Pixels.Init(255, 64 * 64 * 4);
		Texture->Source.Init(64, 64, 1, 1, TSF_BGRA8, Pixels.GetData());
		UPaperSprite* Sprite = NewObject<UPaperSprite>();
		FSpriteAssetInitParameters Init;
		Init.Texture = Texture;
		Init.Dimension = FIntPoint(64, 64);
		Init.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(Init, false);
		for (FVector2D Point : { FVector2D(0, 0), FVector2D(64, 0), FVector2D(0, 64),
			FVector2D(0, 64), FVector2D(64, 0), FVector2D(64, 64) })
		{
			Sprite->BakedRenderData.Add(FVector4(Point.X - 32, 32 - Point.Y, Point.X / 64, Point.Y / 64));
		}
		return Sprite;
	}

	struct FWorld
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UAssemblyComponent* Model = nullptr;
		UAssemblyInteractionComponent* Views = nullptr;
		UAssemblyAppearanceData* Art = nullptr;
		FAssemblyBoneDefinition Bar;
		FText Reason;
		bool bReady = false;

		FWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Owner);
			Owner->AddInstanceComponent(Root);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Owner->SetActorTransform(FTransform(FQuat(FVector::YAxisVector, 0.25), FVector(500, 0, 100)));
			Model = NewObject<UAssemblyComponent>(Owner);
			Model->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Model);
			Model->RegisterComponent();
			FAssemblyBoneDefinition Bone;
			Bone.Footprint.Reset();
			Bar = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar);
			FAssemblyFreeSettings Settings;
			Settings.bEnableSurfaceSnapping = false;
			bReady = Model->ConfigureFreeAssembly(Settings, { Bone, Bar }, {}, Reason);
			Art = NewObject<UAssemblyAppearanceData>();
			Art->ConnectionPinSprite = MakeSprite();
			Art->BreakableJointMarker = Art->ConnectionPinSprite;
			Art->JointSprites.Add(EAssemblyJointKind::Hinge, MakeSprite());
			Views = NewObject<UAssemblyInteractionComponent>(Owner);
			Views->bAutoInitialize = false;
			Views->bAutoCreatePhysicsBridge = false;
			Views->AppearanceData = Art;
			Owner->AddInstanceComponent(Views);
			Views->RegisterComponent();
			bReady = bReady && Views->InitializeInteraction(Model, Reason);
		}
		~FWorld()
		{
			Views->ClearInteraction();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		bool InstallChain(FGuid& BoneId, FGuid& BarId)
		{
			return Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, BoneId, Reason)
				&& Model->TryInstallJointPart(Bar.TypeId, FVector2D(160 + Bar.JointPartSize.X * 0.5, 0), 0, BarId, Reason);
		}
		int32 CountSpriteComponents() const
		{
			TArray<UPaperSpriteComponent*> Components;
			Owner->GetComponents(Components);
			return Components.Num();
		}
	};
}

using namespace AssemblyConnectionPinTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyPinsEveryConnectionTest,
	"JGD2026.AssemblyInteraction.ConnectionPins.EveryConnectionAndKinds", Flags)
bool FAssemblyPinsEveryConnectionTest::RunTest(const FString& Parameters)
{
	FWorld T;
	if (!TestTrue(TEXT("Initialize the presenter"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	FGuid BoneId, BarId;
	if (!TestTrue(TEXT("Install core-bone-bar chain"), T.InstallChain(BoneId, BarId))) { AddError(T.Reason.ToString()); return false; }
	TestEqual(TEXT("There are two actual connection edges"), T.Model->GetJoints().Num(), 2);
	TestEqual(TEXT("A pin replaces legacy caps and no duplicate sprites are created"), T.CountSpriteComponents(), 2);
	const TArray<FAssemblyJointInstance> Joints = T.Model->GetJoints();
	for (const FAssemblyJointInstance& Joint : Joints)
	{
		UPaperSpriteComponent* Pin = T.Views->GetConnectionPinView(Joint.InstanceId);
		if (!TestNotNull(TEXT("Each core or independent-part connection gets a pin"), Pin)) return false;
		TestTrue(TEXT("Configured pin is visible without collision"), Pin->IsVisible()
			&& Pin->GetSprite() == T.Art->ConnectionPinSprite && Pin->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		FVector A, B;
		TestTrue(TEXT("Read physical anchors"), T.Views->GetJointWorldAnchors(Joint, A, B));
		const FVector Center = (A + B) * 0.5, Location = Pin->GetComponentLocation();
		TestTrue(TEXT("Pin has no in-plane displacement from the real contact"),
			FMath::IsNearlyEqual(Center.X, Location.X, 0.001) && FMath::IsNearlyEqual(Center.Z, Location.Z, 0.001));
		const EAssemblyJointKind Breakable = Joint.Kind == EAssemblyJointKind::Fixed
			? EAssemblyJointKind::BreakableFixed : EAssemblyJointKind::BreakableHinge;
		TestTrue(TEXT("Change breakable property without changing its generic pin"), T.Model->TrySetJointKind(Joint.InstanceId, Breakable, T.Reason));
		TestTrue(TEXT("The existing pin is reused"), T.Views->GetConnectionPinView(Joint.InstanceId) == Pin);
	}
	TestEqual(TEXT("Using the same pin for a breakable badge never doubles the artwork"), T.CountSpriteComponents(), 2);
	TestEqual(TEXT("Artwork does not add model bodies"), T.Model->GetBones().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyPinsRestartTest,
	"JGD2026.AssemblyInteraction.ConnectionPins.DragRemoveAndThreeRestarts", Flags)
bool FAssemblyPinsRestartTest::RunTest(const FString& Parameters)
{
	FWorld T;
	if (!TestTrue(TEXT("Initialize the presenter"), T.bReady)) return false;
	for (int32 Round = 0; Round < 3; ++Round)
	{
		FGuid BoneId, BarId;
		if (!TestTrue(TEXT("Install this round"), T.InstallChain(BoneId, BarId))) return false;
		const TArray<FAssemblyJointInstance> Joints = T.Model->GetJoints();
		if (!TestEqual(TEXT("Two connections this round"), Joints.Num(), 2)) return false;
		if (!TestTrue(TEXT("Begin moving the installed bone"), T.Views->BeginBoneDrag(TEXT("Bone"), BoneId, T.Reason))) return false;
		for (const FAssemblyJointInstance& Joint : Joints)
		{
			const UPaperSpriteComponent* Pin = T.Views->GetConnectionPinView(Joint.InstanceId);
			TestTrue(TEXT("Pins associated with the hidden original bone hide immediately"), Pin && !Pin->IsVisible());
		}
		T.Views->CancelBoneDrag();
		for (const FAssemblyJointInstance& Joint : Joints)
		{
			const UPaperSpriteComponent* Pin = T.Views->GetConnectionPinView(Joint.InstanceId);
			TestTrue(TEXT("Cancelling restores existing pins"), Pin && Pin->IsVisible());
		}
		if (!TestTrue(TEXT("Remove the leaf joint part"), T.Views->RemoveInstance(BarId, T.Reason))) return false;
		for (const FAssemblyJointInstance& Joint : Joints)
		{
			if (Joint.BoneA == BarId || Joint.BoneB == BarId)
				TestNull(TEXT("Removing an edge destroys its pin mapping"), T.Views->GetConnectionPinView(Joint.InstanceId));
		}
		if (!TestTrue(TEXT("Restart this round"), T.Model->RestartAssembly(T.Reason))) return false;
		TestEqual(TEXT("No pin components survive the reset"), T.CountSpriteComponents(), 0);
		TestEqual(TEXT("Bone stock resets"), T.Model->GetRemainingQuantity(TEXT("Bone")), 4);
		TestEqual(TEXT("Joint stock resets"), T.Model->GetRemainingQuantity(T.Bar.TypeId), 2);
		TestTrue(TEXT("Repeated initialization remains idempotent"), T.Views->InitializeInteraction(T.Model, T.Reason));
		TestEqual(TEXT("Initialization alone does not create pins"), T.CountSpriteComponents(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyPinsPhysicsPoseTest,
	"JGD2026.AssemblyInteraction.ConnectionPins.PhysicalAnchorsAndInvalidArt", Flags)
bool FAssemblyPinsPhysicsPoseTest::RunTest(const FString& Parameters)
{
	FWorld T;
	if (!TestTrue(TEXT("Initialize the presenter"), T.bReady)) return false;
	FGuid BoneId, BarId;
	if (!TestTrue(TEXT("Install a connected chain"), T.InstallChain(BoneId, BarId))) return false;
	if (!TestTrue(TEXT("Enter Playing"), T.Model->TryStartPlaying(T.Reason))) return false;
	ANormalBone* Bone = T.Views->GetBoneActor(BoneId);
	if (!TestNotNull(TEXT("Find bound bone view"), Bone)) return false;
	const FTransform PhysicalPose(FQuat(FVector::YAxisVector, 0.7), FVector(730, 0, 210));
	Bone->SetActorTransform(PhysicalPose);
	USphereComponent* Core = Cast<USphereComponent>(T.Views->GetCoreBody());
	if (!TestNotNull(TEXT("Find the actual core body"), Core)) return false;
	const FTransform CorePose(FQuat(FVector::YAxisVector, -0.4), FVector(680, 0, 130));
	Core->SetWorldTransform(CorePose);
	Core->SetSphereRadius(27, false);
	static_cast<UActorComponent*>(T.Views)->TickComponent(0, LEVELTICK_All, nullptr);
	TestTrue(TEXT("A custom physics core keeps its Playing pose even without Simulate Physics"), Core->GetComponentTransform().Equals(CorePose));
	TestEqual(TEXT("A custom physics core keeps its own shape in Playing"), Core->GetUnscaledSphereRadius(), 27.0f);
	for (const FAssemblyJointInstance& Joint : T.Model->GetJoints())
	{
		FVector A, B;
		if (!TestTrue(TEXT("Read updated physical endpoints"), T.Views->GetJointWorldAnchors(Joint, A, B))) return false;
		auto PhysicalAnchor = [&](FGuid Id, FVector2D Local)
		{
			const FTransform Pose = !Id.IsValid() ? CorePose : Id == BoneId ? PhysicalPose
				: T.Views->GetBoneActor(Id)->GetBoneBody()->GetComponentTransform();
			return Pose.TransformPosition(FVector(Local.X, 0, Local.Y));
		};
		TestTrue(TEXT("Anchor lookup independently matches the actual core and bone body transforms"),
			A.Equals(PhysicalAnchor(Joint.BoneA, Joint.LocalAnchorA), 0.001)
			&& B.Equals(PhysicalAnchor(Joint.BoneB, Joint.LocalAnchorB), 0.001));
		const UPaperSpriteComponent* Pin = T.Views->GetConnectionPinView(Joint.InstanceId);
		if (!TestNotNull(TEXT("Find moving connection pin"), Pin)) return false;
		const FVector Center = (A + B) * 0.5;
		TestTrue(TEXT("Pins follow current physical anchors rather than old model poses"),
			FMath::IsNearlyEqual(Pin->GetComponentLocation().X, Center.X, 0.001)
			&& FMath::IsNearlyEqual(Pin->GetComponentLocation().Z, Center.Z, 0.001));
	}
	UPaperSprite* OriginalPin = T.Art->ConnectionPinSprite;
	T.Art->ConnectionPinSprite = nullptr;
	T.Views->RefreshAppearance();
	for (const FAssemblyJointInstance& Joint : T.Model->GetJoints())
		TestNull(TEXT("Removing the configured pin destroys stale views"), T.Views->GetConnectionPinView(Joint.InstanceId));
	T.Art->ConnectionPinSprite = OriginalPin;
	T.Views->RefreshAppearance();
	for (const FAssemblyJointInstance& Joint : T.Model->GetJoints())
		TestNotNull(TEXT("Restoring the artwork recreates the pins"), T.Views->GetConnectionPinView(Joint.InstanceId));
	T.Art->ConnectionPinDiameter = std::numeric_limits<float>::quiet_NaN();
	T.Views->RefreshAppearance();
	for (const FAssemblyJointInstance& Joint : T.Model->GetJoints())
		TestNull(TEXT("Non-finite display size cannot leave a stale pin"), T.Views->GetConnectionPinView(Joint.InstanceId));
	TestTrue(TEXT("Presentation never writes the physics-owned body pose"), Bone->GetActorTransform().Equals(PhysicalPose));
	TestEqual(TEXT("Presentation never consumes inventory"), T.Model->GetRemainingQuantity(TEXT("Bone")), 3);
	return true;
}

#endif
