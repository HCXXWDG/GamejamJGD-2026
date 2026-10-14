#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Assembly/NormalBone.h"
#include "../AssemblyFreeRules.h"
#include "Assembly/AssemblySurfaceGeometry.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
	constexpr EAutomationTestFlags SurfaceSnapFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FAssemblyBoneDefinition ShortSurfaceBone()
	{
		FAssemblyBoneDefinition Bone;
		Bone.BoneParameters.Length = EBoneLengthTier::Short;
		Bone.BoneParameters.UnitLength = 64;
		Bone.BoneParameters.Thickness = EBoneThicknessTier::Medium;
		Bone.InitialQuantity = 12;
		return Bone;
	}

	FAssemblyBoneDefinition SurfaceRound()
	{
		return AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Round, 64.0f / 640.0f);
	}

	UAssemblyComponent* MakeSurfaceModel(bool bRemoteCore = false)
	{
		UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
		FAssemblyFreeSettings Settings;
		if (bRemoteCore) { Settings.CoreLocalPosition = FVector2D(-1000, 0); }
		FText Reason;
		Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone(), SurfaceRound() }, { FAssemblyMuscleDefinition() }, Reason);
		return Model;
	}

	FAssemblyBoneInstance SurfaceBody(FGuid Id, FName Type, FVector2D Position, float Angle = 0)
	{
		FAssemblyBoneInstance Body;
		Body.InstanceId = Id;
		Body.TypeId = Type;
		Body.LocalPosition = Position;
		Body.RotationDegrees = Angle;
		Body.LocalVisualTransform = AssemblyFreeRules::MakePlacement(Position, Angle).LocalVisualTransform;
		return Body;
	}

	bool IsSurfacePair(const FAssemblyJointInstance& Joint, FGuid A, FGuid B)
	{
		return (Joint.BoneA == A && Joint.BoneB == B) || (Joint.BoneA == B && Joint.BoneB == A);
	}

	struct FSurfaceDragWorld
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UAssemblyComponent* Model = nullptr;
		UAssemblyInteractionComponent* Interaction = nullptr;
		FText Reason;
		bool bReady = false;

		FSurfaceDragWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Owner);
			Owner->AddInstanceComponent(Root);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Model = NewObject<UAssemblyComponent>(Owner);
			Model->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Model);
			Model->RegisterComponent();
			bReady = Model->ConfigureFreeAssembly(FAssemblyFreeSettings(), { ShortSurfaceBone(), SurfaceRound() }, {}, Reason);
			Interaction = NewObject<UAssemblyInteractionComponent>(Owner);
			Interaction->bAutoInitialize = false;
			Interaction->bAutoCreatePhysicsBridge = false;
			Owner->AddInstanceComponent(Interaction);
			Interaction->RegisterComponent();
			bReady &= Interaction->InitializeInteraction(Model, Reason);
		}

		~FSurfaceDragWorld()
		{
			Interaction->ClearInteraction();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceSnapCoreLatchTest, "JGD2026.Assembly.SurfaceSnap.CoreAcquireReleaseAndExactCommit", SurfaceSnapFlags)
bool FAssemblySurfaceSnapCoreLatchTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel();
	const FAssemblyPlacementResult First = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(74, 0), 0, FGuid(), FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("A 10-unit surface gap acquires the core circle"), First.bValid && First.SurfaceSnap.bSnapped && First.SurfaceSnap.bCore)) { return false; }
	TestTrue(TEXT("Snapped body touches the circumference instead of its center"), First.LocalPosition.Equals(FVector2D(64, 0), 0.001));
	const FAssemblyPlacementResult Held = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(85, 0), 0, FGuid(), First.SurfaceSnap);
	TestTrue(TEXT("A 21-unit raw gap retains the existing latch"), Held.bValid && Held.SurfaceSnap.bSnapped && Held.LocalPosition.Equals(First.LocalPosition, 0.001));
	const FAssemblyPlacementResult Unlatched = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(85, 0), 0, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("The same 21-unit gap cannot acquire a new latch"), Unlatched.bValid && !Unlatched.SurfaceSnap.bSnapped && Unlatched.LocalPosition.Equals(FVector2D(85, 0), 0.001));
	const FAssemblyPlacementResult Released = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(97, 0), 0, FGuid(), Held.SurfaceSnap);
	TestTrue(TEXT("A 33-unit raw gap releases and follows the pointer"), Released.bValid && !Released.SurfaceSnap.bSnapped && Released.LocalPosition.Equals(FVector2D(97, 0), 0.001));
	const FAssemblyPlacementResult Far = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(1200.25, 900.75), 37.25f, FGuid(), First.SurfaceSnap);
	TestTrue(TEXT("Free position and angle survive after release"), Far.bValid && !Far.SurfaceSnap.bSnapped && Far.LocalPosition.Equals(FVector2D(1200.25, 900.75), 0.001));
	TestEqual(TEXT("Snapping does not introduce angle quantization"), Far.RotationDegrees, 37.25f);
	FGuid Id;
	FText Reason;
	TestTrue(TEXT("Resolved placement commits through exact geometry validation"), Model->TryInstallFreeBone(TEXT("Bone"), First.LocalPosition, First.RotationDegrees, Id, Reason));
	FAssemblyBoneInstance Installed;
	TestTrue(TEXT("Committed transform exactly matches the shown preview"), Model->GetBone(Id, Installed) && Installed.LocalVisualTransform.Equals(First.LocalVisualTransform, 0.001));
	TestEqual(TEXT("Only commit consumes stock"), Model->GetRemainingQuantity(TEXT("Bone")), 11);
	const FAssemblyPlacementResult Self = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(500, 0), 0, Id, First.SurfaceSnap);
	TestTrue(TEXT("Moving a body cannot acquire its hidden original instance"), Self.bValid && !Self.SurfaceSnap.bSnapped && Self.LocalPosition.Equals(FVector2D(500, 0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceSnapConnectionPolicyTest, "JGD2026.Assembly.SurfaceSnap.RequiresJointAndAcceptsFullBoneSurface", SurfaceSnapFlags)
bool FAssemblySurfaceSnapConnectionPolicyTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel(true);
	FGuid Bone, AdjacentBone, Joint;
	FText Reason;
	TestTrue(TEXT("Stage one ordinary bone"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(200, 0), 0, Bone, Reason));
	const FAssemblyPlacementResult BoneNearBone = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(270, 0), 0, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("An ordinary bone never snaps directly to another bone"), BoneNearBone.bValid && !BoneNearBone.SurfaceSnap.bSnapped && BoneNearBone.LocalPosition.Equals(FVector2D(270, 0), 0.001));
	TestTrue(TEXT("Surface-touching ordinary bones may be staged without a rigid edge"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(264, 0), 0, AdjacentBone, Reason));
	TestEqual(TEXT("No direct bone-bone constraint is created"), Model->GetJoints().Num(), 0);
	const FAssemblyPlacementResult Midpoint = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"), FVector2D(200, 50), 0, FGuid(), FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("A joint acquires the middle of the bone's top surface"), Midpoint.bValid && Midpoint.SurfaceSnap.bSnapped && Midpoint.SurfaceSnap.TargetId == Bone)) { return false; }
	TestTrue(TEXT("Surface contact uses bone midpoint rather than either end slot"), FMath::Abs(Midpoint.SurfaceSnap.TargetLocalAnchor.X) < 0.001 && FMath::IsNearlyEqual(Midpoint.SurfaceSnap.TargetLocalAnchor.Y, 6.4, 0.001));
	TestTrue(TEXT("Circle center stays one radius outside the contacted surface"), Midpoint.LocalPosition.Equals(FVector2D(200, 38.4), 0.001));
	TestTrue(TEXT("Install the resolved standalone joint"), Model->TryInstallJointPart(TEXT("Joint_Round"), Midpoint.LocalPosition, 0, Joint, Reason));
	TestTrue(TEXT("Bone-joint surface contact creates a real constraint"), Model->GetJoints().ContainsByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J, Bone, Joint); }));
	TestFalse(TEXT("A nearby joint does not resurrect forbidden bone-bone edges"), Model->GetJoints().ContainsByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J, Bone, AdjacentBone); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceSnapCircleAngleTest, "JGD2026.Assembly.SurfaceSnap.CircleRimAndArbitraryAngle", SurfaceSnapFlags)
