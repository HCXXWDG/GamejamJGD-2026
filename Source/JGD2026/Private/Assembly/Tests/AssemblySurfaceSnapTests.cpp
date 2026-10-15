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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySurfaceSnapConnectionPolicyTest, "JGD2026.Assembly.SurfaceSnap.RequiresJointAndBoneEndpoints", SurfaceSnapFlags)
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
	TestTrue(TEXT("A legal middle-surface placement remains free instead of acquiring an endpoint"), Midpoint.bValid && !Midpoint.SurfaceSnap.bSnapped && Midpoint.LocalPosition.Equals(FVector2D(200,50),0.001));
	TestTrue(TEXT("A middle-surface tangent may be staged without an endpoint constraint"), Model->TryInstallJointPart(TEXT("Joint_Round"), FVector2D(200,38.4), 0, Joint, Reason));
	TestFalse(TEXT("The old bone-middle contact does not create an edge"), Model->GetJoints().ContainsByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J,Bone,Joint); }));
	TestFalse(TEXT("A non-rooted set of touching bodies cannot start Playing"),Model->TryStartPlaying(Reason));
	TestTrue(TEXT("Remove the staged middle joint"),Model->RemoveJointPart(Joint,Reason));
	// Move the second bone away so both permitted endpoint candidates are unobstructed.
	TestTrue(TEXT("Separate the other staged bone"),Model->TryMoveFreeBone(AdjacentBone,FVector2D(500,0),0,Reason));
	const FAssemblyPlacementResult Endpoint = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"),FVector2D(274,0),0,FGuid(),FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("A joint acquires the right bone endpoint"),Endpoint.bValid && Endpoint.SurfaceSnap.bSnapped && Endpoint.SurfaceSnap.TargetId == Bone)) { return false; }
	TestTrue(TEXT("The target anchor is the actual right endpoint"),Endpoint.SurfaceSnap.TargetLocalAnchor.Equals(FVector2D(32,0),0.001));
	TestTrue(TEXT("Joint center lies on the bone extension with its rim touching the endpoint"),Endpoint.LocalPosition.Equals(FVector2D(264,0),0.001));
	TestTrue(TEXT("Install the resolved endpoint joint"),Model->TryInstallJointPart(TEXT("Joint_Round"),Endpoint.LocalPosition,Endpoint.RotationDegrees,Joint,Reason));
	TestTrue(TEXT("Bone-joint endpoint contact creates a real constraint"),Model->GetJoints().ContainsByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J,Bone,Joint); }));
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
	const FVector Axis3D = Angled.LocalVisualTransform.TransformVectorNoScale(FVector::ForwardVector);
	const FVector2D Axis(Axis3D.X, Axis3D.Z);
	const FVector2D Radial = (Angled.LocalPosition - FVector2D(200, 0)).GetSafeNormal();
	TestTrue(TEXT("A circle contact turns the bone long axis through the circle center"), FMath::Abs(FVector2D::CrossProduct(Axis, Radial)) < 0.001);
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
	const FAssemblyPlacementResult RectangleContact = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Bar"), FVector2D(260,0), 0, FGuid(), FAssemblySurfaceSnapState());
	TestTrue(TEXT("A shallow intrusion at a true endpoint resolves to a collinear exact contact"), RectangleContact.bValid && RectangleContact.SurfaceSnap.bSnapped && RectangleContact.SurfaceSnap.TargetId == Bone && RectangleContact.LocalPosition.Equals(FVector2D(264,0), 0.001));
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
	TArray<FAssemblyBoneInstance> Bodies = { SurfaceBody(BoneId, TEXT("Bone"), FVector2D(200,0)), SurfaceBody(RoundId, TEXT("Joint_Round"), FVector2D(264,0)) };
	TArray<FAssemblyJointInstance> Original = AssemblyFreeRules::BuildJoints(Settings, Definitions, Bodies, {});
	if (!TestEqual(TEXT("Complete facing surfaces still create only one constraint per pair"), Original.Num(), 1)) { return false; }
	Original[0].Kind = EAssemblyJointKind::BreakableHinge;
	Original[0].MinAngleDegrees = -12;
	Original[0].MaxAngleDegrees = 27;
	Bodies[1] = SurfaceBody(RoundId, TEXT("Joint_Round"), FVector2D(276,0));
	const TArray<FAssemblyJointInstance> Sliding = AssemblyFreeRules::BuildJoints(Settings, Definitions, Bodies, Original);
	if (!TestEqual(TEXT("A sliding surface contact keeps one constraint"), Sliding.Num(), 1)) { return false; }
	TestTrue(TEXT("Sliding preserves persistent edge ID and customized type"), Sliding[0].InstanceId == Original[0].InstanceId && Sliding[0].Kind == EAssemblyJointKind::BreakableHinge);
	TestTrue(TEXT("Sliding preserves both angular limits"), Sliding[0].MinAngleDegrees == -12 && Sliding[0].MaxAngleDegrees == 27);
	const FVector2D OldBoneAnchor = Original[0].BoneA == BoneId ? Original[0].LocalAnchorA : Original[0].LocalAnchorB;
	const FVector2D NewBoneAnchor = Sliding[0].BoneA == BoneId ? Sliding[0].LocalAnchorA : Sliding[0].LocalAnchorB;
	TestTrue(TEXT("The bone connection remains at the endpoint while sliding along its extension"), OldBoneAnchor.Equals(FVector2D(32,0),0.001) && NewBoneAnchor.Equals(OldBoneAnchor,0.001));
	const FVector2D NewRoundAnchor = Sliding[0].BoneA == RoundId ? Sliding[0].LocalAnchorA : Sliding[0].LocalAnchorB;
	TestTrue(TEXT("The connector anchor remains on its real boundary despite the tolerated axial gap"),NewRoundAnchor.Equals(FVector2D(-32,0),0.001));
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
	// Leave enough room for the newly radial bone between both circles while retaining equal raw gaps.
	TArray<FAssemblyBoneInstance> Targets = { SurfaceBody(High, TEXT("Joint_Round"), FVector2D(270,0)), SurfaceBody(Low, TEXT("Joint_Round"), FVector2D(130,0)) };
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
	TestFalse(TEXT("A blocked radial contact is rejected instead of falling back to the old horizontal pose"), Blocked.bValid);
	TestFalse(TEXT("A blocked radial contact is not shown as snapped"), Blocked.SurfaceSnap.bSnapped);
	TestFalse(TEXT("Blocked radial contact explains the rejected preview"), Blocked.Reason.IsEmpty());
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
		const FVector RawLocal3D = Frame.InverseTransformPosition(FVector(138,0,0));
		const FVector2D RawLocal(RawLocal3D.X, RawLocal3D.Z);
		TestTrue(TEXT("Begin a new standalone joint drag"), T.Interaction->BeginBoneDrag(TEXT("Joint_Round"), FGuid(), T.Reason));
		if (!TestTrue(TEXT("Current frame snapshot snaps to the actual visible bone surface"), T.Interaction->UpdateBoneDragAtLocalPosition(RawLocal, T.Reason))) { AddError(T.Reason.ToString()); return false; }
		const FAssemblyPlacementResult Preview = T.Interaction->GetBoneDragPreview();
		TestTrue(TEXT("Snap target is the existing visible bone ID"), Preview.SurfaceSnap.bSnapped && !Preview.SurfaceSnap.bCore && Preview.SurfaceSnap.TargetId == Bone);
		const FTransform ExpectedWorld = Preview.LocalVisualTransform * T.Model->GetGridFrameTransform();
		TestTrue(TEXT("Contact uses the visible bone endpoint and collinear world-space joint center"), ExpectedWorld.GetLocation().Equals(FVector(128,0,0), 0.001));
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
		TestTrue(TEXT("Constraint anchors coincide at the visible bone endpoint after rebasing"), AnchorA.Equals(AnchorB, 0.001) && AnchorA.Equals(FVector(96,0,0), 0.001));
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
	const FAssemblyPlacementResult RoundPreview = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"), FVector2D(202,0), 0, FGuid(), FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("Round snaps onto the root bone's outer endpoint"), RoundPreview.bValid && RoundPreview.SurfaceSnap.bSnapped && RoundPreview.SurfaceSnap.TargetId == RootBone && RoundPreview.LocalPosition.Equals(FVector2D(192,0),0.001))) { return false; }
	if (!TestTrue(TEXT("Commit endpoint joint piece"), Model->TryInstallJointPart(TEXT("Joint_Round"), RoundPreview.LocalPosition, RoundPreview.RotationDegrees, Round, Reason))) { return false; }
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FAssemblyPlacementResult Preview = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), FVector2D(262,Side == 0 ? 70 : -70), Side == 0 ? -45 : 45, FGuid(), FAssemblySurfaceSnapState());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialCoreQuadrantsTest, "JGD2026.Assembly.SurfaceSnap.Radial.CoreSideTopAndFourQuadrants", SurfaceSnapFlags)
bool FAssemblyRadialCoreQuadrantsTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel();
	const TArray<FVector2D> PointerPositions = { FVector2D(74,0), FVector2D(0,48), FVector2D(-74,0), FVector2D(0,-48),
		FVector2D(42,42), FVector2D(-42,42), FVector2D(-42,-42), FVector2D(42,-42) };
	FText Reason;
	for (const FVector2D Pointer : PointerPositions)
	{
		const FAssemblyPlacementResult Pose = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"), Pointer, 0, FGuid(), FAssemblySurfaceSnapState());
		if (!TestTrue(FString::Printf(TEXT("A horizontal bone acquires the circle near pointer %.1f, %.1f"), Pointer.X, Pointer.Y), Pose.bValid && Pose.SurfaceSnap.bSnapped && Pose.SurfaceSnap.bCore)) { continue; }
		const FVector Axis3D = Pose.LocalVisualTransform.TransformVectorNoScale(FVector::ForwardVector);
		const FVector2D Axis(Axis3D.X, Axis3D.Z);
		const FVector2D Radial = Pose.LocalPosition.GetSafeNormal();
		TestTrue(TEXT("The bone long axis passes through the core center in every quadrant"), FMath::Abs(FVector2D::CrossProduct(Axis, Radial)) < 0.001);
		TestTrue(TEXT("Automatic rotation keeps the displayed body outside the core"), FMath::IsNearlyEqual(Pose.LocalPosition.Size(), 64.0, 0.001));
		TestTrue(TEXT("The target anchor remains exactly on the core circumference"), FMath::IsNearlyEqual(Pose.SurfaceSnap.TargetLocalAnchor.Size(), 32.0, 0.001));
		const FVector SourceAnchor3D = Pose.LocalVisualTransform.TransformPosition(FVector(Pose.SurfaceSnap.SourceLocalAnchor.X,0,Pose.SurfaceSnap.SourceLocalAnchor.Y));
		TestTrue(TEXT("Both reported anchors coincide at the shown rim"), FVector2D(SourceAnchor3D.X,SourceAnchor3D.Z).Equals(Pose.SurfaceSnap.TargetLocalAnchor,0.001));
		TestTrue(TEXT("Exactly the radial preview remains legal for an actual drop"), Model->ValidateFreeBonePlacement(TEXT("Bone"), Pose.LocalPosition, Pose.RotationDegrees, FGuid()).bValid);
	}
	TestEqual(TEXT("Radial preview calculations do not consume inventory"), Model->GetRemainingQuantity(TEXT("Bone")), 12);
	TestTrue(TEXT("Radial preview calculations create no installed instances"), Model->GetBones().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialFixedCircleTest, "JGD2026.Assembly.SurfaceSnap.Radial.FixedCircleAndExactCommit", SurfaceSnapFlags)
bool FAssemblyRadialFixedCircleTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition = FVector2D(-1000,0);
	const FAssemblyBoneDefinition Fixed = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::FixedRound,64.0f/640.0f);
	FText Reason;
	if (!TestTrue(TEXT("Configure a yellow fixed circle target"), Model->ConfigureFreeAssembly(Settings,{ShortSurfaceBone(),Fixed},{},Reason))) { return false; }
	FGuid Target, Bone;
	if (!TestTrue(TEXT("Stage the fixed circle without quantizing its angle"), Model->TryInstallJointPart(Fixed.TypeId,FVector2D(200,0),23.5f,Target,Reason))) { return false; }
	const FAssemblyPlacementResult Preview = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(200,48),0,FGuid(),FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("The top of a fixed round joint also acquires radial alignment"),Preview.bValid && Preview.SurfaceSnap.bSnapped && Preview.SurfaceSnap.TargetId == Target)) { return false; }
	const FVector Axis = Preview.LocalVisualTransform.TransformVectorNoScale(FVector::ForwardVector);
	TestTrue(TEXT("A horizontal bone becomes vertical above a fixed circular joint"),FMath::Abs(Axis.X) < 0.001 && FMath::Abs(Axis.Z) > 0.999);
	TestTrue(TEXT("The installed target rotation does not move the circular rim"),Preview.LocalPosition.Equals(FVector2D(200,64),0.001));
	TestTrue(TEXT("Drop commits the exact rotated preview"),Model->TryInstallFreeBone(TEXT("Bone"),Preview.LocalPosition,Preview.RotationDegrees,Bone,Reason));
	FAssemblyBoneInstance Installed;
	TestTrue(TEXT("The committed instance preserves preview position and angle"),Model->GetBone(Bone,Installed) && Installed.LocalVisualTransform.Equals(Preview.LocalVisualTransform,0.001));
	TestEqual(TEXT("A successful radial drop consumes exactly one bone"),Model->GetRemainingQuantity(TEXT("Bone")),11);
	const TArray<FAssemblyJointInstance> Edges = Model->GetJoints();
	const FAssemblyJointInstance* Edge = Edges.FindByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J,Target,Bone); });
	TestNotNull(TEXT("The radial drop produces a real bone-to-joint edge"),Edge);
	if (Edge) { TestEqual(TEXT("Yellow joint preserves fixed physical semantics"),Edge->Kind,EAssemblyJointKind::Fixed); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialCustomContourTest, "JGD2026.Assembly.SurfaceSnap.Radial.CustomContourTrueTangent", SurfaceSnapFlags)
bool FAssemblyRadialCustomContourTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
	FAssemblyBoneDefinition Bone = ShortSurfaceBone();
	Bone.SurfaceOutline = { FVector2D(-24,0),FVector2D(-18,-3),FVector2D(15,-8),FVector2D(24,0),FVector2D(15,10),FVector2D(-18,3) };
	FText Reason;
	if (!TestTrue(TEXT("Configure an asymmetric bone contour"),Model->ConfigureFreeAssembly(FAssemblyFreeSettings(),{Bone},{},Reason))) { return false; }
	const FAssemblyPlacementResult Preview = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(0,48),0,FGuid(),FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("An asymmetric bone can acquire the top of the core"),Preview.bValid && Preview.SurfaceSnap.bSnapped && Preview.SurfaceSnap.bCore)) { return false; }
	const FVector Axis3D = Preview.LocalVisualTransform.TransformVectorNoScale(FVector::ForwardVector);
	TestTrue(TEXT("The configured bone local X axis points through the circle center"),FMath::Abs(FVector2D::CrossProduct(FVector2D(Axis3D.X,Axis3D.Z),Preview.LocalPosition.GetSafeNormal())) < 0.001);
	AssemblySurfaceGeometry::FShape Shape;
	if (!TestTrue(TEXT("Build the actual custom outline at the preview pose"),AssemblySurfaceGeometry::BuildShape(Bone,Preview.LocalVisualTransform,Shape,Reason))) { return false; }
	const AssemblySurfaceGeometry::FShape Core = AssemblySurfaceGeometry::MakeCircle(FVector2D::ZeroVector,32);
	TestFalse(TEXT("Radial placement does not penetrate the circle with a custom contour"),AssemblySurfaceGeometry::Overlaps(Shape,Core));
	AssemblySurfaceGeometry::FContact Contact;
	TestTrue(TEXT("The actual contour, rather than just a width estimate, touches the rim"),AssemblySurfaceGeometry::ClosestContact(Shape,Core,Contact) && Contact.DistanceSquared < 0.000004);
	TestTrue(TEXT("The contacted endpoint comes from the custom outline rather than nominal half-length"),FMath::IsNearlyEqual(FMath::Abs(Preview.SurfaceSnap.SourceLocalAnchor.X),24.0,0.001) && FMath::Abs(Preview.SurfaceSnap.SourceLocalAnchor.Y)<0.001);
	TestTrue(TEXT("Custom contour radial placement passes the shared drop validator"),Model->ValidateFreeBonePlacement(TEXT("Bone"),Preview.LocalPosition,Preview.RotationDegrees,FGuid()).bValid);
	TestEqual(TEXT("Custom contour previews preserve inventory"),Model->GetRemainingQuantity(TEXT("Bone")),12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialOrbitLatchTest, "JGD2026.Assembly.SurfaceSnap.Radial.LongBoneOrbitKeepsEndAndReleaseThreshold", SurfaceSnapFlags)
bool FAssemblyRadialOrbitLatchTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
	FAssemblyBoneDefinition Bone = ShortSurfaceBone();
	Bone.BoneParameters.Length = EBoneLengthTier::Long;
	FText Reason;
	if (!TestTrue(TEXT("Configure a long bone"),Model->ConfigureFreeAssembly(FAssemblyFreeSettings(),{Bone},{},Reason))) { return false; }
	FAssemblyPlacementResult Pose = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(0,48),0,FGuid(),FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("A horizontal long bone touching by its side acquires the top of the core"),Pose.bValid && Pose.SurfaceSnap.bSnapped)) { return false; }
	TestTrue(TEXT("The rotated long bone clears the circle at its real half-length"),Pose.LocalPosition.Equals(FVector2D(0,96),0.001));
	const bool bInitialEnd = Pose.SurfaceSnap.bRadialXOutward;
	FVector PreviousAxis = Pose.LocalVisualTransform.TransformVectorNoScale(FVector::ForwardVector);
	for (int32 Step = 1; Step <= 72; ++Step)
	{
		const double Radians = FMath::DegreesToRadians(90.0 + 5.0 * Step);
		const FVector2D Radial(FMath::Cos(Radians),FMath::Sin(Radians));
		const FAssemblyPlacementResult Next = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),Radial*48,0,FGuid(),Pose.SurfaceSnap);
		if (!TestTrue(FString::Printf(TEXT("The latched long bone follows orbit step %d despite its unchanged free angle"),Step),Next.bValid && Next.SurfaceSnap.bSnapped && Next.SurfaceSnap.bCore)) { return false; }
		const FVector Axis = Next.LocalVisualTransform.TransformVectorNoScale(FVector::ForwardVector);
		TestTrue(TEXT("The same physical end remains attached around the complete circle"),Next.SurfaceSnap.bRadialXOutward == bInitialEnd);
		TestTrue(TEXT("Orbiting by five degrees cannot flip the bone by 180 degrees"),FVector::DotProduct(PreviousAxis,Axis) > 0.99);
		TestTrue(TEXT("The long bone center remains radial and exactly outside the circle"),Next.LocalPosition.Equals(Radial*96,0.001));
		PreviousAxis = Axis;
		Pose = Next;
	}
	const FAssemblyPlacementResult Held = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(0,70.3),0,FGuid(),Pose.SurfaceSnap);
	TestTrue(TEXT("A 31.9-unit pull from the original side-contact baseline stays latched"),Held.bValid && Held.SurfaceSnap.bSnapped);
	const FAssemblyPlacementResult Released = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(0,71.4),0,FGuid(),Held.SurfaceSnap);
	TestTrue(TEXT("A 33-unit pull releases instead of comparing the old horizontal width"),Released.bValid && !Released.SurfaceSnap.bSnapped && Released.LocalPosition.Equals(FVector2D(0,71.4),0.001));
	TestEqual(TEXT("Detaching restores the user's free horizontal angle"),Released.RotationDegrees,0.0f);
	TestEqual(TEXT("Orbit and detach do not consume stock"),Model->GetRemainingQuantity(TEXT("Bone")),12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialScopeTest, "JGD2026.Assembly.SurfaceSnap.Radial.NonCircularPortsAlignAndFreeJointDragsKeepManualAngle", SurfaceSnapFlags)
bool FAssemblyRadialScopeTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition = FVector2D(-1000,0);
	const FAssemblyBoneDefinition Bar = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar,64.0f/640.0f);
	FText Reason;
	if (!TestTrue(TEXT("Configure a noncircular target"),Model->ConfigureFreeAssembly(Settings,{ShortSurfaceBone(),Bar},{},Reason))) { return false; }
	FGuid Target;
	const double Angle = FMath::DegreesToRadians(37.25);
	const FVector2D Axis(FMath::Cos(Angle),-FMath::Sin(Angle));
	if (!TestTrue(TEXT("Install a yellow bar with a facing end at the same arbitrary angle"),Model->TryInstallJointPart(Bar.TypeId,FVector2D(200,0),37.25f,Target,Reason))) { return false; }
	TestFalse(TEXT("A center-aligned bone whose flat end cuts across the bar face cannot snap through it"),Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(264,0),0,FGuid(),FAssemblySurfaceSnapState()).bValid);
	const FAssemblyPlacementResult NonCircle = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(200,0)+Axis*64,37.25f,FGuid(),FAssemblySurfaceSnapState());
	TestTrue(TEXT("The arbitrary-angle bone still snaps to a bar surface"),NonCircle.bValid && NonCircle.SurfaceSnap.bSnapped && NonCircle.SurfaceSnap.TargetId == Target);
	TestEqual(TEXT("An already aligned bone retains the requested angle"),NonCircle.RotationDegrees,37.25f);
	const FAssemblyPlacementResult AutoAligned = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(200,0)+Axis*64,0,FGuid(),FAssemblySurfaceSnapState());
	TestTrue(TEXT("A noncircular end port now rotates a nearby horizontal bone into its authored axis"),AutoAligned.bValid && AutoAligned.SurfaceSnap.bSnapped
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(AutoAligned.RotationDegrees,37.25f)) < 0.001);
	TestTrue(TEXT("The bar center also lies on the arbitrarily rotated bone extension"),FMath::Abs(FVector2D::CrossProduct(NonCircle.LocalPosition-FVector2D(200,0),Axis))<0.001);
	TestFalse(TEXT("A noncircular surface cannot claim radial alignment"),NonCircle.SurfaceSnap.bRadialAligned);
	FGuid AngledBone;
	if (!TestTrue(TEXT("The reverse bone-to-noncircular-connector preview commits exactly"),Model->TryInstallFreeBone(TEXT("Bone"),NonCircle.LocalPosition,NonCircle.RotationDegrees,AngledBone,Reason))) { AddError(Reason.ToString()); return false; }
	FAssemblyBoneInstance Committed;
	TestTrue(TEXT("Installed noncircular-target bone retains the displayed arbitrary pose"),Model->GetBone(AngledBone,Committed) && Committed.LocalVisualTransform.Equals(NonCircle.LocalVisualTransform,0.001));
	TestTrue(TEXT("Reverse dragging creates the same permitted endpoint edge"),Model->GetJoints().ContainsByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J,Target,AngledBone); }));
	Model = MakeSurfaceModel();
	const FAssemblyPlacementResult Joint = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"),FVector2D(0,74),37.25f,FGuid(),FAssemblySurfaceSnapState());
	TestTrue(TEXT("A circular joint near the core remains a free unconnected placement"),Joint.bValid && !Joint.SurfaceSnap.bSnapped && Joint.LocalPosition.Equals(FVector2D(0,74),0.001));
	TestEqual(TEXT("Dragging a joint part does not auto-rotate its artwork"),Joint.RotationDegrees,37.25f);
	TestFalse(TEXT("Only ordinary bone drags activate radial alignment"),Joint.SurfaceSnap.bRadialAligned);
	FAssemblyFreeSettings Disabled;
	Disabled.bEnableSurfaceSnapping = false;
	TestTrue(TEXT("Disable surface snapping"),Model->ConfigureFreeAssembly(Disabled,{ShortSurfaceBone()},{},Reason));
	const FAssemblyPlacementResult Free = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(0,48),0,FGuid(),Joint.SurfaceSnap);
	TestTrue(TEXT("Disabling snapping also disables automatic alignment"),Free.bValid && !Free.SurfaceSnap.bSnapped && Free.LocalPosition.Equals(FVector2D(0,48),0.001));
	TestEqual(TEXT("The disabled tool keeps the original horizontal angle"),Free.RotationDegrees,0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialDragRotationTest, "JGD2026.AssemblyInteraction.SurfaceSnap.Radial.ManualRotationTickDetachAndCancel", SurfaceSnapFlags)
bool FAssemblyRadialDragRotationTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize radial interaction world"),T.bReady)) { return false; }
	FGuid Installed;
	if (!TestTrue(TEXT("Install one bone on the right side of the core"),T.Interaction->BeginBoneDrag(TEXT("Bone"),FGuid(),T.Reason)
		&& T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(74,0),T.Reason)
		&& T.Interaction->CommitBoneDrag(Installed,T.Reason))) { AddError(T.Reason.ToString()); return false; }
	ANormalBone* Actor = T.Interaction->GetBoneActor(Installed);
	if (!TestNotNull(TEXT("Installed body has a real actor"),Actor)) { return false; }
	const FTransform Original = Actor->GetActorTransform();
	TestTrue(TEXT("Drag an existing installed bone"),T.Interaction->BeginBoneDrag(TEXT("Bone"),Installed,T.Reason));
	TestTrue(TEXT("Acquire the right-hand circle rim"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(74,0),T.Reason));
	const bool bFirstEnd = T.Interaction->GetBoneDragPreview().SurfaceSnap.bRadialXOutward;
	TestTrue(TEXT("Manual R-equivalent rotation remains available while latched"),T.Interaction->RotateBoneDrag(180,T.Reason));
	const FAssemblyPlacementResult Rotated = T.Interaction->GetBoneDragPreview();
	TestTrue(TEXT("A manual half-turn intentionally selects the opposite attachment end"),Rotated.bValid && Rotated.SurfaceSnap.bSnapped && Rotated.SurfaceSnap.bRadialXOutward != bFirstEnd);
	for (int32 I=0;I<3;++I) { static_cast<UActorComponent*>(T.Interaction)->TickComponent(0.016f,LEVELTICK_All,nullptr); }
	TestTrue(TEXT("Ticks never feed the automatic display angle back as another user rotation"),T.Interaction->GetBoneDragPreview().LocalVisualTransform.Equals(Rotated.LocalVisualTransform,0.001));
	TestTrue(TEXT("Pull beyond the 32-unit release distance"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(97,0),T.Reason));
	const FAssemblyPlacementResult Detached = T.Interaction->GetBoneDragPreview();
	TestTrue(TEXT("Released preview follows the mouse and retains the manual half-turn"),Detached.bValid && !Detached.SurfaceSnap.bSnapped && Detached.LocalPosition.Equals(FVector2D(97,0),0.001) && FMath::IsNearlyEqual(FMath::Abs(Detached.RotationDegrees),180.0f,0.001f));
	T.Interaction->CancelBoneDrag();
	TestTrue(TEXT("Cancel restores the original installed actor and angle"),T.Interaction->GetBoneActor(Installed) == Actor && !Actor->IsHidden() && Actor->GetActorTransform().Equals(Original,0.001));
	TestEqual(TEXT("Rotating, detaching and canceling never duplicates or consumes an installed bone"),T.Model->GetRemainingQuantity(TEXT("Bone")),11);
	TestEqual(TEXT("Exactly one installed instance remains"),T.Model->GetBones().Num(),1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialRegisteredCircleFrameTest, "JGD2026.AssemblyInteraction.SurfaceSnap.Radial.RegisteredCirclePoseAndRotatedFrameCommit", SurfaceSnapFlags)
