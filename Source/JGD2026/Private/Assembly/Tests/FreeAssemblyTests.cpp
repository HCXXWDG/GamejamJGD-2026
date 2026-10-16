#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr EAutomationTestFlags FreeAssemblyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UAssemblyComponent* MakeFreeRulesAssembly()
	{
		UAssemblyComponent* Assembly = NewObject<UAssemblyComponent>();
		// Use a known 64 by 12.8 body so the geometry cases do not depend on art.
		Assembly->DefaultBones[0].BoneParameters.Length = EBoneLengthTier::Short;
		Assembly->DefaultBones[0].BoneParameters.Thickness = EBoneThicknessTier::Medium;
		Assembly->DefaultBones[0].BoneParameters.UnitLength = 64.0f;
		Assembly->DefaultBones[0].InitialQuantity = 12;
		Assembly->DefaultMuscles[0].InitialQuantity = 6;
	Assembly->DefaultJointParts = { AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar, 64.0f / 640.0f) };
		Assembly->DefaultFreeSettings.CoreRadius = 32.0f;
		Assembly->DefaultFreeSettings.ConnectionDistance = 8.0f;
		FText Reason;
		Assembly->EnterAssembly(Reason);
		return Assembly;
	}

	FAssemblyMuscleEndpoint FreeRulesEndpoint(FGuid BoneId, double X, double Z = 0.0)
	{
		FAssemblyMuscleEndpoint Endpoint;
		Endpoint.BoneId = BoneId;
		Endpoint.BoneLocalPoint = FVector2D(X, Z);
		return Endpoint;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFreeAssemblyContinuousPoseTest, "JGD2026.Assembly.Free.ContinuousPoseAndWorldRay", FreeAssemblyTestFlags)
bool FFreeAssemblyContinuousPoseTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeFreeRulesAssembly();
	TestTrue(TEXT("New component defaults to Free placement"), Assembly->GetPlacementMode() == EAssemblyPlacementMode::Free);
	TestTrue(TEXT("Free configuration initialized"), Assembly->IsInitialized());
	const FVector2D Position(-1095.25, 730.75);
	const float Angle = 37.25f;
	const FAssemblyPlacementResult Preview = Assembly->ValidateFreeBonePlacement(TEXT("Bone"), Position, Angle, FGuid());
	if (!TestTrue(TEXT("Fractional position outside the old 6 by 6 area is valid"), Preview.bValid)) { return false; }
	TestTrue(TEXT("Preview keeps continuous local position"), Preview.LocalPosition.Equals(Position, 0.000001));
	TestEqual(TEXT("Preview keeps arbitrary angle"), Preview.RotationDegrees, Angle);
	TestTrue(TEXT("Free placement has no occupied grid cells"), Preview.OccupiedCells.IsEmpty());
	FText Reason;
	FGuid BoneId;
	if (!TestTrue(TEXT("Commit uses the same valid free placement"), Assembly->TryInstallFreeBone(TEXT("Bone"), Position, Angle, BoneId, Reason))) { return false; }
	FAssemblyBoneInstance Bone;
	TestTrue(TEXT("Committed instance can be read"), Assembly->GetBone(BoneId, Bone));
	TestTrue(TEXT("Committed visual transform matches preview"), Bone.LocalVisualTransform.Equals(Preview.LocalVisualTransform));
	TestTrue(TEXT("Committed position remains fractional"), Bone.LocalPosition.Equals(Position, 0.000001));
	TestEqual(TEXT("Installation deducts one item"), Assembly->GetRemainingQuantity(TEXT("Bone")), 11);
	TestFalse(TEXT("An unconnected bone cannot start play"), Assembly->TryStartPlaying(Reason));
	FGuid GridId;
	TestFalse(TEXT("Legacy grid install is rejected in Free mode"), Assembly->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, GridId, Reason));
	TestFalse(TEXT("Rejected grid install has no instance identifier"), GridId.IsValid());
	TestEqual(TEXT("Rejected grid install preserves stock"), Assembly->GetRemainingQuantity(TEXT("Bone")), 11);

	USceneComponent* Frame = NewObject<USceneComponent>();
	const FTransform FrameTransform(FQuat(FVector::YAxisVector, 0.41), FVector(100, 22, 300), FVector(1.5, 1.0, 0.75));
	Frame->SetWorldTransform(FrameTransform);
	Assembly->SetGridFrame(Frame);
	const FVector WorldHit = FrameTransform.TransformPosition(FVector(Position.X, 0.0, Position.Y));
	const FVector Normal = FrameTransform.TransformVectorNoScale(FVector::YAxisVector);
	FVector2D HitPosition;
	TestTrue(TEXT("World ray intersects the transformed XZ assembly plane"), Assembly->RayToAssemblyPlane(WorldHit + Normal * 500.0, -Normal, HitPosition));
	TestTrue(TEXT("World ray returns the same continuous local position"), HitPosition.Equals(Position, 0.000001));
	TestFalse(TEXT("Parallel ray is rejected"), Assembly->RayToAssemblyPlane(WorldHit + Normal * 500.0, FrameTransform.TransformVectorNoScale(FVector::XAxisVector), HitPosition));
	TestFalse(TEXT("Intersection behind the ray origin is rejected"), Assembly->RayToAssemblyPlane(WorldHit + Normal * 500.0, Normal, HitPosition));

	const FAssemblyMuscleEndpoint Endpoint = FreeRulesEndpoint(BoneId, 10.25, -3.5);
	FVector EndpointWorld;
	TestTrue(TEXT("Endpoint follows arbitrary bone rotation and frame transform"), Assembly->GetEndpointWorldPosition(Endpoint, EndpointWorld));
	const FVector Expected = (Bone.LocalVisualTransform * FrameTransform).TransformPosition(FVector(10.25, 0.0, -3.5));
	TestTrue(TEXT("World endpoint agrees with actual visual transform"), EndpointWorld.Equals(Expected, 0.000001));
	FAssemblyMuscleEndpoint Picked;
	TestTrue(TEXT("Continuous world hit selects the bone"), Assembly->FindBoneAtWorldPosition(EndpointWorld, Picked));
	TestTrue(TEXT("World hit recovers the same local attachment point"), Picked.BoneId == BoneId && Picked.BoneLocalPoint.Equals(Endpoint.BoneLocalPoint, 0.000001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFreeAssemblyGeometryTransactionTest, "JGD2026.Assembly.Free.OrientedGeometryTransactions", FreeAssemblyTestFlags)
bool FFreeAssemblyGeometryTransactionTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeFreeRulesAssembly();
	FText Reason;
	FGuid First, Second, Tangent, Invalid;
	if (!TestTrue(TEXT("Install an angled body away from the core"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(240, 0), 45.0f, First, Reason))) { return false; }
	const int32 OriginalStock = Assembly->GetRemainingQuantity(TEXT("Bone"));
	TestFalse(TEXT("Two coincident rotated bodies overlap"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(240, 0), -37.0f, Invalid, Reason));
	TestFalse(TEXT("Overlap failure returns no ID"), Invalid.IsValid());
	TestFalse(TEXT("Overlap failure explains the rejection"), Reason.IsEmpty());
	TestFalse(TEXT("Rotated body through spherical core is rejected"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D::ZeroVector, 23.0f, Invalid, Reason));
	TestEqual(TEXT("Invalid drops consume no inventory"), Assembly->GetRemainingQuantity(TEXT("Bone")), OriginalStock);
	FAssemblyBoneInstance Before, After;
	Assembly->GetBone(First, Before);
	TestFalse(TEXT("Moving a body through the core is rejected"), Assembly->TryMoveFreeBone(First, FVector2D::ZeroVector, 16.0f, Reason));
	TestTrue(TEXT("Failed move preserves the original instance"), Assembly->GetBone(First, After));
	TestTrue(TEXT("Failed move preserves pose and angle"), Before.LocalVisualTransform.Equals(After.LocalVisualTransform) && Before.RotationDegrees == After.RotationDegrees);
	TestEqual(TEXT("Failed move preserves stock"), Assembly->GetRemainingQuantity(TEXT("Bone")), OriginalStock);

	// Their axis-aligned bounds overlap, but their narrow oriented bodies do not.
	const double Offset = 20.0 / FMath::Sqrt(2.0);
	TestTrue(TEXT("SAT permits separated parallel bodies with intersecting AABBs"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(240 + Offset, Offset), 45.0f, Second, Reason));
	TestTrue(TEXT("Core tangent endpoint is a legal non-overlapping connection"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(64, 0), 0.0f, Tangent, Reason));
	TestEqual(TEXT("Only the three valid installations consume stock"), Assembly->GetRemainingQuantity(TEXT("Bone")), 9);
	TestEqual(TEXT("Only valid instances exist"), Assembly->GetBones().Num(), 3);
	TestTrue(TEXT("Removing an unconnected staged body is allowed"), Assembly->TryRemoveInstance(Second, Reason));
	TestFalse(TEXT("A repeated removal cannot duplicate stock"), Assembly->TryRemoveInstance(Second, Reason));
	TestEqual(TEXT("Removal returns exactly one item"), Assembly->GetRemainingQuantity(TEXT("Bone")), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFreeAssemblyMuscleConnectivityTest, "JGD2026.Assembly.Free.MuscleConnectivityAndBindings", FreeAssemblyTestFlags)
bool FFreeAssemblyMuscleConnectivityTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeFreeRulesAssembly();
	FText Reason;
	FGuid CoreBone, RemoteBone, BridgeMuscle, SameBoneMuscle, Invalid;
	if (!TestTrue(TEXT("Install bone connected to core circumference"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(64, 0), 0.0f, CoreBone, Reason))) { return false; }
	if (!TestTrue(TEXT("Install a remote staged bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(500, 180), 23.0f, RemoteBone, Reason))) { return false; }
	TestFalse(TEXT("Remote bone is not connected by a hidden grid edge"), Assembly->IsStructureConnected());
	TestFalse(TEXT("Start rejects the disconnected structure"), Assembly->TryStartPlaying(Reason));
	const FAssemblyMuscleEndpoint A = FreeRulesEndpoint(CoreBone, 20.25, 1.5);
	const FAssemblyMuscleEndpoint B = FreeRulesEndpoint(RemoteBone, -20.5, -2.5);
	TestFalse(TEXT("Muscle cannot be installed before choosing its key"), Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, Invalid, Reason));
	TestEqual(TEXT("Missing key preserves inventory"), Assembly->GetRemainingQuantity(TEXT("Muscle")), 6);
	TestTrue(TEXT("Select W"), Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestFalse(TEXT("Core is not a muscle attachment target"), Assembly->TryInstallMuscle(TEXT("Muscle"), A, FreeRulesEndpoint(FGuid(), 0), Invalid, Reason));
	TestFalse(TEXT("Two identical points on one bone are rejected"), Assembly->TryInstallMuscle(TEXT("Muscle"), A, A, Invalid, Reason));
	if (!TestTrue(TEXT("Muscle may connect a remote bone to the rooted structure"), Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, BridgeMuscle, Reason))) { return false; }
	TestTrue(TEXT("Muscle edge counts towards core connectivity"), Assembly->IsStructureConnected());
	TestFalse(TEXT("Removing the only muscle bridge is refused"), Assembly->TryRemoveInstance(BridgeMuscle, Reason));
	TestEqual(TEXT("Rejected bridge removal preserves stock"), Assembly->GetRemainingQuantity(TEXT("Muscle")), 5);
	TestTrue(TEXT("Different positions on the same bone are supported"), Assembly->TryInstallMuscle(TEXT("Muscle"), FreeRulesEndpoint(CoreBone, -10, -2), FreeRulesEndpoint(CoreBone, 10, 2), SameBoneMuscle, Reason));
	TestTrue(TEXT("Complete free assembly validates"), Assembly->ValidateAssembly().bValid);
	int32 Contractions = 0, Relaxations = 0;
	TSet<FGuid> Received;
	Assembly->OnMuscleDriveRequestedNative.AddLambda([&](FGuid Id, bool bContracting)
	{
		Received.Add(Id);
		if (bContracting) { ++Contractions; } else { ++Relaxations; }
	});
	TestFalse(TEXT("Assembly phase gates muscle drive"), Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestEqual(TEXT("No contraction was requested during assembly"), Contractions, 0);
	TestTrue(TEXT("Connected free assembly starts Playing"), Assembly->TryStartPlaying(Reason));
	TestTrue(TEXT("Press W requests contraction"), Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true));
	Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true);
	TestEqual(TEXT("Same key fans out once to both muscles"), Contractions, 2);
	Assembly->SetMuscleInput(EAssemblyMuscleKey::A, true);
	TestEqual(TEXT("An unbound key does not drive another muscle"), Contractions, 2);
	TestTrue(TEXT("Release W requests relaxation"), Assembly->SetMuscleInput(EAssemblyMuscleKey::W, false));
	Assembly->SetMuscleInput(EAssemblyMuscleKey::W, false);
	TestEqual(TEXT("Release fans out once to both muscles"), Relaxations, 2);
	TestTrue(TEXT("Requests identify the two actual installed muscles"), Received.Num() == 2 && Received.Contains(BridgeMuscle) && Received.Contains(SameBoneMuscle));
	TestEqual(TEXT("Input requests leave inventory unchanged"), Assembly->GetRemainingQuantity(TEXT("Muscle")), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFreeAssemblyJointIdentityTest, "JGD2026.Assembly.Free.SlidingCoreJointIdentityAndLimits", FreeAssemblyTestFlags)
bool FFreeAssemblyJointIdentityTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeFreeRulesAssembly();
	FText Reason;
	FGuid BoneId;
	if (!TestTrue(TEXT("Install a tangent core bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(64, 0), 0.0f, BoneId, Reason))) { return false; }
	TArray<FAssemblyJointInstance> Joints = Assembly->GetJoints();
	if (!TestEqual(TEXT("One core endpoint creates one joint"), Joints.Num(), 1)) { return false; }
	const FAssemblyJointInstance Original = Joints[0];
	TestTrue(TEXT("Core is represented by the joint's invalid BoneA ID"), !Original.BoneA.IsValid() && Original.BoneB == BoneId);
	TestTrue(TEXT("Customize joint kind"), Assembly->TrySetJointKind(Original.InstanceId, EAssemblyJointKind::BreakableFixed, Reason));
	TestTrue(TEXT("Customize angular limits"), Assembly->TrySetJointLimits(Original.InstanceId, -10.0f, 30.0f, Reason));
	TestFalse(TEXT("A full-turn angular range is refused"), Assembly->TrySetJointLimits(Original.InstanceId, -180.0f, 180.0f, Reason));
	const double AngleRadians = FMath::DegreesToRadians(5.0);
	const FVector2D MovedPosition(64.0 * FMath::Cos(AngleRadians), -64.0 * FMath::Sin(AngleRadians));
	TestTrue(TEXT("Rotate the body around the core while keeping its endpoint on the circumference"), Assembly->TryMoveFreeBone(BoneId, MovedPosition, 5.0f, Reason));
	Joints = Assembly->GetJoints();
	if (!TestEqual(TEXT("Sliding contact still has exactly one joint"), Joints.Num(), 1)) { return false; }
	TestTrue(TEXT("Joint identifier survives a sliding core contact"), Joints[0].InstanceId == Original.InstanceId);
	TestTrue(TEXT("Customized joint kind survives rebuilding"), Joints[0].Kind == EAssemblyJointKind::BreakableFixed);
	TestEqual(TEXT("Customized lower limit survives rebuilding"), Joints[0].MinAngleDegrees, -10.0f);
	TestEqual(TEXT("Customized upper limit survives rebuilding"), Joints[0].MaxAngleDegrees, 30.0f);
	TestFalse(TEXT("The core anchor actually moves around the circumference"), Joints[0].LocalAnchorA.Equals(Original.LocalAnchorA, 0.001));
	TestTrue(TEXT("Sliding core anchor remains on the configured circle"), FMath::IsNearlyEqual(Joints[0].LocalAnchorA.Size(), 32.0, 0.001));
	TestTrue(TEXT("Moved assembly remains valid and connected"), Assembly->ValidateAssembly().bValid);
	TestTrue(TEXT("Start playing with the customized joint"), Assembly->TryStartPlaying(Reason));
	TestTrue(TEXT("Breakable joint accepts a break request"), Assembly->RequestJointBreak(Original.InstanceId, Reason));
	TestEqual(TEXT("Intent alone does not remove a physical joint"), Assembly->GetJoints().Num(), 1);
	TestTrue(TEXT("Explicit physics confirmation removes the joint"), Assembly->ConfirmJointBroken(Original.InstanceId, Reason));
	TestFalse(TEXT("Repeated confirmation is rejected"), Assembly->ConfirmJointBroken(Original.InstanceId, Reason));
	TestTrue(TEXT("Playing may continue after a physical joint breaks"), Assembly->ValidateAssembly().bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFreeAssemblyRemovalAndRestartTest, "JGD2026.Assembly.Free.StagedRemovalAndThreeRestarts", FreeAssemblyTestFlags)
bool FFreeAssemblyRemovalAndRestartTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeFreeRulesAssembly();
	FText Reason;
	FGuid A, B, Connector, Staged, Remote, Bridge;
	TestTrue(TEXT("Install first rooted bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(64, 0), 0, A, Reason));
	TestTrue(TEXT("Install a required independent joint between bones"), Assembly->TryInstallJointPart(TEXT("Joint_Bar"), FVector2D(128, 0), 0, Connector, Reason));
	TestTrue(TEXT("Install second bone at the joint surface"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(192, 0), 0, B, Reason));
	TestTrue(TEXT("Install disconnected staging bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(700, 120), 11, Staged, Reason));
	TestTrue(TEXT("Install remote bone for a muscle bridge"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(400, -100), -17, Remote, Reason));
	Assembly->SelectMuscleKey(EAssemblyMuscleKey::S, Reason);
	TestTrue(TEXT("Connect remote bone with a muscle"), Assembly->TryInstallMuscle(TEXT("Muscle"), FreeRulesEndpoint(B, 20), FreeRulesEndpoint(Remote, -20), Bridge, Reason));
	TestFalse(TEXT("Whole structure remains unconnected because of the staged bone"), Assembly->IsStructureConnected());
	TestFalse(TEXT("Staging another bone does not permit removal of a rooted articulation bone"), Assembly->TryRemoveInstance(A, Reason));
	TestFalse(TEXT("Staging another bone does not permit removal of a rooted muscle bridge"), Assembly->TryRemoveInstance(Bridge, Reason));
	TestEqual(TEXT("Both rejected removals preserve bone stock"), Assembly->GetRemainingQuantity(TEXT("Bone")), 8);
	TestEqual(TEXT("Both rejected removals preserve muscle stock"), Assembly->GetRemainingQuantity(TEXT("Muscle")), 5);
	TestEqual(TEXT("No installed bone or intervening joint was lost"), Assembly->GetBones().Num(), 5);
	TestFalse(TEXT("The required connector cannot strand rooted bones"), Assembly->TryRemoveInstance(Connector, Reason));
	TestEqual(TEXT("No muscle bridge was lost"), Assembly->GetMuscles().Num(), 1);
	TestTrue(TEXT("The disconnected staged bone can still be removed"), Assembly->TryRemoveInstance(Staged, Reason));
	TestTrue(TEXT("Removing staging leaves the rooted structure connected"), Assembly->IsStructureConnected());
	TestTrue(TEXT("Inventory and remaining graph validate"), Assembly->ValidateAssembly().bValid);
	TestTrue(TEXT("Reset the staging setup"), Assembly->RestartAssembly(Reason));

	int32 Contractions = 0, Relaxations = 0, CoreEdges = 0;
	bool bReentrantResetRejected = true;
	Assembly->OnMuscleDriveRequestedNative.AddLambda([&](FGuid Id, bool bContracting)
	{
		if (bContracting)
		{
			++Contractions;
			FText NestedReason;
			bReentrantResetRejected &= !Assembly->RestartAssembly(NestedReason) && !NestedReason.IsEmpty();
		}
		else { ++Relaxations; }
	});
	Assembly->OnCoreRotationRequestedNative.AddLambda([&](bool bHeld) { ++CoreEdges; });
	TSet<FGuid> OldIds;
	for (int32 Round = 0; Round < 3; ++Round)
	{
		FGuid Bone, Muscle;
		const int32 BeforeContractions = Contractions, BeforeRelaxations = Relaxations, BeforeCore = CoreEdges;
		TestTrue(TEXT("Every new round enters Assembly"), Assembly->GetPhase() == EAssemblyPhase::Assembly);
		TestTrue(TEXT("Each round installs a rooted bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(64, 0), 0, Bone, Reason));
		TestFalse(TEXT("Bone instance IDs are not reused across restarts"), OldIds.Contains(Bone));
		OldIds.Add(Bone);
		Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason);
		TestTrue(TEXT("Each round installs a same-bone muscle"), Assembly->TryInstallMuscle(TEXT("Muscle"), FreeRulesEndpoint(Bone, -12), FreeRulesEndpoint(Bone, 12), Muscle, Reason));
		TestFalse(TEXT("Muscle instance IDs are not reused across restarts"), OldIds.Contains(Muscle));
		OldIds.Add(Muscle);
		TestFalse(TEXT("Assembly rejects core drive"), Assembly->SetCoreRotationInput(true));
		Assembly->SetCoreRotationInput(true);
		TestEqual(TEXT("Assembly emits no core request"), CoreEdges - BeforeCore, 0);
		TestTrue(TEXT("Each complete round starts Playing"), Assembly->TryStartPlaying(Reason));
		TestTrue(TEXT("Core drive is available only after Playing"), Assembly->SetCoreRotationInput(true));
		Assembly->SetCoreRotationInput(true);
		Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true);
		Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true);
		TestTrue(TEXT("Restart clears the active round"), Assembly->RestartAssembly(Reason));
		TestEqual(TEXT("One contraction per round, including duplicate-input rejection"), Contractions - BeforeContractions, 1);
		TestEqual(TEXT("Restart sends one relaxation before deleting the muscle"), Relaxations - BeforeRelaxations, 1);
		TestEqual(TEXT("Core press and reset release occur exactly once each"), CoreEdges - BeforeCore, 2);
		TestTrue(TEXT("Restart removes bones, muscles and joints"), Assembly->GetBones().IsEmpty() && Assembly->GetMuscles().IsEmpty() && Assembly->GetJoints().IsEmpty());
		TestEqual(TEXT("Restart restores the level's bone stock"), Assembly->GetRemainingQuantity(TEXT("Bone")), 12);
		TestEqual(TEXT("Restart restores the level's muscle stock"), Assembly->GetRemainingQuantity(TEXT("Muscle")), 6);
		TestTrue(TEXT("Restart clears key selection and held rotation"), Assembly->GetSelectedMuscleKey() == EAssemblyMuscleKey::None && !Assembly->IsCoreRotationHeld());
		TestTrue(TEXT("Restart preserves the Free placement configuration"), Assembly->GetPlacementMode() == EAssemblyPlacementMode::Free);
		TestTrue(TEXT("Fresh round satisfies inventory and graph invariants"), Assembly->ValidateAssembly().bValid);
	}
	TestTrue(TEXT("Drive callbacks cannot reset a transaction while it is notifying"), bReentrantResetRejected);
	return true;
}

#endif
