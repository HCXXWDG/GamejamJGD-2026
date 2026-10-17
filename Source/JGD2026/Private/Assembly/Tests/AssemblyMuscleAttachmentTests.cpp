#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
	constexpr EAutomationTestFlags AttachmentFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UAssemblyComponent* MakeAttachmentModel(bool bAllowOverlap = false, bool bNotchedContour = false)
	{
		FAssemblyBoneDefinition Bone;
		Bone.BoneParameters.Length = EBoneLengthTier::Short;
		Bone.InitialQuantity = 12;
		Bone.SurfaceOutline = bNotchedContour
			? TArray<FVector2D>{ {-32,-6}, {32,-6}, {32,6}, {8,6}, {8,2}, {-8,2}, {-8,6}, {-32,6} }
			: TArray<FVector2D>{ {-32,-6}, {32,-6}, {32,6}, {-32,6} };
		FAssemblyBoneDefinition Joint = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Round, 0.1f);
		FAssemblyMuscleDefinition Muscle;
		Muscle.InitialQuantity = 6;
		FAssemblyFreeSettings Settings;
		Settings.CoreLocalPosition = FVector2D(-1000,-1000);
		Settings.bEnableSurfaceSnapping = false;
		Settings.bAllowBoneOverlap = bAllowOverlap;
		Settings.bRejectDisconnectingRemoval = false;
		UAssemblyComponent* Model = NewObject<UAssemblyComponent>();
		FText Reason;
		return Model->ConfigureFreeAssembly(Settings, { Bone, Joint }, { Muscle }, Reason) ? Model : nullptr;
	}

	FVector AttachmentWorldPoint(const UAssemblyComponent* Model, FGuid BoneId, FVector2D Point)
	{
		FTransform Transform;
		return Model->GetBoneWorldTransform(BoneId, Transform)
			? Transform.TransformPosition(FVector(Point.X, 0.0, Point.Y)) : FVector::ZeroVector;
	}

	bool SameAttachment(const FAssemblyMuscleEndpoint& Endpoint, FGuid BoneId, FVector2D Point)
	{
		return Endpoint.BoneId == BoneId && Endpoint.BoneLocalPoint.Equals(Point, 0.000001);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyMuscleInnerOuterAttachmentTest,
	"JGD2026.Assembly.MuscleAttachments.InnerOuterAnchorsAndJointOcclusion", AttachmentFlags)