bool FAssemblyRadialRegisteredCircleFrameTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize registered circle fixture"),T.bReady)) { return false; }
	FGuid Round;
	if (!TestTrue(TEXT("Install the independent round target"),T.Model->TryInstallJointPart(TEXT("Joint_Round"),FVector2D(200,0),0,Round,T.Reason))) { return false; }
	ANormalBone* RoundActor = T.Interaction->GetBoneActor(Round);
	USceneComponent* Source = T.Model->GetBonePoseSource(Round);
	if (!TestNotNull(TEXT("Round target exposes a real actor"),RoundActor) || !TestNotNull(TEXT("Round target exposes a registered pose source"),Source)) { return false; }
	const FVector Center(300,0,120);
	const FTransform TargetWorld(FQuat(FVector::YAxisVector,0.71),Center);
	Source->SetWorldTransform(TargetWorld);
	const FTransform Frame(FQuat(FVector::YAxisVector,0.42),FVector(1000,0,300));
	T.Owner->SetActorTransform(Frame);
	const FVector Raw3D = Frame.InverseTransformPosition(Center+FVector(0,0,48));
	TestTrue(TEXT("Begin ordinary bone drag after moving both frame and actual target pose"),T.Interaction->BeginBoneDrag(TEXT("Bone"),FGuid(),T.Reason));
	if (!TestTrue(TEXT("Acquire the actual circle above its visible physical position"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(Raw3D.X,Raw3D.Z),T.Reason))) { AddError(T.Reason.ToString()); return false; }
	const FAssemblyPlacementResult Preview = T.Interaction->GetBoneDragPreview();
	if (!TestTrue(TEXT("The real round target, rather than its stale model position, is selected"),Preview.SurfaceSnap.bSnapped && Preview.SurfaceSnap.TargetId == Round)) { return false; }
	const FTransform PreviewWorld = Preview.LocalVisualTransform*T.Model->GetGridFrameTransform();
	TestTrue(TEXT("Radial placement keeps the visible center exactly one radius plus half-length above the actual target"),PreviewWorld.GetLocation().Equals(Center+FVector(0,0,64),0.001));
	const FVector Axis = PreviewWorld.TransformVectorNoScale(FVector::ForwardVector);
	TestTrue(TEXT("The displayed long axis points through the real world-space circle center"),FMath::Abs(Axis.X)<0.001 && FMath::Abs(Axis.Z)>0.999);
	const FVector AnchorA = PreviewWorld.TransformPosition(FVector(Preview.SurfaceSnap.SourceLocalAnchor.X,0,Preview.SurfaceSnap.SourceLocalAnchor.Y));
	const FVector AnchorB = TargetWorld.TransformPosition(FVector(Preview.SurfaceSnap.TargetLocalAnchor.X,0,Preview.SurfaceSnap.TargetLocalAnchor.Y));
	TestTrue(TEXT("Both preview anchors meet on the actual rotated target's world circumference"),AnchorA.Equals(AnchorB,0.001) && AnchorA.Equals(Center+FVector(0,0,32),0.001));
	FGuid Bone;
	if (!TestTrue(TEXT("Commit the exact radial actor preview through the rotated frame"),T.Interaction->CommitBoneDrag(Bone,T.Reason))) { AddError(T.Reason.ToString()); return false; }
	ANormalBone* BoneActor = T.Interaction->GetBoneActor(Bone);
	TestTrue(TEXT("Committed bone location and angle exactly match the shown preview"),IsValid(BoneActor) && BoneActor->GetActorTransform().Equals(PreviewWorld,0.001));
	TestTrue(TEXT("The existing target does not teleport when the new radial bone is installed"),Source->GetComponentTransform().Equals(TargetWorld,0.001));
	const TArray<FAssemblyJointInstance> Edges = T.Model->GetJoints();
	const FAssemblyJointInstance* Edge = Edges.FindByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J,Round,Bone); });
	if (!TestNotNull(TEXT("Commit creates exactly the intended circle-to-bone constraint"),Edge)) { return false; }
	FTransform WorldA,WorldB;
	TestTrue(TEXT("Both committed actual body world poses remain available"),T.Model->GetBoneWorldTransform(Edge->BoneA,WorldA) && T.Model->GetBoneWorldTransform(Edge->BoneB,WorldB));
	const FVector ActualA = WorldA.TransformPosition(FVector(Edge->LocalAnchorA.X,0,Edge->LocalAnchorA.Y));
	const FVector ActualB = WorldB.TransformPosition(FVector(Edge->LocalAnchorB.X,0,Edge->LocalAnchorB.Y));
	TestTrue(TEXT("Committed constraint anchors match the actual visible radial contact"),ActualA.Equals(ActualB,0.001) && ActualA.Equals(Center+FVector(0,0,32),0.001));
	TestEqual(TEXT("Exactly one bone was consumed"),T.Model->GetRemainingQuantity(TEXT("Bone")),11);
	TestEqual(TEXT("The target joint is neither duplicated nor consumed twice"),T.Model->GetRemainingQuantity(TEXT("Joint_Round")),1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRadialBlockedDropTest, "JGD2026.AssemblyInteraction.SurfaceSnap.Radial.BlockedAlignmentRejectsShelfAndInstalledDrop", SurfaceSnapFlags)
bool FAssemblyRadialBlockedDropTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize blocked radial interaction world"),T.bReady)) { return false; }
	FGuid Blocker,Original;
	if (!TestTrue(TEXT("Install a blocker above the core"),T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(0,90),0,Blocker,T.Reason))) { return false; }
	const FVector2D Pointer(0,38.4);
	TestTrue(TEXT("The old horizontal raw pose touches the circle without overlapping the blocker"),T.Model->ValidateFreeBonePlacement(TEXT("Bone"),Pointer,0,FGuid()).bValid);
	TestFalse(TEXT("The requested vertical radial pose really intersects the existing blocker"),T.Model->ValidateFreeBonePlacement(TEXT("Bone"),FVector2D(0,64),90,FGuid()).bValid);
	TestTrue(TEXT("Begin a new shelf bone"),T.Interaction->BeginBoneDrag(TEXT("Bone"),FGuid(),T.Reason));
	TestFalse(TEXT("A blocked radial placement cannot silently fall back to a horizontal snap"),T.Interaction->UpdateBoneDragAtLocalPosition(Pointer,T.Reason));
	TestFalse(TEXT("Blocked alignment has a clear player-facing reason"),T.Reason.IsEmpty());
	FGuid Failed;
	TestFalse(TEXT("Invalid shelf release is refused"),T.Interaction->CommitBoneDrag(Failed,T.Reason));
	TestTrue(TEXT("Shelf failure keeps stock and the only original instance"),!Failed.IsValid() && T.Model->GetRemainingQuantity(TEXT("Bone")) == 11 && T.Model->GetBones().Num() == 1);
	if (!TestTrue(TEXT("Install another freely placed bone for move rollback"),T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(400,0),0,Original,T.Reason))) { return false; }
	ANormalBone* OriginalActor = T.Interaction->GetBoneActor(Original);
	if (!TestNotNull(TEXT("The moved existing bone has a real view"),OriginalActor)) { return false; }
	const FTransform OriginalPose = OriginalActor->GetActorTransform();
	TestTrue(TEXT("Begin moving the existing instance"),T.Interaction->BeginBoneDrag(TEXT("Bone"),Original,T.Reason));
	TestFalse(TEXT("The same blocker also rejects an installed drag"),T.Interaction->UpdateBoneDragAtLocalPosition(Pointer,T.Reason));
	TestFalse(TEXT("Rejected installed release cannot overwrite the original instance"),T.Interaction->CommitBoneDrag(Failed,T.Reason));
	TestTrue(TEXT("The original bone returns to its exact visible pose"),T.Interaction->GetBoneActor(Original) == OriginalActor && !OriginalActor->IsHidden() && OriginalActor->GetActorTransform().Equals(OriginalPose,0.001));
	TestTrue(TEXT("No inventory or instance is lost or duplicated by either rejected operation"),!Failed.IsValid() && T.Model->GetRemainingQuantity(TEXT("Bone")) == 10 && T.Model->GetBones().Num() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyEndpointConnectorPolicyTest, "JGD2026.Assembly.SurfaceSnap.Endpoints.ConnectorPairsCannotConnectOrStart", SurfaceSnapFlags)
bool FAssemblyEndpointConnectorPolicyTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel();
	FText Reason;
	FGuid First, Second;
	const FAssemblyPlacementResult NearCore = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"),FVector2D(74,0),12.5f,FGuid(),FAssemblySurfaceSnapState());
	TestTrue(TEXT("A joint near the core is free and retains its manual angle"),NearCore.bValid && !NearCore.SurfaceSnap.bSnapped && NearCore.LocalPosition.Equals(FVector2D(74,0),0.001) && NearCore.RotationDegrees==12.5f);
	if (!TestTrue(TEXT("Stage a joint tangent to the core"),Model->TryInstallJointPart(TEXT("Joint_Round"),FVector2D(64,0),0,First,Reason))) { return false; }
	TestTrue(TEXT("Core-joint contact creates no constraint"),Model->GetJoints().IsEmpty());
	const FAssemblyPlacementResult NearJoint = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"),FVector2D(138,0),27,FGuid(),FAssemblySurfaceSnapState());
	TestTrue(TEXT("Another joint does not snap to an already installed joint"),NearJoint.bValid && !NearJoint.SurfaceSnap.bSnapped && NearJoint.RotationDegrees==27);
	TestTrue(TEXT("Stage the second tangent joint"),Model->TryInstallJointPart(TEXT("Joint_Round"),FVector2D(128,0),0,Second,Reason));
	TestTrue(TEXT("Neither joint-joint nor joint-core edges are generated"),Model->GetJoints().IsEmpty());
	TestFalse(TEXT("A chain containing only connectors is not connected to the core"),Model->IsStructureConnected());
	TestFalse(TEXT("Geometric touching cannot let a connector-only chain start Playing"),Model->TryStartPlaying(Reason));
	TestTrue(TEXT("Rejected start retains Assembly, both instances and exact stock"),Model->GetPhase()==EAssemblyPhase::Assembly && Model->GetBones().Num()==2 && Model->GetRemainingQuantity(TEXT("Joint_Round"))==0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointEndpointLatchTest, "JGD2026.Assembly.SurfaceSnap.Endpoints.JointBothEndsAngleAndRelease", SurfaceSnapFlags)
bool FAssemblyJointEndpointLatchTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeSurfaceModel(true);
	FText Reason;
	FGuid Bone;
	const FVector2D Center(200,100);
	constexpr float BoneAngle = 37.25f;
	const double Radians = FMath::DegreesToRadians(static_cast<double>(BoneAngle));
	const FVector2D Axis(FMath::Cos(Radians),-FMath::Sin(Radians));
	if (!TestTrue(TEXT("Install an arbitrarily rotated target bone"),Model->TryInstallFreeBone(TEXT("Bone"),Center,BoneAngle,Bone,Reason))) { return false; }
	for (int32 Sign : {-1,1})
	{
		const FVector2D Outward = Axis*Sign;
		const FAssemblyPlacementResult First = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"),Center+Outward*74,19.5f,FGuid(),FAssemblySurfaceSnapState());
		if (!TestTrue(TEXT("Either actual bone endpoint acquires a connector"),First.bValid && First.SurfaceSnap.bSnapped && First.SurfaceSnap.TargetId==Bone)) { return false; }
		TestTrue(TEXT("Resolved joint center is on the angled bone extension"),First.LocalPosition.Equals(Center+Outward*64,0.001));
		TestTrue(TEXT("The selected anchor identifies the correct signed endpoint"),First.SurfaceSnap.TargetLocalAnchor.Equals(FVector2D(Sign*32,0),0.001));
		TestEqual(TEXT("Joint artwork keeps its manual angle"),First.RotationDegrees,19.5f);
		const FAssemblyPlacementResult Held = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"),Center+Outward*85,19.5f,FGuid(),First.SurfaceSnap);
		TestTrue(TEXT("A 21-unit endpoint pull keeps the same latch and signed endpoint"),Held.bValid && Held.SurfaceSnap.bSnapped && Held.LocalPosition.Equals(First.LocalPosition,0.001) && Held.SurfaceSnap.TargetLocalAnchor.Equals(First.SurfaceSnap.TargetLocalAnchor,0.001));
		const FAssemblyPlacementResult Released = Model->ResolveFreeBoneDragPlacement(TEXT("Joint_Round"),Center+Outward*97,19.5f,FGuid(),Held.SurfaceSnap);
		TestTrue(TEXT("A 33-unit endpoint pull detaches and follows the pointer"),Released.bValid && !Released.SurfaceSnap.bSnapped && Released.LocalPosition.Equals(Center+Outward*97,0.001));
	}
	TestEqual(TEXT("All endpoint previews preserve joint inventory"),Model->GetRemainingQuantity(TEXT("Joint_Round")),2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointEndpointRegisteredPoseTest, "JGD2026.AssemblyInteraction.SurfaceSnap.Endpoints.RegisteredPosePreviewCommitAndCancel", SurfaceSnapFlags)
bool FAssemblyJointEndpointRegisteredPoseTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize endpoint presenter"),T.bReady)) { return false; }
	FGuid Bone;
	if (!TestTrue(TEXT("Stage the actual target bone"),T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(200,0),0,Bone,T.Reason))) { return false; }
	ANormalBone* BoneActor = T.Interaction->GetBoneActor(Bone);
	USceneComponent* Source = T.Model->GetBonePoseSource(Bone);
	if (!TestNotNull(TEXT("The target exposes its actual world pose source"),Source) || !TestNotNull(TEXT("The target has an actual actor"),BoneActor)) { return false; }
	const FTransform BoneWorld(FQuat(FVector::YAxisVector,FMath::DegreesToRadians(37.25)),FVector(300,0,120));
	const FVector Axis = BoneWorld.TransformVectorNoScale(FVector::ForwardVector);
	Source->SetWorldTransform(BoneWorld);
	const FTransform Frame(FQuat(FVector::YAxisVector,0.42),FVector(1000,0,300));
	T.Owner->SetActorTransform(Frame);
	const FVector Raw = Frame.InverseTransformPosition(BoneWorld.GetLocation()+Axis*74);
	TestTrue(TEXT("Begin a new connector shelf drag"),T.Interaction->BeginBoneDrag(TEXT("Joint_Round"),FGuid(),T.Reason));
	if (!TestTrue(TEXT("Project pointer to the actual angled target endpoint"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(Raw.X,Raw.Z),T.Reason))) { AddError(T.Reason.ToString()); return false; }
	TestTrue(TEXT("A connector can be rotated manually while attached to an endpoint"),T.Interaction->RotateBoneDrag(19.5f,T.Reason));
	const FAssemblyPlacementResult Preview = T.Interaction->GetBoneDragPreview();
	const FTransform PreviewWorld = Preview.LocalVisualTransform*Frame;
	TestTrue(TEXT("The preview follows the current registered bone rather than stale model coordinates"),Preview.SurfaceSnap.bSnapped && Preview.SurfaceSnap.TargetId==Bone && PreviewWorld.GetLocation().Equals(BoneWorld.GetLocation()+Axis*64,0.001));
	TestTrue(TEXT("Endpoint preview uses the true body-local endpoint"),Preview.SurfaceSnap.TargetLocalAnchor.Equals(FVector2D(32,0),0.001));
	FGuid Joint;
	if (!TestTrue(TEXT("Commit the exact endpoint preview"),T.Interaction->CommitBoneDrag(Joint,T.Reason))) { AddError(T.Reason.ToString()); return false; }
	ANormalBone* JointActor = T.Interaction->GetBoneActor(Joint);
	if (!TestNotNull(TEXT("Committed connector has a real actor"),JointActor)) { return false; }
	TestTrue(TEXT("Actual installed transform equals the preview through a rotated frame"),JointActor->GetActorTransform().Equals(PreviewWorld,0.001));
	TestTrue(TEXT("Installing the connector leaves the existing bone at its actual world pose"),Source->GetComponentTransform().Equals(BoneWorld,0.001));
	const TArray<FAssemblyJointInstance> Edges = T.Model->GetJoints();
	const FAssemblyJointInstance* Edge = Edges.FindByPredicate([=](const FAssemblyJointInstance& J) { return IsSurfacePair(J,Bone,Joint); });
	if (!TestNotNull(TEXT("Exactly the intended bone-connector constraint exists"),Edge)) { return false; }
	FTransform WorldA,WorldB;
	TestTrue(TEXT("Actual body world poses are available"),T.Model->GetBoneWorldTransform(Edge->BoneA,WorldA) && T.Model->GetBoneWorldTransform(Edge->BoneB,WorldB));
	const FVector AnchorA = WorldA.TransformPosition(FVector(Edge->LocalAnchorA.X,0,Edge->LocalAnchorA.Y));
	const FVector AnchorB = WorldB.TransformPosition(FVector(Edge->LocalAnchorB.X,0,Edge->LocalAnchorB.Y));
	TestTrue(TEXT("Both installed anchors coincide at the real bone endpoint"),AnchorA.Equals(AnchorB,0.001) && AnchorA.Equals(BoneWorld.GetLocation()+Axis*32,0.001));
	const FTransform Original = JointActor->GetActorTransform();
	TestTrue(TEXT("Begin moving the installed connector"),T.Interaction->BeginBoneDrag(TEXT("Joint_Round"),Joint,T.Reason));
	TestTrue(TEXT("Move the connector far from all endpoints"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(800,400),T.Reason));
	T.Interaction->CancelBoneDrag();
	TestTrue(TEXT("Cancel restores the same actor and exact endpoint pose"),T.Interaction->GetBoneActor(Joint)==JointActor && !JointActor->IsHidden() && JointActor->GetActorTransform().Equals(Original,0.001));
	TestTrue(TEXT("Preview, commit and cancellation retain one joint and correct stock"),T.Model->GetJointParts().Num()==1 && T.Model->GetRemainingQuantity(TEXT("Joint_Round"))==1 && T.Model->GetRemainingQuantity(TEXT("Bone"))==11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointEndpointBlockedTest, "JGD2026.AssemblyInteraction.SurfaceSnap.Endpoints.BlockedJointDropIsAtomic", SurfaceSnapFlags)
bool FAssemblyJointEndpointBlockedTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition=FVector2D(-1000,0);
	FAssemblyBoneDefinition Blocker = ShortSurfaceBone();
	Blocker.TypeId=TEXT("SmallBlocker");
	Blocker.SurfaceOutline={FVector2D(-2,-2),FVector2D(2,-2),FVector2D(2,2),FVector2D(-2,2)};
	if (!TestTrue(TEXT("Configure an actual endpoint obstruction"),T.Model->ConfigureFreeAssembly(Settings,{ShortSurfaceBone(),SurfaceRound(),Blocker},{},T.Reason))) { return false; }
	FGuid Bone,Obstacle,Original,Failed;
	if (!TestTrue(TEXT("Install target and blocker without overlap"),T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(200,0),0,Bone,T.Reason) && T.Model->TryInstallFreeBone(TEXT("SmallBlocker"),FVector2D(264,0),0,Obstacle,T.Reason))) { return false; }
	TestTrue(TEXT("Begin a blocked connector shelf drop"),T.Interaction->BeginBoneDrag(TEXT("Joint_Round"),FGuid(),T.Reason));
	TestFalse(TEXT("The endpoint candidate cannot overlap the blocker"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(274,0),T.Reason));
	TestFalse(TEXT("Blocked connector release fails"),T.Interaction->CommitBoneDrag(Failed,T.Reason));
	TestTrue(TEXT("Failed shelf drop neither creates an ID nor consumes inventory"),!Failed.IsValid() && T.Model->GetJointParts().IsEmpty() && T.Model->GetRemainingQuantity(TEXT("Joint_Round"))==2);
	if (!TestTrue(TEXT("Install an original connector far away"),T.Model->TryInstallJointPart(TEXT("Joint_Round"),FVector2D(700,0),0,Original,T.Reason))) { return false; }
	ANormalBone* OriginalActor = T.Interaction->GetBoneActor(Original);
	if (!TestNotNull(TEXT("The installed connector view exists"),OriginalActor)) { return false; }
	const FTransform OriginalPose=OriginalActor->GetActorTransform();
	TestTrue(TEXT("Begin blocked move of the installed connector"),T.Interaction->BeginBoneDrag(TEXT("Joint_Round"),Original,T.Reason));
	TestFalse(TEXT("Blocked move preview remains invalid"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(274,0),T.Reason));
	TestFalse(TEXT("Blocked installed release fails"),T.Interaction->CommitBoneDrag(Failed,T.Reason));
	TestTrue(TEXT("Failed installed drop restores exact actor, pose, instance and stock"),!Failed.IsValid() && T.Interaction->GetBoneActor(Original)==OriginalActor && !OriginalActor->IsHidden() && OriginalActor->GetActorTransform().Equals(OriginalPose,0.001) && T.Model->GetJointParts().Num()==1 && T.Model->GetRemainingQuantity(TEXT("Joint_Round"))==1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyEndpointPlayingHingeTest, "JGD2026.Assembly.SurfaceSnap.Endpoints.PlayingHingeDoesNotRequireStaticCollinearity", SurfaceSnapFlags)
bool FAssemblyEndpointPlayingHingeTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize runtime hinge fixture"),T.bReady)) { return false; }
	FGuid Root,Joint,Leaf;
	if (!TestTrue(TEXT("Install a core-bone-joint-bone endpoint chain"),T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(64,0),0,Root,T.Reason)
		&& T.Model->TryInstallJointPart(TEXT("Joint_Round"),FVector2D(128,0),0,Joint,T.Reason)
		&& T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(192,0),0,Leaf,T.Reason))) { return false; }
	TestTrue(TEXT("The static endpoint assembly validates before Playing"),T.Model->ValidateAssembly().bValid);
	if (!TestTrue(TEXT("Enter Playing with the connected endpoint assembly"),T.Model->TryStartPlaying(T.Reason))) { AddError(T.Reason.ToString()); return false; }
	USceneComponent* Source=T.Model->GetBonePoseSource(Leaf);
	if (!TestNotNull(TEXT("The leaf exposes its physical pose source"),Source)) { return false; }
	// Rotate about the existing rim constraint, preserving that world anchor while moving off the center line.
	Source->SetWorldTransform(AssemblyFreeRules::MakePlacement(FVector2D(160,-32),90).LocalVisualTransform);
	TestTrue(TEXT("Physical hinge motion may move the connector center off the leaf axis while Playing"),T.Model->ValidateAssembly().bValid);
	TestTrue(TEXT("Hinge pose updates do not alter stock, identifiers or the three constraints"),T.Model->GetRemainingQuantity(TEXT("Bone"))==10 && T.Model->GetRemainingQuantity(TEXT("Joint_Round"))==1 && T.Model->GetJoints().Num()==3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyEndpointPrepareRecheckTest, "JGD2026.Assembly.SurfaceSnap.Endpoints.PreparePoseChangeRevalidatesBeforePlaying", SurfaceSnapFlags)
bool FAssemblyEndpointPrepareRecheckTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	if (!TestTrue(TEXT("Initialize preparation endpoint fixture"),T.bReady)) { return false; }
	FGuid Bone,Joint;
	if (!TestTrue(TEXT("Install the valid editable endpoint structure"),T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(64,0),0,Bone,T.Reason)
		&& T.Model->TryInstallJointPart(TEXT("Joint_Round"),FVector2D(128,0),0,Joint,T.Reason))) { return false; }
	USceneComponent* Source=T.Model->GetBonePoseSource(Joint);
	if (!TestNotNull(TEXT("The connector has a registered world pose"),Source)) { return false; }
	const FTransform Before=Source->GetComponentTransform();
	int32 Prepared=0,Aborted=0;
	const FAssemblyLifecycleCheck Prepare=FAssemblyLifecycleCheck::CreateLambda([&](FText& OutReason)
	{
		++Prepared;
		Source->SetWorldLocation(Before.GetLocation()+FVector(0,0,0.5));
		OutReason=FText::GetEmpty();
		return true;
	});
	const FAssemblyLifecycleCheck Reset=FAssemblyLifecycleCheck::CreateLambda([](FText& OutReason) { OutReason=FText::GetEmpty(); return true; });
	const FSimpleDelegate Abort=FSimpleDelegate::CreateLambda([&]() { ++Aborted; Source->SetWorldTransform(Before); });
	if (!TestTrue(TEXT("Bind a provider that moves the actual connector during preparation"),T.Model->RegisterLifecycleCallbacks(T.Owner,Prepare,Reset,Abort,FSimpleDelegate(),T.Reason))) { return false; }
	TestFalse(TEXT("A previously valid structure cannot enter Playing with a newly off-axis connector center"),T.Model->TryStartPlaying(T.Reason));
	TestFalse(TEXT("Preparation endpoint rejection gives an explicit reason"),T.Reason.IsEmpty());
	TestTrue(TEXT("Preparation is aborted exactly once before publishing Playing"),Prepared==1 && Aborted==1 && T.Model->GetPhase()==EAssemblyPhase::Assembly);
	TestTrue(TEXT("Failed preparation preserves all instance and constraint identities and inventory"),T.Model->GetBones().Num()==2 && T.Model->GetJoints().Num()==2 && T.Model->GetRemainingQuantity(TEXT("Bone"))==11 && T.Model->GetRemainingQuantity(TEXT("Joint_Round"))==1);
	TestTrue(TEXT("The restored provider pose yields a valid editable structure"),Source->GetComponentTransform().Equals(Before,0.001) && T.Model->ValidateAssembly().bValid);
	T.Model->UnregisterLifecycleCallbacksIfMatches(T.Owner);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyConnectionPortGeometryTest, "JGD2026.Assembly.SurfaceSnap.Ports.GeometryAndInvalidOverrides", SurfaceSnapFlags)