bool FAssemblySurfaceSnapCircleAngleTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel(true);
	FGuid Round;
	FText Reason;
	TestTrue(TEXT("Place a circular joint away from the core"), Model->TryInstallJointPart(TEXT("Joint_Round"), FVector2D(200, 0), 0, Round, Reason));
	const FAssemblyPlacementResult Pose = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(274, 0), 0, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("Bone snaps to the joint circle's true rim"), Pose.bValid && Pose.SurfaceSnap.bSnapped && Pose.SurfaceSnap.TargetId == Round && Pose.LocalPosition.Equals(FVector2D(264, 0), 0.001));
	TestTrue(TEXT("Target anchor lies exactly on the circle"), FMath::IsNearlyEqual(Pose.SurfaceSnap.TargetLocalAnchor.Size(), 32.0, 0.001));
	const FAssemblyPlacementResult Angled = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(251, 0), 37.25f, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("A nearby arbitrarily rotated body resolves a valid contact"), Angled.bValid && Angled.SurfaceSnap.bSnapped);
	TestEqual(TEXT("Surface snapping translates only and preserves the requested angle"), Angled.RotationDegrees, 37.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceOutlineGeometryTest, "JGD2026.Assembly.SurfaceSnap.ConcaveOutlineAndInvalidGeometry", SurfaceSnapFlags)