bool FAssemblyMuscleInnerOuterAttachmentTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeAttachmentModel(true);
	if (!TestNotNull(TEXT("Create a model that permits explicit occlusion geometry"), Model)) { return false; }
	FText Reason;
	FGuid BoneA, BoneB, Joint;
	if (!TestTrue(TEXT("Install first bone"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(0,0), 0.0f, BoneA, Reason))
		|| !TestTrue(TEXT("Install angled second bone"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(160,0), -60.0f, BoneB, Reason))) { return false; }
	const FName JointType = Model->GetBoneDefinitions().FindByPredicate([](const FAssemblyBoneDefinition& D)
		{ return D.PartKind == EAssemblyRigidPartKind::JointPart; })->TypeId;
	if (!TestTrue(TEXT("A newer joint overlaps the first bone's selectable area"),
		Model->TryInstallJointPart(JointType, FVector2D(0,0), 0.0f, Joint, Reason))) { return false; }

	FAssemblyMuscleEndpoint InnerA, InnerB, OuterA, OuterB, Generic;
	const FVector2D InnerPointA(10.25,5.5), InnerPointB(-11.25,5.5);
	const FVector2D OuterPointA(10.25,-5.5), OuterPointB(-11.25,-5.5);
	const FVector HitA = AttachmentWorldPoint(Model, BoneA, InnerPointA);
	TestTrue(TEXT("Generic rigid picking still selects the newer visible joint"),
		Model->FindBoneAtWorldPosition(HitA, Generic) && Generic.BoneId == Joint);
	TestTrue(TEXT("Muscle picking reaches the bone underneath the joint"),
		Model->FindMuscleEndpointAtWorldPosition(HitA, InnerA) && SameAttachment(InnerA, BoneA, InnerPointA));
	TestTrue(TEXT("The second inner anchor retains its actual signed cross-bone coordinate"),
		Model->FindMuscleEndpointAtWorldPosition(AttachmentWorldPoint(Model, BoneB, InnerPointB), InnerB)
		&& SameAttachment(InnerB, BoneB, InnerPointB));
	TestTrue(TEXT("The first outer anchor keeps the other side rather than the centerline"),
		Model->FindMuscleEndpointAtWorldPosition(AttachmentWorldPoint(Model, BoneA, OuterPointA), OuterA)
		&& SameAttachment(OuterA, BoneA, OuterPointA));
	TestTrue(TEXT("The second outer anchor remains a distinct point on the same second bone"),
		Model->FindMuscleEndpointAtWorldPosition(AttachmentWorldPoint(Model, BoneB, OuterPointB), OuterB)
		&& SameAttachment(OuterB, BoneB, OuterPointB));
	FGuid InnerId, OuterId;
	TestTrue(TEXT("Bind the inner muscle to W"), Model->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestTrue(TEXT("Install the inner muscle"), Model->TryInstallMuscle(TEXT("Muscle"), InnerA, InnerB, InnerId, Reason));
	TestTrue(TEXT("Bind the outer muscle to S"), Model->SelectMuscleKey(EAssemblyMuscleKey::S, Reason));
	TestTrue(TEXT("The same bone pair also accepts an outer muscle"), Model->TryInstallMuscle(TEXT("Muscle"), OuterA, OuterB, OuterId, Reason));
	TestTrue(TEXT("Both muscles have independent identities"), InnerId.IsValid() && OuterId.IsValid() && InnerId != OuterId);
	TestEqual(TEXT("Two actual muscles consume two items"), Model->GetRemainingQuantity(TEXT("Muscle")), 4);
	FAssemblyMuscleInstance Inner, Outer;
	TestTrue(TEXT("The inner instance retains both exact endpoints and its key"), Model->GetMuscle(InnerId, Inner)
		&& SameAttachment(Inner.EndpointA, BoneA, InnerPointA) && SameAttachment(Inner.EndpointB, BoneB, InnerPointB)
		&& Inner.BindingKey == EAssemblyMuscleKey::W);
	TestTrue(TEXT("The outer instance retains both exact endpoints and its different key"), Model->GetMuscle(OuterId, Outer)
		&& SameAttachment(Outer.EndpointA, BoneA, OuterPointA) && SameAttachment(Outer.EndpointB, BoneB, OuterPointB)
		&& Outer.BindingKey == EAssemblyMuscleKey::S);
	FVector InnerWorldA, InnerWorldB, OuterWorldA, OuterWorldB;
	Model->GetEndpointWorldPosition(Inner.EndpointA, InnerWorldA);
	Model->GetEndpointWorldPosition(Inner.EndpointB, InnerWorldB);
	Model->GetEndpointWorldPosition(Outer.EndpointA, OuterWorldA);
	Model->GetEndpointWorldPosition(Outer.EndpointB, OuterWorldB);
	TestFalse(TEXT("The two attachment choices define different force directions"),
		(InnerWorldB - InnerWorldA).GetSafeNormal().Equals((OuterWorldB - OuterWorldA).GetSafeNormal(), 0.000001));

	FAssemblyMuscleEndpoint Invalid = InnerA;
	Invalid.BoneLocalPoint.Y = 50.0;
	TestFalse(TEXT("An invalid move cannot lose either original anchor"), Model->TryMoveMuscle(InnerId, Invalid, InnerB, Reason));
	TestTrue(TEXT("The failed move leaves the inner anchor unchanged"), Model->GetMuscle(InnerId, Inner)
		&& SameAttachment(Inner.EndpointA, BoneA, InnerPointA));
	TestTrue(TEXT("Removing one muscle is independent of the other"), Model->TryRemoveInstance(InnerId, Reason));
	TestEqual(TEXT("Removing the inner muscle returns exactly one item"), Model->GetRemainingQuantity(TEXT("Muscle")), 5);
	TestTrue(TEXT("The remaining outer muscle keeps its identity and actual anchors"), Model->GetMuscle(OuterId, Outer)
		&& SameAttachment(Outer.EndpointA, BoneA, OuterPointA) && SameAttachment(Outer.EndpointB, BoneB, OuterPointB));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyMuscleAttachmentPriorityTest,
	"JGD2026.Assembly.MuscleAttachments.WorldMarginPrioritiesAndInvalidInput", AttachmentFlags)