bool FAssemblyConnectionPortGeometryTest::RunTest(const FString& Parameters)
{
	const int32 ExpectedCounts[] = { 4, 2, 2, 3, 4, 3, 4 };
	for (uint8 Kind = 0; Kind <= static_cast<uint8>(EAssemblyJointPartShape::FixedRound); ++Kind)
	{
		const FAssemblyBoneDefinition D = AssemblyRigidPartMetrics::MakeJointPartDefinition(static_cast<EAssemblyJointPartShape>(Kind), 0.1f);
		TArray<AssemblyRigidPartMetrics::FConnectionPort> Ports;
		FText Reason;
		if (!TestTrue(FString::Printf(TEXT("Shape %d exposes valid actual end ports: %s"), Kind, *Reason.ToString()),
			AssemblyRigidPartMetrics::GetConnectionPorts(D, Ports, Reason))) return false;
		TestEqual(TEXT("The public runtime query retains every authored end"), Ports.Num(), ExpectedCounts[Kind]);
		AssemblySurfaceGeometry::FShape Shape;
		if (!AssemblySurfaceGeometry::BuildShape(D, FTransform::Identity, Shape, Reason)) return false;
		for (const AssemblyRigidPartMetrics::FConnectionPort& Port : Ports)
		{
			TestTrue(TEXT("The public port position lies on its actual contour"), AssemblySurfaceGeometry::IsOnBoundary(Shape, Port.Point));
			TestTrue(TEXT("The attachment direction is a finite unit vector"), FMath::IsNearlyEqual(Port.OutwardAxis.SizeSquared(), 1.0, 0.0001));
			TestTrue(TEXT("Stepping behind the face is material"), AssemblySurfaceGeometry::ContainsPoint(Shape, Port.Point - Port.OutwardAxis * .01));
			TestFalse(TEXT("Stepping beyond the face is outside"), AssemblySurfaceGeometry::ContainsPoint(Shape, Port.Point + Port.OutwardAxis * .01));
		}
		FAssemblyBoneDefinition Reordered = D;
		for (int32 I = 0, J = Reordered.ConnectionPoints.Num() - 1; I < J; ++I, --J) Swap(Reordered.ConnectionPoints[I], Reordered.ConnectionPoints[J]);
		TArray<AssemblyRigidPartMetrics::FConnectionPort> ReversePorts;
		TestTrue(TEXT("Directions follow their actual face even when configured point order changes"), AssemblyRigidPartMetrics::GetConnectionPorts(Reordered, ReversePorts, Reason));
		for (const AssemblyRigidPartMetrics::FConnectionPort& Port : Ports)
		{
			TestTrue(TEXT("Reordering never changes a port's direction"), ReversePorts.ContainsByPredicate([&](const AssemblyRigidPartMetrics::FConnectionPort& Other)
			{
				return Other.Point.Equals(Port.Point, .0001) && Other.OutwardAxis.Equals(Port.OutwardAxis, .0001);
			}));
		}
	}
	FAssemblyBoneDefinition Invalid = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar, .1f);
	Invalid.ConnectionPoints = { FVector2D::ZeroVector };
	TArray<AssemblyRigidPartMetrics::FConnectionPort> Ports;
	FText Reason;
	TestFalse(TEXT("An interior marker cannot become a physical end port"), AssemblyRigidPartMetrics::GetConnectionPorts(Invalid, Ports, Reason));
	TestTrue(TEXT("A failed query returns no partial ports and a reason"), Ports.IsEmpty() && !Reason.IsEmpty());
	UAssemblyComponent* Model = MakeSurfaceModel();
	TestFalse(TEXT("Configuration rejects non-boundary custom ports before any drag"), Model->ConfigureFreeAssembly(FAssemblyFreeSettings(), { ShortSurfaceBone(), Invalid }, {}, Reason));
	TestEqual(TEXT("A rejected configuration retains the old stock"), Model->GetRemainingQuantity(TEXT("Bone")), 12);
	Invalid.ConnectionPoints = { FVector2D(-32, -16) };
	TestFalse(TEXT("A polygon corner with two different face normals is not an unambiguous port"), AssemblyRigidPartMetrics::GetConnectionPorts(Invalid, Ports, Reason));
	FAssemblyBoneDefinition CustomBone = ShortSurfaceBone();
	CustomBone.SurfaceOutline = { FVector2D(-24,0), FVector2D(-18,-3), FVector2D(15,-8), FVector2D(24,0), FVector2D(15,10), FVector2D(-18,3) };
	TestTrue(TEXT("The shared runtime query exposes custom bones' real axial ends"), AssemblyRigidPartMetrics::GetConnectionPorts(CustomBone, Ports, Reason));
	TestTrue(TEXT("Custom axial tips are not replaced by the nominal metrics box"), Ports.Num() == 2 && Ports[0].Point.Equals(FVector2D(-24,0),.001) && Ports[1].Point.Equals(FVector2D(24,0),.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyAllPortsBidirectionalSnapTest, "JGD2026.Assembly.SurfaceSnap.Ports.AllShapesBothDirectionsAndRotatedFrames", SurfaceSnapFlags)
bool FAssemblyAllPortsBidirectionalSnapTest::RunTest(const FString& Parameters)
{
	for (const EAssemblyJointPartShape Kind : { EAssemblyJointPartShape::Bar, EAssemblyJointPartShape::Corner,
		EAssemblyJointPartShape::Tee, EAssemblyJointPartShape::Cross, EAssemblyJointPartShape::Fork })
	{
		const FAssemblyBoneDefinition Joint = AssemblyRigidPartMetrics::MakeJointPartDefinition(Kind, .1f);
		TArray<AssemblyRigidPartMetrics::FConnectionPort> Ports;
		FText Reason;
		if (!AssemblyRigidPartMetrics::GetConnectionPorts(Joint, Ports, Reason)) { AddError(Reason.ToString()); return false; }
		for (const float GlobalAngle : { 0.0f, 37.25f })
		{
			for (int32 PortIndex = 0; PortIndex < Ports.Num(); ++PortIndex)
			{
				for (const double BoneEndSign : { -1.0, 1.0 })
				{
					const AssemblyRigidPartMetrics::FConnectionPort& Port = Ports[PortIndex];
					const FString Context = FString::Printf(TEXT("Shape %d, port %d, global %.2f, bone end %.0f"), static_cast<uint8>(Kind), PortIndex, GlobalAngle, BoneEndSign);
					FAssemblyFreeSettings Settings;
					Settings.CoreLocalPosition = FVector2D(-1000, 0);
					UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
					if (!Model->ConfigureFreeAssembly(Settings, { ShortSurfaceBone(), Joint }, {}, Reason)) { AddError(Reason.ToString()); return false; }
					const FTransform TargetPose = AssemblyFreeRules::MakePlacement(FVector2D(400,200), GlobalAngle).LocalVisualTransform;
					const FVector Point3 = TargetPose.TransformPosition(FVector(Port.Point.X,0,Port.Point.Y));
					const FVector Axis3 = TargetPose.TransformVectorNoScale(FVector(Port.OutwardAxis.X,0,Port.OutwardAxis.Y));
					const FVector2D Point(Point3.X,Point3.Z), Outward(Axis3.X,Axis3.Z);
					const FVector2D BoneAxis = -Outward * BoneEndSign;
					const float BoneAngle = FMath::RadiansToDegrees(FMath::Atan2(-BoneAxis.Y,BoneAxis.X));
					const FVector2D BonePosition = Point + Outward * 32.0;
					FGuid JointId;
					if (!TestTrue(Context + TEXT(" installs the fixed target"), Model->TryInstallJointPart(Joint.TypeId,FVector2D(400,200),GlobalAngle,JointId,Reason))) return false;
					const FAssemblyPlacementResult Preview = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),BonePosition + Outward * 5.0,BoneAngle + 23.0f,FGuid(),FAssemblySurfaceSnapState());
					if (!TestTrue(Context + TEXT(" auto-aligns a dragged bone to the selected end"), Preview.bValid && Preview.SurfaceSnap.bSnapped && Preview.SurfaceSnap.TargetId == JointId)) { AddError(Preview.Reason.ToString()); return false; }
					TestTrue(Context + TEXT(" preserves the selected bone end and connector port"), Preview.SurfaceSnap.SourceLocalAnchor.Equals(FVector2D(BoneEndSign*32,0),.001)
						&& Preview.SurfaceSnap.TargetLocalAnchor.Equals(Port.Point,.001));
					TestTrue(Context + TEXT(" puts the bone axis exactly on the port extension"), Preview.LocalPosition.Equals(BonePosition,.001)
						&& FMath::Abs(FMath::FindDeltaAngleDegrees(Preview.RotationDegrees,BoneAngle)) < .001);
					const FAssemblyPlacementResult HeldBone = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),BonePosition+Outward*21.0,BoneAngle+23.0f,FGuid(),Preview.SurfaceSnap);
					TestTrue(Context + TEXT(" holds that exact port while dragging the bone"),HeldBone.bValid && HeldBone.SurfaceSnap.bSnapped
						&& HeldBone.LocalVisualTransform.Equals(Preview.LocalVisualTransform,.001)
						&& HeldBone.SurfaceSnap.TargetLocalAnchor.Equals(Port.Point,.001));
					const FAssemblyPlacementResult FreeBone = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),BonePosition+Outward*33.0,BoneAngle+23.0f,FGuid(),HeldBone.SurfaceSnap);
					TestTrue(Context + TEXT(" releases the dragged bone beyond the larger threshold"),FreeBone.bValid && !FreeBone.SurfaceSnap.bSnapped
						&& FreeBone.LocalPosition.Equals(BonePosition+Outward*33.0,.001));
					FGuid BoneId;
					TestTrue(Context + TEXT(" commits the exact shown pose"), Model->TryInstallFreeBone(TEXT("Bone"),Preview.LocalPosition,Preview.RotationDegrees,BoneId,Reason));
					TestTrue(Context + TEXT(" creates exactly the permitted pair"), Model->GetJoints().Num() == 1 && IsSurfacePair(Model->GetJoints()[0],JointId,BoneId));
					TestTrue(Context + TEXT(" consumes only one of each stock"), Model->GetRemainingQuantity(TEXT("Bone")) == 11 && Model->GetRemainingQuantity(Joint.TypeId) == 1);

					// Reverse the drag: each actual joint end aligns to either signed bone end too.
					Model = NewObject<UAssemblyComponent>();
					if (!Model->ConfigureFreeAssembly(Settings,{ShortSurfaceBone(),Joint},{},Reason)) { AddError(Reason.ToString()); return false; }
					const FTransform FixedBone = AssemblyFreeRules::MakePlacement(FVector2D(400,200),GlobalAngle).LocalVisualTransform;
					const FVector FixedEnd3 = FixedBone.TransformPosition(FVector(BoneEndSign*32,0,0));
					const FVector FixedAxis3 = FixedBone.TransformVectorNoScale(FVector(BoneEndSign,0,0));
					const FVector2D FixedEnd(FixedEnd3.X,FixedEnd3.Z), FixedOutward(FixedAxis3.X,FixedAxis3.Z);
					const float JointAngle = FMath::RadiansToDegrees(FMath::Atan2(FixedOutward.Y,-FixedOutward.X))
						- FMath::RadiansToDegrees(FMath::Atan2(-Port.OutwardAxis.Y,Port.OutwardAxis.X));
					const FTransform AtOrigin = AssemblyFreeRules::MakePlacement(FVector2D::ZeroVector,JointAngle).LocalVisualTransform;
					const FVector Offset3 = AtOrigin.TransformPosition(FVector(Port.Point.X,0,Port.Point.Y));
					const FVector2D JointPosition = FixedEnd - FVector2D(Offset3.X,Offset3.Z);
					if (!TestTrue(Context + TEXT(" installs the fixed bone"),Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(400,200),GlobalAngle,BoneId,Reason))) return false;
					const FAssemblyPlacementResult Reverse = Model->ResolveFreeBoneDragPlacement(Joint.TypeId,JointPosition + FixedOutward*5.0,JointAngle+23.0f,FGuid(),FAssemblySurfaceSnapState());
					if (!TestTrue(Context + TEXT(" auto-aligns a dragged joint into the bone extension"),Reverse.bValid && Reverse.SurfaceSnap.bSnapped && Reverse.SurfaceSnap.TargetId == BoneId)) { AddError(Reverse.Reason.ToString()); return false; }
					TestTrue(Context + TEXT(" reverse snapping preserves the selected port and bone end"),Reverse.SurfaceSnap.SourceLocalAnchor.Equals(Port.Point,.001)
						&& Reverse.SurfaceSnap.TargetLocalAnchor.Equals(FVector2D(BoneEndSign*32,0),.001));
					TestTrue(Context + TEXT(" reverse pose has matching axes and actual anchors"),Reverse.LocalPosition.Equals(JointPosition,.001)
						&& FMath::Abs(FMath::FindDeltaAngleDegrees(Reverse.RotationDegrees,JointAngle)) < .001);
					const FAssemblyPlacementResult HeldJoint = Model->ResolveFreeBoneDragPlacement(Joint.TypeId,JointPosition+FixedOutward*21.0,JointAngle+23.0f,FGuid(),Reverse.SurfaceSnap);
					TestTrue(Context + TEXT(" holds that exact joint port and signed bone end"),HeldJoint.bValid && HeldJoint.SurfaceSnap.bSnapped
						&& HeldJoint.LocalVisualTransform.Equals(Reverse.LocalVisualTransform,.001)
						&& HeldJoint.SurfaceSnap.SourceLocalAnchor.Equals(Port.Point,.001));
					const FAssemblyPlacementResult FreeJoint = Model->ResolveFreeBoneDragPlacement(Joint.TypeId,JointPosition+FixedOutward*33.0,JointAngle+23.0f,FGuid(),HeldJoint.SurfaceSnap);
					TestTrue(Context + TEXT(" releases the dragged joint beyond the larger threshold"),FreeJoint.bValid && !FreeJoint.SurfaceSnap.bSnapped
						&& FreeJoint.LocalPosition.Equals(JointPosition+FixedOutward*33.0,.001));
					TestTrue(Context + TEXT(" reverse preview commits without changing its pose"),Model->TryInstallJointPart(Joint.TypeId,Reverse.LocalPosition,Reverse.RotationDegrees,JointId,Reason));
					TestTrue(Context + TEXT(" reverse commit has one actual constraint"),Model->GetJoints().Num()==1 && IsSurfacePair(Model->GetJoints()[0],JointId,BoneId));
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyPortLatchAndSidePolicyTest, "JGD2026.Assembly.SurfaceSnap.Ports.SideRejectionAndSamePortHysteresis", SurfaceSnapFlags)
bool FAssemblyPortLatchAndSidePolicyTest::RunTest(const FString& Parameters)
{
	const FAssemblyBoneDefinition Bar = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar,.1f);
	UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition = FVector2D(-1000,0);
	FText Reason;
	TestTrue(TEXT("Configure endpoint-only bar case"),Model->ConfigureFreeAssembly(Settings,{ShortSurfaceBone(),Bar},{},Reason));
	FGuid BarId, BoneId;
	TestTrue(TEXT("Stage the horizontal bar"),Model->TryInstallJointPart(Bar.TypeId,FVector2D(200,0),0,BarId,Reason));
	const FAssemblyPlacementResult Side = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(200,53),-90,FGuid(),FAssemblySurfaceSnapState());
	TestTrue(TEXT("A bone near the long side remains free instead of rotating into a fake central port"),Side.bValid && !Side.SurfaceSnap.bSnapped && Side.RotationDegrees == -90);
	TestTrue(TEXT("An exact side tangent is legal free staging"),Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(200,48),-90,BoneId,Reason));
	TestTrue(TEXT("Touching the long side creates no constraint edge"),Model->GetJoints().IsEmpty());
	TestTrue(TEXT("Remove the staged bone"),Model->TryRemoveInstance(BoneId,Reason));
	const FAssemblyPlacementResult First = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(269,0),23,FGuid(),FAssemblySurfaceSnapState());
	if (!TestTrue(TEXT("The real right port rotates a bone to a collinear pose"),First.bValid && First.SurfaceSnap.bSnapped && First.LocalPosition.Equals(FVector2D(264,0),.001))) return false;
	const FAssemblyPlacementResult Held = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(285,0),23,FGuid(),First.SurfaceSnap);
	TestTrue(TEXT("The same port and signed bone end remain latched within release distance"),Held.bValid && Held.SurfaceSnap.bSnapped
		&& Held.SurfaceSnap.TargetLocalAnchor.Equals(First.SurfaceSnap.TargetLocalAnchor,.001)
		&& Held.SurfaceSnap.SourceLocalAnchor.Equals(First.SurfaceSnap.SourceLocalAnchor,.001) && Held.LocalVisualTransform.Equals(First.LocalVisualTransform,.001));
	const FAssemblyPlacementResult Released = Model->ResolveFreeBoneDragPlacement(TEXT("Bone"),FVector2D(297,0),23,FGuid(),Held.SurfaceSnap);
	TestTrue(TEXT("Pulling beyond release distance restores free pointer and requested angle"),Released.bValid && !Released.SurfaceSnap.bSnapped
		&& Released.LocalPosition.Equals(FVector2D(297,0),.001) && Released.RotationDegrees == 23);
	TestTrue(TEXT("Previews never consume stock"),Model->GetRemainingQuantity(TEXT("Bone")) == 12 && Model->GetRemainingQuantity(Bar.TypeId) == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyPortBlockedTransactionTest, "JGD2026.AssemblyInteraction.SurfaceSnap.Ports.BlockedAlignedDropRestoresInstancesAndStock", SurfaceSnapFlags)