bool FAssemblySurfaceOutlineGeometryTest::RunTest(const FString& Parameters)
{
	FAssemblyBoneDefinition Corner = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Corner, 64.0f / 640.0f);
	Corner.SurfaceOutline = { FVector2D(-32,-32), FVector2D(32,-32), FVector2D(32,-16), FVector2D(-16,-16), FVector2D(-16,32), FVector2D(-32,32) };
	AssemblySurfaceGeometry::FShape Shape;
	FText Reason;
	if (!TestTrue(TEXT("A simple custom concave surface is accepted"), AssemblySurfaceGeometry::BuildShape(Corner, FTransform::Identity, Shape, Reason))) { return false; }
	TestTrue(TEXT("Inside the L-shaped arm is material"), AssemblySurfaceGeometry::ContainsPoint(Shape, FVector2D(-24, 8)));
	TestFalse(TEXT("A point inside the bounding box's notch is outside the component"), AssemblySurfaceGeometry::ContainsPoint(Shape, FVector2D(8, 8)));
	TestFalse(TEXT("A circle in the notch does not overlap either arm"), AssemblySurfaceGeometry::Overlaps(Shape, AssemblySurfaceGeometry::MakeCircle(FVector2D(8,8), 4)));
	TestTrue(TEXT("A circle inside an arm overlaps the actual contour"), AssemblySurfaceGeometry::Overlaps(Shape, AssemblySurfaceGeometry::MakeCircle(FVector2D(-24,8), 4)));
	AssemblySurfaceGeometry::FContact Contact;
	TestTrue(TEXT("Closest surface contact can be computed inside the notch"), AssemblySurfaceGeometry::ClosestContact(Shape, AssemblySurfaceGeometry::MakeCircle(FVector2D(8,8), 4), Contact));
	TestTrue(TEXT("The notch has a nonzero clearance"), Contact.DistanceSquared > 1.0);
	Corner.SurfaceOutline = { FVector2D(-10,-10), FVector2D(10,10), FVector2D(-10,10), FVector2D(10,-10) };
	TestFalse(TEXT("A self-intersecting polygon is rejected before use"), AssemblySurfaceGeometry::BuildShape(Corner, FTransform::Identity, Shape, Reason));
	Corner.SurfaceOutline = { FVector2D(-10,-10), FVector2D(10,-10), FVector2D(std::numeric_limits<double>::quiet_NaN(),10) };
	TestFalse(TEXT("Non-finite custom vertices cannot enter geometry queries"), AssemblySurfaceGeometry::BuildShape(Corner, FTransform::Identity, Shape, Reason));
	UAssemblyComponent* Model = MakeSurfaceModel();
	TestFalse(TEXT("A non-finite pointer cannot produce a valid drag preview"), Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(std::numeric_limits<double>::infinity(),0), 0, FGuid(), FAssemblySurfaceSnapState()).bValid);
	const FAssemblyBoneDefinition Cross = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Cross, 64.0f / 960.0f);
	FAssemblyBoneDefinition LongBone = ShortSurfaceBone();
	LongBone.BoneParameters.Length = EBoneLengthTier::Long;
	AssemblySurfaceGeometry::FShape CrossShape, TopShape, BottomShape;
	TestTrue(TEXT("Build the actual standard concave Cross shape"), AssemblySurfaceGeometry::BuildShape(Cross, AssemblyFreeRules::MakePlacement(FVector2D(192,0),0).LocalVisualTransform, CrossShape, Reason));
	TestTrue(TEXT("Build a bone tangent to the Cross top surface"), AssemblySurfaceGeometry::BuildShape(LongBone, AssemblyFreeRules::MakePlacement(FVector2D(192,96),-90).LocalVisualTransform, TopShape, Reason));
	TestTrue(TEXT("Build a bone tangent to the Cross bottom surface"), AssemblySurfaceGeometry::BuildShape(LongBone, AssemblyFreeRules::MakePlacement(FVector2D(192,-96),90).LocalVisualTransform, BottomShape, Reason));
	TestFalse(TEXT("Top surface contact must not count as concave polygon overlap"), AssemblySurfaceGeometry::Overlaps(CrossShape, TopShape));
	TestFalse(TEXT("Bottom surface contact must not count as concave polygon overlap"), AssemblySurfaceGeometry::Overlaps(CrossShape, BottomShape));
	TestTrue(TEXT("Build a bone penetrating the Cross by more than the shared tolerance"), AssemblySurfaceGeometry::BuildShape(LongBone, AssemblyFreeRules::MakePlacement(FVector2D(192,95.99),-90).LocalVisualTransform, TopShape, Reason));
	TestTrue(TEXT("Real 0.01-unit penetration remains rejected after tangent numerical correction"), AssemblySurfaceGeometry::Overlaps(CrossShape, TopShape));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceSnapConfigurationTest, "JGD2026.Assembly.SurfaceSnap.ConfigurationHysteresisBounds", SurfaceSnapFlags)
bool FAssemblySurfaceSnapConfigurationTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel();
	FAssemblyFreeSettings Settings = Model->GetFreeSettings();
	FText Reason;
	Settings.SnapReleaseDistance = Settings.ConnectionDistance;
	TestFalse(TEXT("Enabled snapping requires a strictly larger release radius"), Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone() }, {}, Reason));
	TestFalse(TEXT("Invalid hysteresis explains the failure"), Reason.IsEmpty());
	TestEqual(TEXT("Failed configure preserves the original release distance"), Model->GetFreeSettings().SnapReleaseDistance, 32.0f);
	Settings.SnapReleaseDistance = 8;
	TestFalse(TEXT("Release inside acquisition is refused while snapping is enabled"), Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone() }, {}, Reason));
	Settings.bEnableSurfaceSnapping = false;
	TestTrue(TEXT("Disabled snapping permits an unused smaller release radius"), Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone() }, {}, Reason));
	TestTrue(TEXT("Disabled snapping leaves legal pointer poses unmodified"), Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(74,0), 0, FGuid(), FAssemblySurfaceSnapState()).LocalPosition.Equals(FVector2D(74,0), 0.001));
	Settings.SnapReleaseDistance = -1;
	TestFalse(TEXT("Negative release radii are never accepted"), Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone() }, {}, Reason));
	Settings.SnapReleaseDistance = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Non-finite release radii are never accepted"), Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone() }, {}, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceSnapPenetrationTest, "JGD2026.Assembly.SurfaceSnap.ShallowPenetrationResolvesWithoutStockLoss", SurfaceSnapFlags)
bool FAssemblySurfaceSnapPenetrationTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel();
	const FAssemblyPlacementResult CircleContact = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(60,0), 0, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("A shallow rectangular intrusion can move out to the exact circle rim"), CircleContact.bValid && CircleContact.SurfaceSnap.bSnapped && CircleContact.LocalPosition.Equals(FVector2D(64,0), 0.001));
	TestFalse(TEXT("Deep intrusion cannot invent a distant surface snap"), Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D::ZeroVector, 0, FGuid(), FAssemblySurfaceSnapState()).bValid);
	TestEqual(TEXT("Circle previews consume no inventory"), Model->GetRemainingQuantity(TEXT("Bone")), 12);
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition = FVector2D(-1000,0);
	FAssemblyBoneDefinition Bar = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar, 64.0f / 640.0f);
	FText Reason;
	TestTrue(TEXT("Configure rectangular surface case"), Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone(), Bar }, {}, Reason));
	FGuid Bone;
	TestTrue(TEXT("Install target rectangle"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(200,0), 0, Bone, Reason));
	const FAssemblyPlacementResult RectangleContact = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Bar"), FVector2D(200,21), 0, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("A shallow polygon intrusion resolves to the nearest valid facing surface"), RectangleContact.bValid && RectangleContact.SurfaceSnap.bSnapped && RectangleContact.SurfaceSnap.TargetId == Bone && RectangleContact.LocalPosition.Equals(FVector2D(200,22.4), 0.001));
	TestTrue(TEXT("The resolved polygon placement passes actual installation validation"), Model->ValidateFreeBonePlacement(TEXT("Joint_Bar"), RectangleContact.LocalPosition, RectangleContact.RotationDegrees, FGuid()).bValid);
	TestEqual(TEXT("Shallow rectangle previews consume no joint pieces"), Model->GetRemainingQuantity(TEXT("Joint_Bar")), 2);
	FGuid Failed;
	TestFalse(TEXT("An invalid overlapping exact drop is refused"), Model->TryInstallJointPart(TEXT("Joint_Bar"), FVector2D(200,0), 0, Failed, Reason));
	TestTrue(TEXT("Rejected release retains all stock and the original installed body"), !Failed.IsValid() && Model->GetRemainingQuantity(TEXT("Joint_Bar")) == 2 && Model->GetRemainingQuantity(TEXT("Bone")) == 11 && Model->GetBones().Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceJointRebuildTest, "JGD2026.Assembly.SurfaceSnap.OneEdgePerPairAndSlidingIdentity", SurfaceSnapFlags)