bool FAssemblyMuscleAttachmentPriorityTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeAttachmentModel();
	if (!TestNotNull(TEXT("Create attachment priority model"), Model)) { return false; }
	FText Reason;
	FGuid Older, Newer, SharedEdge;
	if (!TestTrue(TEXT("Install older lower bone"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(0,0), 0, Older, Reason))
		|| !TestTrue(TEXT("Install newer upper bone"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(0,20), 0, Newer, Reason))
		|| !TestTrue(TEXT("Install another bone touching the older endpoint"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D(64,0), 0, SharedEdge, Reason))) { return false; }
	const int32 BoneStock = Model->GetRemainingQuantity(TEXT("Bone"));
	FAssemblyMuscleEndpoint Pick;
	TestTrue(TEXT("An exact older hit wins over the newer bone's eight-unit margin"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(5,0,6), Pick, 8) && SameAttachment(Pick, Older, FVector2D(5,6)));
	TestTrue(TEXT("Nearby selection chooses the nearest actual contour rather than newest bone"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(5,0,9), Pick, 8) && SameAttachment(Pick, Older, FVector2D(5,6)));
	TestTrue(TEXT("The other contour becomes nearest on its side of the gap"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(5,0,11), Pick, 8) && SameAttachment(Pick, Newer, FVector2D(5,-6)));
	TestTrue(TEXT("Equidistant nearby contours consistently prefer the newest bone"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(5,0,10), Pick, 8) && SameAttachment(Pick, Newer, FVector2D(5,-6)));
	TestTrue(TEXT("An exact shared endpoint consistently prefers the newest bone"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(32,0,3), Pick, 8) && SameAttachment(Pick, SharedEdge, FVector2D(-32,3)));
	TestFalse(TEXT("The default zero margin never moves an off-body point"), Model->FindMuscleEndpointAtWorldPosition(FVector(-25,0,-7), Pick));
	TestTrue(TEXT("A margin-boundary hit projects to the real bone edge"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(-25,0,-14), Pick, 8) && SameAttachment(Pick, Older, FVector2D(-25,-6)));
	TestFalse(TEXT("An off-body hit beyond the configured margin is rejected"), Model->FindMuscleEndpointAtWorldPosition(FVector(-25,0,-14.01), Pick, 8));
	TestFalse(TEXT("A remote pointer cannot select a convenient distant bone"), Model->FindMuscleEndpointAtWorldPosition(FVector(500,0,500), Pick, 8));

	const float InvalidLimits[] = { -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
	for (const float Limit : InvalidLimits)
	{
		Pick.BoneId = Older;
		Pick.BoneLocalPoint = FVector2D(9,4);
		TestFalse(TEXT("A negative or nonfinite margin is rejected even for an interior point"),
			Model->FindMuscleEndpointAtWorldPosition(FVector(0,0,0), Pick, Limit));
		TestTrue(TEXT("Failure clears stale picked identity and coordinates"), !Pick.BoneId.IsValid() && Pick.BoneLocalPoint.IsZero());
	}
	TestFalse(TEXT("Nonfinite world coordinates are rejected"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(std::numeric_limits<double>::quiet_NaN(),0,0), Pick, 8));
	TestFalse(TEXT("An uninitialized model cannot supply an attachment"),
		NewObject<UAssemblyComponent>()->FindMuscleEndpointAtWorldPosition(FVector::ZeroVector, Pick, 8));
	TestEqual(TEXT("All hit tests leave bone inventory unchanged"), Model->GetRemainingQuantity(TEXT("Bone")), BoneStock);
	TestEqual(TEXT("All hit tests leave muscle inventory unchanged"), Model->GetRemainingQuantity(TEXT("Muscle")), 6);
	TestEqual(TEXT("Hit tests never install a muscle"), Model->GetMuscles().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyMuscleAttachmentScaledContourTest,
	"JGD2026.Assembly.MuscleAttachments.PhysicalContourAndScaledPose", AttachmentFlags)
bool FAssemblyMuscleAttachmentScaledContourTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Model = MakeAttachmentModel(false, true);
	if (!TestNotNull(TEXT("Create a model with a concave physical bone contour"), Model)) { return false; }
	FText Reason;
	FGuid Bone;
	if (!TestTrue(TEXT("Install the contoured bone"), Model->TryInstallFreeBone(TEXT("Bone"), FVector2D::ZeroVector, 0, Bone, Reason))) { return false; }
	FAssemblyMuscleEndpoint Pick;
	TestFalse(TEXT("The empty notch inside nominal bounds is not an exact bone hit"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(0,0,4), Pick));
	TestTrue(TEXT("A nearby notch hit lands on the actual concave contour"),
		Model->FindMuscleEndpointAtWorldPosition(FVector(0,0,4), Pick, 2) && SameAttachment(Pick, Bone, FVector2D(0,2)));

	USceneComponent* PhysicalPose = NewObject<USceneComponent>();
	const FTransform Pose(FQuat(FVector::YAxisVector, 0.63), FVector(400,13,-200), FVector(1.75,1.0,0.5));
	PhysicalPose->SetWorldTransform(Pose);
	if (!TestTrue(TEXT("Register the actual rotated and nonuniformly scaled body pose"), Model->RegisterBonePoseSource(Bone, PhysicalPose, Reason))) { return false; }
	const FVector2D Inside(12.5,-4.25);
	TestTrue(TEXT("Exact transformed hits preserve both full local coordinates"),
		Model->FindMuscleEndpointAtWorldPosition(Pose.TransformPosition(FVector(Inside.X,0,Inside.Y)), Pick, 8)
		&& SameAttachment(Pick, Bone, Inside));
	const FVector Outside = Pose.TransformPosition(FVector(0,0,8));
	TestFalse(TEXT("The surface margin is measured in world units rather than local units"),
		Model->FindMuscleEndpointAtWorldPosition(Outside, Pick, 2.9f));
	TestTrue(TEXT("The rotated half-scale contour is three world units from the pointer"),
		Model->FindMuscleEndpointAtWorldPosition(Outside, Pick, 3.001f) && SameAttachment(Pick, Bone, FVector2D(0,2)));
	FVector ResolvedWorld;
	TestTrue(TEXT("The projected endpoint resolves back onto the actual registered body surface"),
		Model->GetEndpointWorldPosition(Pick, ResolvedWorld) && ResolvedWorld.Equals(Pose.TransformPosition(FVector(0,0,2)), 0.000001));
	return true;
}

#endif