bool FAssemblyPortBlockedTransactionTest::RunTest(const FString& Parameters)
{
	FSurfaceDragWorld T;
	FAssemblyFreeSettings Settings;
	Settings.CoreLocalPosition=FVector2D(-1000,0);
	FAssemblyBoneDefinition Corner=AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Corner,.1f);
	FAssemblyBoneDefinition Blocker=ShortSurfaceBone();
	Blocker.TypeId=TEXT("PortBlocker");
	Blocker.SurfaceOutline={FVector2D(-2,-2),FVector2D(2,-2),FVector2D(2,2),FVector2D(-2,2)};
	if (!TestTrue(TEXT("Configure a real aligned port obstruction"),T.Model->ConfigureFreeAssembly(Settings,{ShortSurfaceBone(),Corner,Blocker},{},T.Reason))) return false;
	FGuid Bone,Obstacle,Original,Failed;
	if (!TestTrue(TEXT("Stage the bone and the small blocker"),T.Model->TryInstallFreeBone(TEXT("Bone"),FVector2D(200,0),0,Bone,T.Reason)
		&& T.Model->TryInstallFreeBone(Blocker.TypeId,FVector2D(280,-16),0,Obstacle,T.Reason))) return false;
	if (!TestTrue(TEXT("Begin a corner shelf preview"),T.Interaction->BeginBoneDrag(Corner.TypeId,FGuid(),T.Reason))) return false;
	// Shelf previews deliberately remain invalid until a fresh pointer is supplied. Establish a
	// clear legal pose before testing rotation, whose return value describes preview validity.
	if (!TestTrue(TEXT("Supply a fresh pointer at an unobstructed position"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(600,200),T.Reason))) return false;
	if (!TestTrue(TEXT("Rotate away from the eventual automatic alignment"),T.Interaction->RotateBoneDrag(17,T.Reason))) return false;
	TestFalse(TEXT("Auto-aligned corner cannot cut through another body"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(269,-16),T.Reason));
	TestFalse(TEXT("A blocked new drop fails atomically"),T.Interaction->CommitBoneDrag(Failed,T.Reason));
	TestTrue(TEXT("The blocked shelf drop creates no ID or stock change"),!Failed.IsValid() && T.Model->GetJointParts().IsEmpty() && T.Model->GetRemainingQuantity(Corner.TypeId)==2);
	if (!TestTrue(TEXT("Install an original corner away from the obstruction"),T.Model->TryInstallJointPart(Corner.TypeId,FVector2D(700,0),0,Original,T.Reason))) return false;
	ANormalBone* OriginalActor=T.Interaction->GetBoneActor(Original);
	if (!TestNotNull(TEXT("The original independent corner view exists"),OriginalActor)) return false;
	const FTransform Before=OriginalActor->GetActorTransform();
	TestTrue(TEXT("Begin moving the installed corner"),T.Interaction->BeginBoneDrag(Corner.TypeId,Original,T.Reason));
	TestFalse(TEXT("The installed move sees the same blocked port"),T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(269,-16),T.Reason));
	TestFalse(TEXT("The failed installed drop rolls back"),T.Interaction->CommitBoneDrag(Failed,T.Reason));
	TestTrue(TEXT("Actor identity, original pose, visibility and inventory are restored"),!Failed.IsValid() && T.Interaction->GetBoneActor(Original)==OriginalActor
		&& !OriginalActor->IsHidden() && OriginalActor->GetActorTransform().Equals(Before,.001) && T.Model->GetJointParts().Num()==1 && T.Model->GetRemainingQuantity(Corner.TypeId)==1);
	return true;
}

#endif