bool FAssemblySurfaceJointRebuildTest::RunTest(const FString& Parameters)
{
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition = FVector2D(-1000,0);
	const TArray<FAssemblyBoneDefinition> Definitions = { ShortSurfaceBone(), SurfaceRound() };
	const FGuid BoneId(20,0,0,1), RoundId(10,0,0,1);
	TArray<FAssemblyBoneInstance> Bodies = { SurfaceBody(BoneId, TEXT("Bone"), FVector2D(200,0)), SurfaceBody(RoundId, TEXT("Joint_Round"), FVector2D(200,38.4)) };
	TArray<FAssemblyJointInstance> Original = AssemblyFreeRules::BuildJoints(Settings, Definitions, Bodies, {});
	if (!TestEqual(TEXT("Complete facing surfaces still create only one constraint per pair"), Original.Num(), 1)) { return false; }
	Original[0].Kind = EAssemblyJointKind::BreakableHinge;
	Original[0].MinAngleDegrees = -12;
	Original[0].MaxAngleDegrees = 27;
	Bodies[1] = SurfaceBody(RoundId, TEXT("Joint_Round"), FVector2D(212,38.4));
	const TArray<FAssemblyJointInstance> Sliding = AssemblyFreeRules::BuildJoints(Settings, Definitions, Bodies, Original);
	if (!TestEqual(TEXT("A sliding surface contact keeps one constraint"), Sliding.Num(), 1)) { return false; }
	TestTrue(TEXT("Sliding preserves persistent edge ID and customized type"), Sliding[0].InstanceId == Original[0].InstanceId && Sliding[0].Kind == EAssemblyJointKind::BreakableHinge);
	TestTrue(TEXT("Sliding preserves both angular limits"), Sliding[0].MinAngleDegrees == -12 && Sliding[0].MaxAngleDegrees == 27);
	const FVector2D OldBoneAnchor = Original[0].BoneA == BoneId ? Original[0].LocalAnchorA : Original[0].LocalAnchorB;
	const FVector2D NewBoneAnchor = Sliding[0].BoneA == BoneId ? Sliding[0].LocalAnchorA : Sliding[0].LocalAnchorB;
	TestFalse(TEXT("Constraint anchors actually update along the bone surface"), OldBoneAnchor.Equals(NewBoneAnchor, 0.001));
	Swap(Bodies[0], Bodies[1]);
	const TArray<FAssemblyJointInstance> Reordered = AssemblyFreeRules::BuildJoints(Settings, Definitions, Bodies, Sliding);
	TestTrue(TEXT("Persistent IDs and anchors do not depend on array order"), Reordered.Num() == 1 && Reordered[0].InstanceId == Sliding[0].InstanceId && Reordered[0].BoneA == Sliding[0].BoneA && Reordered[0].BoneB == Sliding[0].BoneB && Reordered[0].LocalAnchorA.Equals(Sliding[0].LocalAnchorA, 0.001) && Reordered[0].LocalAnchorB.Equals(Sliding[0].LocalAnchorB, 0.001));
	Bodies[0] = SurfaceBody(RoundId, TEXT("Joint_Round"), FVector2D(700,0));
	TestTrue(TEXT("Removing actual surface contact removes the constraint"), AssemblyFreeRules::BuildJoints(Settings, Definitions, Bodies, Sliding).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceSnapCandidateSafetyTest, "JGD2026.Assembly.SurfaceSnap.DeterministicTiesAndBlockedContact", SurfaceSnapFlags)
bool FAssemblySurfaceSnapCandidateSafetyTest::RunTest(const FString& Parameters)
{
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition = FVector2D(-1000,0);
	const FGuid Low(1,0,0,1), High(20,0,0,1);
	TArray<FAssemblyBoneInstance> Targets = { SurfaceBody(High, TEXT("Joint_Round"), FVector2D(200,-50)), SurfaceBody(Low, TEXT("Joint_Round"), FVector2D(200,50)) };
	TArray<FAssemblyBoneDefinition> Definitions = { ShortSurfaceBone(), SurfaceRound() };
	const FAssemblyPlacementResult First = AssemblyFreeRules::ResolveSurfacePlacement(Settings, Definitions, Targets, TEXT("Bone"), FVector2D(200,0), 0, FGuid(), FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("Equidistant surfaces produce a valid snap"), First.bValid && First.SurfaceSnap.bSnapped)) { return false; }
	Swap(Targets[0], Targets[1]);
	const FAssemblyPlacementResult Reordered = AssemblyFreeRules::ResolveSurfacePlacement(Settings, Definitions, Targets, TEXT("Bone"), FVector2D(200,0), 0, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("Equal-distance target selection uses persistent IDs rather than array order"), First.SurfaceSnap.TargetId == Low && Reordered.SurfaceSnap.TargetId == Low && Reordered.LocalPosition.Equals(First.LocalPosition, 0.001));
	FAssemblyBoneDefinition Blocker = ShortSurfaceBone();
	Blocker.TypeId = TEXT("SmallBlocker");
	Blocker.SurfaceOutline = { FVector2D(-2,-2), FVector2D(2,-2), FVector2D(2,2), FVector2D(-2,2) };
	Definitions.Add(Blocker);
	Targets = { SurfaceBody(Low, TEXT("Joint_Round"), FVector2D(200,0)), SurfaceBody(High, TEXT("SmallBlocker"), FVector2D(234,0)) };
	const FAssemblyPlacementResult Blocked = AssemblyFreeRules::ResolveSurfacePlacement(Settings, Definitions, Targets, TEXT("Bone"), FVector2D(244,0), 90, FGuid(), FAssemblySurfaceSnapState());
	FText Reason;
	TestTrue(TEXT("Raw pointer remains a legal free placement beside the blocker"), AssemblyFreeRules::ValidateGeometry(Settings, Definitions, Targets, TEXT("Bone"), AssemblyFreeRules::MakePlacement(FVector2D(244,0),90), FGuid(), Reason));
	TestFalse(TEXT("The nearest contact translation would penetrate a third component"), AssemblyFreeRules::ValidateGeometry(Settings, Definitions, Targets, TEXT("Bone"), AssemblyFreeRules::MakePlacement(FVector2D(238.4,0),90), FGuid(), Reason));
	TestTrue(TEXT("Resolver never selects a contact that penetrates the third component"), Blocked.bValid && AssemblyFreeRules::ValidateGeometry(Settings, Definitions, Targets, TEXT("Bone"), Blocked, FGuid(), Reason));
	TestFalse(TEXT("Blocked nearest contact is not displayed as the successful result"), Blocked.LocalPosition.Equals(FVector2D(238.4,0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceDragActorTest, "JGD2026.AssemblyInteraction.SurfaceSnap.RawPointerTickCancelAndMove", SurfaceSnapFlags)
bool FAssemblySurfaceDragActorTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize native actor presenter"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	FGuid Id;
	TestTrue(TEXT("Begin a shelf drag"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason));
	TestTrue(TEXT("Move into acquisition range"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(74,0), T.Reason));
	const FAssemblyPlacementResult Preview = T.Interaction->GetBoneDragPreview();
	if (!TestTrue(TEXT("Actual presenter displays the resolved circle contact"), Preview.SurfaceSnap.bSnapped && Preview.LocalPosition.Equals(FVector2D(64,0), 0.001))) { return false; }
	TestTrue(TEXT("Install exactly the visible resolved preview"), T.Interaction->CommitBoneDrag(Id, T.Reason));
	ANormalBone* Actor = T.Interaction->GetBoneActor(Id);
	if (!TestNotNull(TEXT("Committed bone has a real view"), Actor)) { return false; }
	const FTransform InstalledPose = Actor->GetActorTransform();
	const int32 Stock = T.Model->GetRemainingQuantity(TEXT("Bone"));
	TestTrue(TEXT("Direct drag begins on the installed bone"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, T.Reason));
	TestTrue(TEXT("Move raw pointer beyond acquisition but within release"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(85,0), T.Reason));
	for (int32 I = 0; I < 3; ++I) { static_cast<UActorComponent*>(T.Interaction)->TickComponent(0.016f, LEVELTICK_All, nullptr); }
	TestTrue(TEXT("Repeated tick keeps the latch without replacing raw pointer data"), T.Interaction->GetBoneDragPreview().SurfaceSnap.bSnapped && T.Interaction->GetBoneDragPreview().LocalPosition.Equals(FVector2D(64,0), 0.001));
	TestTrue(TEXT("Pulling the raw pointer further releases"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(97,0), T.Reason));
	static_cast<UActorComponent*>(T.Interaction)->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Tick follows released pointer rather than locking to old preview"), !T.Interaction->GetBoneDragPreview().SurfaceSnap.bSnapped && T.Interaction->GetBoneDragPreview().LocalPosition.Equals(FVector2D(97,0), 0.001));
	TestTrue(TEXT("Free pointer moves far from all targets"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(500.25,120.75), T.Reason));
	TestTrue(TEXT("Drag rotation accepts a continuous delta"), T.Interaction->RotateBoneDrag(13.5f, T.Reason));
	static_cast<UActorComponent*>(T.Interaction)->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Rotating and ticking retain the latest free pointer position"), T.Interaction->GetBoneDragPreview().LocalPosition.Equals(FVector2D(500.25,120.75), 0.001));
	TestEqual(TEXT("Rotation remains continuous"), T.Interaction->GetBoneDragPreview().RotationDegrees, 13.5f);
	T.Interaction->InvalidateBoneDragPreview(FText::FromString(TEXT("Test pointer projection lost")));
	TestFalse(TEXT("Losing the pointer makes the preview uncommittable"), T.Interaction->GetBoneDragPreview().bValid);
	TestFalse(TEXT("Rotation cannot revive a preview after losing the pointer"), T.Interaction->RotateBoneDrag(90, T.Reason));
	static_cast<UActorComponent*>(T.Interaction)->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Tick cannot revive a preview after losing the pointer"), T.Interaction->GetBoneDragPreview().bValid);
	T.Interaction->CancelBoneDrag();
	TestTrue(TEXT("Cancel restores the same original actor and transform"), T.Interaction->GetBoneActor(Id) == Actor && !Actor->IsHidden() && Actor->GetActorTransform().Equals(InstalledPose, 0.001));
	TestEqual(TEXT("Preview movement and cancel never change stock"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
	TestTrue(TEXT("Begin invalid installed drag"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, T.Reason));
	TestFalse(TEXT("Deep core penetration remains invalid"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D::ZeroVector, T.Reason));
	FGuid FailedId;
	TestFalse(TEXT("Invalid release does not commit a new state"), T.Interaction->CommitBoneDrag(FailedId, T.Reason));
	TestTrue(TEXT("Invalid release restores original actor and no replacement ID"), !FailedId.IsValid() && !Actor->IsHidden() && Actor->GetActorTransform().Equals(InstalledPose, 0.001));
	TestEqual(TEXT("Invalid release keeps stock unchanged"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
	TestTrue(TEXT("Begin a successful free move"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, T.Reason));
	TestTrue(TEXT("Move to a legal detached staging position"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(500,100), T.Reason));
	FGuid Moved;
	TestTrue(TEXT("Move commits existing instance rather than cloning"), T.Interaction->CommitBoneDrag(Moved, T.Reason) && Moved == Id);
	TestEqual(TEXT("One instance remains after all drag attempts"), T.Model->GetBones().Num(), 1);
	TestEqual(TEXT("Successful move does not consume stock"), T.Model->GetRemainingQuantity(TEXT("Bone")), Stock);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceDragFrameTest, "JGD2026.AssemblyInteraction.SurfaceSnap.FrameChangeKeepsPreviewWorldPose", SurfaceSnapFlags)
bool FAssemblySurfaceDragFrameTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize frame test world"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	TestTrue(TEXT("Begin a drag from the shelf"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason));
	TestFalse(TEXT("A shelf drag cannot commit before obtaining a pointer position"), T.Interaction->GetBoneDragPreview().bValid);
	TestFalse(TEXT("Rotation alone cannot invent a pointer position"), T.Interaction->RotateBoneDrag(90, T.Reason));
	TestTrue(TEXT("Acquire the core from an actual pointer"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(64,0), T.Reason));
	// Restore the horizontal angle; the previous no-pointer rotation may update the selected orientation.
	TestTrue(TEXT("Choose a horizontal orientation"), T.Interaction->RotateBoneDrag(-T.Interaction->GetBoneDragPreview().RotationDegrees, T.Reason));
	TestTrue(TEXT("Move to the horizontal core contact"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(74,0), T.Reason));
	const FAssemblyPlacementResult Before = T.Interaction->GetBoneDragPreview();
	TestTrue(TEXT("Acquire state is visible before the frame moves"), Before.bValid && Before.SurfaceSnap.bSnapped);
	T.Owner->SetActorTransform(FTransform(FQuat(FVector::YAxisVector, 0.42), FVector(500,20,300)));
	for (int32 I = 0; I < 3; ++I) { static_cast<UActorComponent*>(T.Interaction)->TickComponent(0.016f, LEVELTICK_All, nullptr); }
	const FAssemblyPlacementResult After = T.Interaction->GetBoneDragPreview();
	TestTrue(TEXT("Changing the owner frame preserves local pointer-derived contact"), After.bValid && After.SurfaceSnap.bSnapped && After.LocalPosition.Equals(Before.LocalPosition, 0.001));
	ANormalBone* PreviewActor = nullptr;
	for (TActorIterator<ANormalBone> It(T.World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetOwner() == T.Owner && !It->GetInstanceId().IsValid()) { PreviewActor = *It; break; }
	}
	if (!TestNotNull(TEXT("A real preview actor remains during the drag"), PreviewActor)) { return false; }
	TestTrue(TEXT("Preview world transform follows the current rotated translated frame"), PreviewActor->GetActorTransform().Equals(After.LocalVisualTransform * T.Model->GetGridFrameTransform(), 0.001));
	TestTrue(TEXT("Pull out beyond the release radius after frame change"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(97,0), T.Reason));
	static_cast<UActorComponent*>(T.Interaction)->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("A changed frame does not retain or reacquire the old contact after release"), !T.Interaction->GetBoneDragPreview().SurfaceSnap.bSnapped && T.Interaction->GetBoneDragPreview().LocalPosition.Equals(FVector2D(97,0), 0.001));
	T.Interaction->CancelBoneDrag();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceInstalledTargetFrameTest, "JGD2026.AssemblyInteraction.SurfaceSnap.InstalledTargetsUseCurrentFrameWithoutTeleport", SurfaceSnapFlags)
bool FAssemblySurfaceInstalledTargetFrameTest::RunTest(const FString& Parameters)
{
	for (int32 Case = 0; Case < 3; ++Case)
	{
		FSurfaceDragWorld T;
		if (!TestTrue(TEXT("Initialize installed target world"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		FGuid Bone;
		if (!TestTrue(TEXT("Install a real core-connected bone view"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
			&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(64,0), T.Reason)
			&& T.Interaction->CommitBoneDrag(Bone, T.Reason))) { AddError(T.Reason.ToString()); return false; }
		ANormalBone* BoneActor = T.Interaction->GetBoneActor(Bone);
		if (!TestNotNull(TEXT("Target has a registered real view"), BoneActor)) { return false; }
		const FTransform OriginalWorld = BoneActor->GetActorTransform();
		TestTrue(TEXT("Initial world body is at the visible expected position"), OriginalWorld.GetLocation().Equals(FVector(64,0,0), 0.001));
		const FTransform Frame(FQuat(FVector::YAxisVector, Case == 0 ? 0.0 : 0.42), FVector(Case == 2 ? 0 : 1000,0,0));
		T.Owner->SetActorTransform(Frame);
		TestTrue(TEXT("Changing the frame does not itself move an unattached installed body"), BoneActor->GetActorTransform().Equals(OriginalWorld, 0.001));
		TestTrue(TEXT("Changing the selected muscle key is a non-geometric notification"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::W, T.Reason));
		TestTrue(TEXT("Non-geometric model notification does not reapply a stale local pose through the new frame"), BoneActor->GetActorTransform().Equals(OriginalWorld, 0.001));
		const FVector RawLocal3D = Frame.InverseTransformPosition(FVector(64,0,50));
		const FVector2D RawLocal(RawLocal3D.X, RawLocal3D.Z);
		TestTrue(TEXT("Begin a new standalone joint drag"), T.Interaction->BeginBoneDrag(TEXT("Joint_Round"), FGuid(), T.Reason));
		if (!TestTrue(TEXT("Current frame snapshot snaps to the actual visible bone surface"), T.Interaction->UpdateBoneDragAtLocalPosition(RawLocal, T.Reason))) { AddError(T.Reason.ToString()); return false; }
		const FAssemblyPlacementResult Preview = T.Interaction->GetBoneDragPreview();
		TestTrue(TEXT("Snap target is the existing visible bone ID"), Preview.SurfaceSnap.bSnapped && !Preview.SurfaceSnap.bCore && Preview.SurfaceSnap.TargetId == Bone);
		const FTransform ExpectedWorld = Preview.LocalVisualTransform * T.Model->GetGridFrameTransform();
		TestTrue(TEXT("Contact preserves the correct world-space bone midpoint"), ExpectedWorld.GetLocation().Equals(FVector(64,0,38.4), 0.001));
		ANormalBone* PreviewActor = nullptr;
		for (TActorIterator<ANormalBone> It(T.World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetOwner() == T.Owner && !It->GetInstanceId().IsValid()) { PreviewActor = *It; break; }
		}
		if (!TestNotNull(TEXT("A visible joint preview actor exists"), PreviewActor)) { return false; }
		TestTrue(TEXT("Actual preview world transform agrees with resolved data"), PreviewActor->GetActorTransform().Equals(ExpectedWorld, 0.001));
		FGuid JointPart;
		if (!TestTrue(TEXT("Commit the resolved joint without teleporting existing targets"), T.Interaction->CommitBoneDrag(JointPart, T.Reason))) { AddError(T.Reason.ToString()); return false; }
		TestTrue(TEXT("Commit keeps the original bone actor and world pose"), T.Interaction->GetBoneActor(Bone) == BoneActor && BoneActor->GetActorTransform().Equals(OriginalWorld, 0.001));
		FAssemblyBoneInstance UpdatedBone;
		TestTrue(TEXT("Authoritative local pose is rebased to the current frame"), T.Model->GetBone(Bone, UpdatedBone)
			&& (UpdatedBone.LocalVisualTransform * T.Model->GetGridFrameTransform()).Equals(OriginalWorld, 0.001));
		const TArray<FAssemblyJointInstance> Constraints = T.Model->GetJoints();
		const FAssemblyJointInstance* Constraint = Constraints.FindByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J, Bone, JointPart); });
		if (!TestNotNull(TEXT("The new surface constraint addresses both actual bodies"), Constraint)) { return false; }
		FTransform WorldA, WorldB;
		TestTrue(TEXT("Both current world poses are available"), T.Model->GetBoneWorldTransform(Constraint->BoneA, WorldA) && T.Model->GetBoneWorldTransform(Constraint->BoneB, WorldB));
		const FVector AnchorA = WorldA.TransformPosition(FVector(Constraint->LocalAnchorA.X,0,Constraint->LocalAnchorA.Y));
		const FVector AnchorB = WorldB.TransformPosition(FVector(Constraint->LocalAnchorB.X,0,Constraint->LocalAnchorB.Y));
		TestTrue(TEXT("Constraint anchors coincide at the visible bone surface after rebasing"), AnchorA.Equals(AnchorB, 0.001) && AnchorA.Equals(FVector(64,0,6.4), 0.001));
		TestEqual(TEXT("Frame changes and previews do not consume another bone"), T.Model->GetRemainingQuantity(TEXT("Bone")), 11);
		TestEqual(TEXT("Only the committed joint consumes one item"), T.Model->GetRemainingQuantity(TEXT("Joint_Round")), 1);
		TestEqual(TEXT("Existing bone and new joint each have exactly one model instance"), T.Model->GetBones().Num(), 2);
		if (Case == 2)
		{
			TestTrue(TEXT("A rotated frame with unchanged physical core center keeps the actual surface graph valid"), T.Model->ValidateAssembly().bValid);
			TestTrue(TEXT("The actual connected surface graph can enter Playing after frame-aware commit"), T.Model->TryStartPlaying(T.Reason));
		}
		else
		{
			TestFalse(TEXT("Moving the physical core far away does not invent connectivity to detached bodies"), T.Model->TryStartPlaying(T.Reason));
			TestFalse(TEXT("Detached physical structure gives an explicit start rejection"), T.Reason.IsEmpty());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceConnectedStartTest, "JGD2026.Assembly.SurfaceSnap.CompleteSurfaceGraphCanPlayAndRestart", SurfaceSnapFlags)
bool FAssemblySurfaceConnectedStartTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
	FAssemblyBoneDefinition BoneDefinition = ShortSurfaceBone();
	BoneDefinition.BoneParameters.Length = EBoneLengthTier::Long;
	FText Reason;
	if (!TestTrue(TEXT("Configure full surface graph"), Model->ConfigureFreeAssembly(FAssemblyFreeSettings(), { BoneDefinition, SurfaceRound() }, { FAssemblyMuscleDefinition() }, Reason))) { return false; }
	FGuid RootBone, Round, LeafA, LeafB;
	if (!TestTrue(TEXT("Install root bone touching the core"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(96,0), 0, RootBone, Reason))) { return false; }
	const FAssemblyPlacementResult RoundPreview = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"), FVector2D(96,50), 0, FGuid(), FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("Round snaps onto the root bone's middle"), RoundPreview.bValid && RoundPreview.SurfaceSnap.bSnapped && RoundPreview.SurfaceSnap.TargetId == RootBone)) { return false; }
	if (!TestTrue(TEXT("Commit midpoint joint piece"), Model->TryInstallJointPart(TEXT("Joint_Round"), RoundPreview.LocalPosition, RoundPreview.RotationDegrees, Round, Reason))) { return false; }
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FAssemblyPlacementResult Preview = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(Side == 0 ? 126 : 66,134.4), -90, FGuid(), FAssemblySurfaceSnapState());
		if (!TestTrue(TEXT("A leaf bone snaps to a different non-cardinal point on the circular rim"), Preview.bValid && Preview.SurfaceSnap.bSnapped && Preview.SurfaceSnap.TargetId == Round)) { return false; }
		TestTrue(TEXT("The contact is not limited to an old circular slot"), FMath::Abs(Preview.SurfaceSnap.TargetLocalAnchor.X) > 1 && FMath::Abs(Preview.SurfaceSnap.TargetLocalAnchor.Y) > 1);
		FGuid& Leaf = Side == 0 ? LeafA : LeafB;
		if (!TestTrue(TEXT("Install each actual resolved leaf"), Model->TryInstallFreeBone(TEXT("Bone"), Preview.LocalPosition, Preview.RotationDegrees, Leaf, Reason))) { AddError(Reason.ToString()); return false; }
	}
	TestEqual(TEXT("Core-root plus three independent surface contacts have no duplicate pair edges"), Model->GetJoints().Num(), 4);
	TestTrue(TEXT("Multiple points on one joint surface preserve core connectivity"), Model->IsStructureConnected());
	const FAssemblyValidationResult Validation = Model->ValidateAssembly();
	if (!TestTrue(FString::Printf(TEXT("Complete continuous surface assembly validates: %s"), *Validation.Reason.ToString()), Validation.bValid)) { return false; }
	FGuid Muscle;
	FAssemblyMuscleEndpoint EndpointA, EndpointB;
	EndpointA.BoneId = EndpointB.BoneId = RootBone;
	EndpointA.BoneLocalPoint = FVector2D(-12,0);
	EndpointB.BoneLocalPoint = FVector2D(12,0);
	TestTrue(TEXT("Select W before adding muscle"), Model->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestTrue(TEXT("Install actual same-bone muscle on the connected structure"), Model->TryInstallMuscle(TEXT("Muscle"), EndpointA, EndpointB, Muscle, Reason));
	TestFalse(TEXT("Assembly phase still gates muscle contraction"), Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestFalse(TEXT("Assembly phase gates core input while preserving surface editing"), Model->SetCoreRotationInput(true));
	if (!TestTrue(TEXT("Complete non-slot surface graph enters Playing"), Model->TryStartPlaying(Reason))) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("Playing allows bound muscle contraction"), Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("Playing allows core input on the complete surface graph"), Model->SetCoreRotationInput(true));
	TestTrue(TEXT("The actual muscle records contraction"), Model->GetMuscles().Num() == 1 && Model->GetMuscles()[0].bContracting);
	TestTrue(TEXT("Input release requests relaxation"), Model->SetMuscleInput(EAssemblyMuscleKey::W, false));
	TestTrue(TEXT("Restart clears the entire continuous surface structure"), Model->RestartAssembly(Reason));
	TestTrue(TEXT("Restart has no bodies, muscles, edges or held input"), Model->GetBones().IsEmpty() && Model->GetMuscles().IsEmpty() && Model->GetJoints().IsEmpty() && !Model->IsCoreRotationHeld());
	TestEqual(TEXT("Restart restores initial bone stock"), Model->GetRemainingQuantity(TEXT("Bone")), 12);
	TestEqual(TEXT("Restart restores initial round joint stock"), Model->GetRemainingQuantity(TEXT("Joint_Round")), 2);
	TestEqual(TEXT("Restart restores initial muscle stock"), Model->GetRemainingQuantity(TEXT("Muscle")), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceInvalidTargetPoseTest, "JGD2026.Assembly.SurfaceSnap.InvalidRegisteredTargetPoseRejectsTransaction", SurfaceSnapFlags)
bool FAssemblySurfaceInvalidTargetPoseTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize invalid-pose fixture"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Install actual target body"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
		&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(64,0), T.Reason)
		&& T.Interaction->CommitBoneDrag(Bone, T.Reason))) { return false; }
	ANormalBone* Actor = T.Interaction->GetBoneActor(Bone);
	if (!TestNotNull(TEXT("Real target pose source exists"), Actor)) { return false; }
	USceneComponent* Source = T.Model->GetBonePoseSource(Bone);
	if (!TestNotNull(TEXT("Actual body is registered as the target pose"), Source)) { return false; }
	const FTransform Original = Source->GetComponentTransform();
	Source->SetWorldScale3D(FVector(0,1,1));
	FTransform InvalidWorld;
	TestFalse(TEXT("Zero-scale actual pose cannot be inverted"), T.Model->GetBoneWorldTransform(Bone, InvalidWorld));
	const FAssemblyPlacementResult Preview = T.Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"), FVector2D(64,50), 0, FGuid(), FAssemblySurfaceSnapState());
	TestFalse(TEXT("Resolver refuses invalid registered targets instead of silently using stale model poses"), Preview.bValid);
	TestFalse(TEXT("Target-pose rejection has an explanation"), Preview.Reason.IsEmpty());
	FGuid Failed;
	TestFalse(TEXT("Exact install cannot bypass invalid target-pose validation"), T.Model->TryInstallJointPart(TEXT("Joint_Round"), FVector2D(64,38.4), 0, Failed, T.Reason));
	TestTrue(TEXT("Rejected transaction preserves stock, existing ID and body count"), !Failed.IsValid() && T.Model->GetRemainingQuantity(TEXT("Joint_Round")) == 2 && T.Model->GetRemainingQuantity(TEXT("Bone")) == 11 && T.Model->GetBones().Num() == 1 && T.Interaction->GetBoneActor(Bone) == Actor);
	Source->SetWorldTransform(Original);
	TestTrue(TEXT("Restored physical pose permits a valid surface preview again"), T.Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"), FVector2D(64,50), 0, FGuid(), FAssemblySurfaceSnapState()).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceNonuniformFrameTest, "JGD2026.Assembly.SurfaceSnap.NonuniformFrameIsExplicitlyRejected", SurfaceSnapFlags)
bool FAssemblySurfaceNonuniformFrameTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize nonuniform frame fixture"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	T.Owner->SetActorScale3D(FVector(2,1,0.75));
	const FAssemblyPlacementResult Preview = T.Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(74,0), 37.25f, FGuid(), FAssemblySurfaceSnapState());
	TestFalse(TEXT("Surface resolver refuses planar nonuniform scale instead of treating shear as a rectangle"), Preview.bValid);
	TestFalse(TEXT("Unsupported frame transform has a clear reason"), Preview.Reason.IsEmpty());
	TestEqual(TEXT("Unsupported frame preview preserves stock"), T.Model->GetRemainingQuantity(TEXT("Bone")), 12);
	TestTrue(TEXT("Unsupported frame preview installs no body"), T.Model->GetBones().IsEmpty());
	return true;
}

#endif
