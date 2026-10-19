#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"

namespace
{
	UAssemblyComponent* MakeAssembly(bool bSingleCellBones = false)
	{
		UAssemblyComponent* Component = NewObject<UAssemblyComponent>();
		// These transaction cases use a fixed four-item budget independently of the game's level stock.
		Component->DefaultBones[0].InitialQuantity = 4;
		Component->DefaultPlacementMode = EAssemblyPlacementMode::Grid;
		if (bSingleCellBones)
		{
			Component->DefaultBones[0].Footprint = { FIntPoint(0, 0) };
		}
		FText Reason;
		Component->EnterAssembly(Reason);
		return Component;
	}

	FAssemblyMuscleEndpoint Endpoint(FGuid BoneId, double X, double Z)
	{
		FAssemblyMuscleEndpoint Result;
		Result.BoneId = BoneId;
		Result.BoneLocalPoint = FVector2D(X, Z);
		return Result;
	}

	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyCoordinatesTest, "JGD2026.Assembly.CoordinatesAndRotation", TestFlags)
bool FAssemblyCoordinatesTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* C = MakeAssembly();
	TestTrue(TEXT("Core cell center is local origin"), C->CellToLocal(FIntPoint(2, 2)).Equals(FVector::ZeroVector));
	for (int32 Row = 0; Row < 6; ++Row)
	{
		for (int32 Column = 0; Column < 6; ++Column)
		{
			const FIntPoint Cell(Column, Row);
			TestEqual(TEXT("All 36 cell centers round-trip"), C->LocalToCell(C->CellToLocal(Cell)), Cell);
		}
	}
	TestEqual(TEXT("Upper-left boundary belongs to first cell"), C->LocalToCell(FVector(-160, 0, 160)), FIntPoint(0, 0));
	TestFalse(TEXT("Far boundary is outside"), C->IsCellInBounds(C->LocalToCell(FVector(224, 0, -224))));
	TestEqual(TEXT("Outside upper-left floors instead of truncating"), C->LocalToCell(FVector(-160.1, 0, 160.1)), FIntPoint(-1, -1));

	FText Reason;
	FAssemblyGridSettings Grid;
	Grid.CoreCells = { FIntPoint(2, 1), FIntPoint(2, 2) };
	FAssemblyBoneDefinition L;
	L.Footprint = { FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, 1) };
	TestTrue(TEXT("Configure L shape"), C->ConfigureAssembly(Grid, { L }, {}, Reason));
	const FVector UnrotatedCenters[] = { FVector(-32, 0, 32), FVector(32, 0, 32), FVector(-32, 0, -32) };
	TArray<FIntPoint> LastCells;
	for (int32 Turn = 0; Turn < 4; ++Turn)
	{
		const FAssemblyPlacementResult Preview = C->ValidateBonePlacement(TEXT("Bone"), FIntPoint(3, 1), Turn, FGuid());
		TestTrue(TEXT("All four L rotations can connect"), Preview.bValid);
		TestEqual(TEXT("Rotation keeps three occupied cells"), Preview.OccupiedCells.Num(), 3);
		for (const FVector Center : UnrotatedCenters)
		{
			const FIntPoint Cell = C->LocalToCell(Preview.LocalVisualTransform.TransformPosition(Center));
			TestTrue(TEXT("Actual rotated visual cell is occupied"), Preview.OccupiedCells.Contains(Cell));
		}
		if (Turn > 0)
		{
			TestTrue(TEXT("L footprint changes when rotated"), Preview.OccupiedCells != LastCells);
		}
		LastCells = Preview.OccupiedCells;
	}
	TestEqual(TEXT("Negative rotation normalizes"), C->ValidateBonePlacement(TEXT("Bone"), FIntPoint(3, 1), -1, FGuid()).QuarterTurns, 3);
	TestEqual(TEXT("Large rotation normalizes"), C->ValidateBonePlacement(TEXT("Bone"), FIntPoint(3, 1), 9, FGuid()).QuarterTurns, 1);
	TestFalse(TEXT("Extreme anchor cannot overflow into grid"), C->ValidateBonePlacement(TEXT("Bone"), FIntPoint(MAX_int32, 1), 0, FGuid()).bValid);

	Grid.TopLeftLocal.Y = 17;
	TestTrue(TEXT("Configure offset grid plane"), C->ConfigureAssembly(Grid, { L }, {}, Reason));
	USceneComponent* Frame = NewObject<USceneComponent>();
	const FTransform FrameTransform(FQuat(FVector(0.2, 1, 0.3).GetSafeNormal(), 0.47), FVector(100, 22, 300), FVector(2, 0.75, 1.5));
	Frame->SetWorldTransform(FrameTransform);
	C->SetGridFrame(Frame);
	for (int32 Row = 0; Row < 6; ++Row)
	{
		for (int32 Column = 0; Column < 6; ++Column)
		{
			const FIntPoint Cell(Column, Row);
			TestEqual(TEXT("Translated/rotated/scaled world centers round-trip"), C->WorldToCell(C->CellToWorld(Cell)), Cell);
		}
	}
	const FIntPoint Target(5, 4);
	const FVector WorldHit = C->CellToWorld(Target);
	const FVector Normal = FrameTransform.TransformVectorNoScale(FVector(0, 1, 0));
	FVector LocalHit;
	FIntPoint HitCell;
	TestTrue(TEXT("Ray intersects the transformed grid plane"), C->RayToGrid(WorldHit + Normal * 500, -Normal, LocalHit, HitCell));
	TestEqual(TEXT("Ray chooses same cell as installed geometry"), HitCell, Target);
	TestTrue(TEXT("Ray respects plane's local Y offset"), LocalHit.Equals(C->CellToLocal(Target), 0.001));
	TestFalse(TEXT("Parallel ray rejected"), C->RayToGrid(WorldHit + Normal * 500, FrameTransform.TransformVectorNoScale(FVector(1, 0, 0)), LocalHit, HitCell));
	TestFalse(TEXT("Behind-origin ray rejected"), C->RayToGrid(WorldHit + Normal * 500, Normal, LocalHit, HitCell));
	Frame->SetWorldScale3D(FVector(1, 0, 1));
	TestFalse(TEXT("Zero-scale frame rejected"), C->RayToGrid(WorldHit + Normal * 500, -Normal, LocalHit, HitCell));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyConfigurationTest, "JGD2026.Assembly.ConfigurationTransactions", TestFlags)
bool FAssemblyConfigurationTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* C = MakeAssembly();
	FText Reason;
	FGuid Id;
	TestTrue(TEXT("Initial install"), C->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, Id, Reason));
	FAssemblyGridSettings Grid;
	Grid.Rows = 0;
	TestFalse(TEXT("Invalid dimensions refused"), C->ConfigureAssembly(Grid, C->DefaultBones, C->DefaultMuscles, Reason));
	TestFalse(TEXT("Failure supplies reason"), Reason.IsEmpty());
	Grid = FAssemblyGridSettings();
	Grid.CoreCells.Add(FIntPoint(2, 2));
	TestFalse(TEXT("Duplicate core refused"), C->ConfigureAssembly(Grid, C->DefaultBones, C->DefaultMuscles, Reason));
	Grid.CoreCells = { FIntPoint(2, 2), FIntPoint(3, 3) };
	TestFalse(TEXT("Diagonal core shape refused"), C->ConfigureAssembly(Grid, C->DefaultBones, C->DefaultMuscles, Reason));
	Grid = FAssemblyGridSettings();
	TArray<FAssemblyBoneDefinition> Bones = C->DefaultBones;
	Bones[0].Footprint.Add(FIntPoint(0, 0));
	TestFalse(TEXT("Duplicate footprint refused"), C->ConfigureAssembly(Grid, Bones, C->DefaultMuscles, Reason));
	Bones[0].Footprint = { FIntPoint(0, 0), FIntPoint(1, 1) };
	TestFalse(TEXT("Disconnected footprint refused"), C->ConfigureAssembly(Grid, Bones, C->DefaultMuscles, Reason));
	Bones = C->DefaultBones;
	Bones[0].InitialQuantity = -1;
	TestFalse(TEXT("Negative quantity refused"), C->ConfigureAssembly(Grid, Bones, C->DefaultMuscles, Reason));
	TArray<FAssemblyMuscleDefinition> Muscles = C->DefaultMuscles;
	Muscles[0].TypeId = TEXT("Bone");
	TestFalse(TEXT("Shared type ID across categories refused"), C->ConfigureAssembly(Grid, C->DefaultBones, Muscles, Reason));
	TestEqual(TEXT("Failed configure preserves inventory"), C->GetRemainingQuantity(TEXT("Bone")), 3);
	FAssemblyBoneInstance Bone;
	TestTrue(TEXT("Failed configure preserves original instance"), C->GetBone(Id, Bone));
	TestEqual(TEXT("Failed configure preserves grid"), C->GetGridSettings().Rows, 6);
	C->DefaultGrid.CellSize = 128;
	TestEqual(TEXT("Editing defaults does not mutate active round"), C->GetGridSettings().CellSize, 64.0f);
	TestTrue(TEXT("Restart"), C->RestartAssembly(Reason));
	TestEqual(TEXT("Restart uses validated level snapshot"), C->GetGridSettings().CellSize, 64.0f);
	TestEqual(TEXT("Restart restores stock"), C->GetRemainingQuantity(TEXT("Bone")), 4);
	TestTrue(TEXT("Inventory invariant after restart"), C->ValidateAssembly().bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyBonesTest, "JGD2026.Assembly.BoneTransactionsAndConnectivity", TestFlags)
bool FAssemblyBonesTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* C = MakeAssembly(true);
	FText Reason;
	FGuid A, B, D, Invalid;
	TestTrue(TEXT("Core-adjacent install needs no muscle key"), C->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, A, Reason));
	TestTrue(TEXT("Second bone connected via first"), C->TryInstallBone(TEXT("Bone"), FIntPoint(4, 2), 0, B, Reason));
	TestFalse(TEXT("Core overlap refused"), C->TryInstallBone(TEXT("Bone"), FIntPoint(2, 2), 0, Invalid, Reason));
	TestFalse(TEXT("Bone overlap refused"), C->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, Invalid, Reason));
	TestFalse(TEXT("Diagonal-only contact refused"), C->TryInstallBone(TEXT("Bone"), FIntPoint(1, 1), 0, Invalid, Reason));
	TestFalse(TEXT("Negative column refused"), C->TryInstallBone(TEXT("Bone"), FIntPoint(-1, 2), 0, Invalid, Reason));
	TestFalse(TEXT("Far column refused"), C->TryInstallBone(TEXT("Bone"), FIntPoint(6, 2), 0, Invalid, Reason));
	TestFalse(TEXT("Failed install returns no ID"), Invalid.IsValid());
	TestEqual(TEXT("Invalid releases never consume stock"), C->GetRemainingQuantity(TEXT("Bone")), 2);
	TestFalse(TEXT("Removing bridge refused"), C->TryRemoveInstance(A, Reason));
	TestFalse(TEXT("Moving bridge refuses to strand existing bone"), C->TryMoveBone(A, FIntPoint(2, 3), 0, Reason));
	FAssemblyBoneInstance Original;
	TestTrue(TEXT("Failed move restores original instance"), C->GetBone(A, Original));
	TestEqual(TEXT("Failed move preserves original position"), Original.AnchorCell, FIntPoint(3, 2));
	TestEqual(TEXT("Rejected removal/move preserves stock"), C->GetRemainingQuantity(TEXT("Bone")), 2);
	TestTrue(TEXT("Move leaf to legal position"), C->TryMoveBone(B, FIntPoint(3, 1), 3, Reason));
	FAssemblyBoneInstance Moved;
	TestTrue(TEXT("Move keeps same ID"), C->GetBone(B, Moved));
	TestEqual(TEXT("Move keeps stock"), C->GetRemainingQuantity(TEXT("Bone")), 2);
	TestTrue(TEXT("Move commits occupancy and orientation"), Moved.OccupiedCells.Contains(FIntPoint(3, 1)) && Moved.QuarterTurns == 3);
	TestFalse(TEXT("Unknown move ID refused"), C->TryMoveBone(FGuid::NewGuid(), FIntPoint(2, 3), 0, Reason));
	for (int32 Cycle = 0; Cycle < 20; ++Cycle)
	{
		TestTrue(TEXT("Install/remove repeatedly without losses"), C->TryInstallBone(TEXT("Bone"), FIntPoint(2, 3), 0, D, Reason));
		TestEqual(TEXT("One stock deducted"), C->GetRemainingQuantity(TEXT("Bone")), 1);
		TestTrue(TEXT("Remove returns stock"), C->TryRemoveInstance(D, Reason));
		TestFalse(TEXT("Same removal cannot return stock twice"), C->TryRemoveInstance(D, Reason));
		TestEqual(TEXT("Exact stock restored"), C->GetRemainingQuantity(TEXT("Bone")), 2);
		TestTrue(TEXT("Inventory/occupancy invariant after each cycle"), C->ValidateAssembly().bValid);
	}
	TestTrue(TEXT("Remove moved leaf"), C->TryRemoveInstance(B, Reason));
	TestTrue(TEXT("Remove last bone"), C->TryRemoveInstance(A, Reason));
	TestEqual(TEXT("All inventory returned"), C->GetRemainingQuantity(TEXT("Bone")), 4);
	TestTrue(TEXT("Core-only start is allowed without an invented minimum"), C->TryStartPlaying(Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyMusclesTest, "JGD2026.Assembly.ContinuousMuscleEndpoints", TestFlags)
bool FAssemblyMusclesTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* C = MakeAssembly();
	FText Reason;
	FGuid BoneId, M1, M2, Invalid;
	TestTrue(TEXT("Install bone"), C->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, BoneId, Reason));
	const FAssemblyMuscleEndpoint A = Endpoint(BoneId, -20.25, 3.5);
	const FAssemblyMuscleEndpoint B = Endpoint(BoneId, 25.75, -3.25);
	TestFalse(TEXT("Muscle needs key first"), C->TryInstallMuscle(TEXT("Muscle"), A, B, Invalid, Reason));
	TestEqual(TEXT("Missing-key failure keeps inventory"), C->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestTrue(TEXT("Select W"), C->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestFalse(TEXT("Same bone identical endpoint refused"), C->TryInstallMuscle(TEXT("Muscle"), A, A, Invalid, Reason));
	TestFalse(TEXT("Unknown bone/core endpoint refused"), C->TryInstallMuscle(TEXT("Muscle"), A, Endpoint(FGuid(), 0, 0), Invalid, Reason));
	TestFalse(TEXT("Outside bone endpoint refused"), C->TryInstallMuscle(TEXT("Muscle"), A, Endpoint(BoneId, 1024, 0), Invalid, Reason));
	TestFalse(TEXT("Footprint padding is not actual bone thickness"), C->TryInstallMuscle(TEXT("Muscle"), A, Endpoint(BoneId, 0, 30), Invalid, Reason));
	TestEqual(TEXT("Padding rejection keeps inventory"), C->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestTrue(TEXT("Same bone distinct continuous endpoints allowed"), C->TryInstallMuscle(TEXT("Muscle"), A, B, M1, Reason));
	TestTrue(TEXT("Several muscles may share a key and endpoints"), C->TryInstallMuscle(TEXT("Muscle"), A, B, M2, Reason));
	TestTrue(TEXT("IDs are distinct"), M1 != M2);
	TestEqual(TEXT("Two installs consume exactly two items"), C->GetRemainingQuantity(TEXT("Muscle")), 1);
	FAssemblyMuscleInstance Muscle;
	C->GetMuscle(M1, Muscle);
	TestTrue(TEXT("Continuous endpoint never snapped"), Muscle.EndpointA.BoneLocalPoint.Equals(A.BoneLocalPoint));
	TestFalse(TEXT("Bone with attached muscles cannot be removed"), C->TryRemoveInstance(BoneId, Reason));
	TestTrue(TEXT("Bone may move/rotate with stable muscle-local endpoints"), C->TryMoveBone(BoneId, FIntPoint(2, 3), 1, Reason));
	C->GetMuscle(M1, Muscle);
	TestTrue(TEXT("Moving bone preserves muscle-local position"), Muscle.EndpointA.BoneLocalPoint.Equals(A.BoneLocalPoint));
	TestEqual(TEXT("Moving bone preserves muscle stock"), C->GetRemainingQuantity(TEXT("Muscle")), 1);
	FAssemblyBoneInstance Bone;
	C->GetBone(BoneId, Bone);
	FVector Position;
	TestTrue(TEXT("Endpoint uses updated bone transform"), C->GetEndpointWorldPosition(A, Position));
	TestTrue(TEXT("Logical endpoint world position is consistent"), Position.Equals(Bone.LocalVisualTransform.TransformPosition(FVector(A.BoneLocalPoint.X, 0, A.BoneLocalPoint.Y))));

	USceneComponent* Body = NewObject<USceneComponent>();
	const FTransform PhysicalPose(FQuat(FVector(0, 1, 0), 0.51), FVector(500, 5, 600));
	Body->SetWorldTransform(PhysicalPose);
	TestTrue(TEXT("Register actual physical pose source"), C->RegisterBonePoseSource(BoneId, Body, Reason));
	TestTrue(TEXT("Read endpoint physical pose"), C->GetEndpointWorldPosition(A, Position));
	TestTrue(TEXT("Physical endpoint follows body, not grid assumption"), Position.Equals(PhysicalPose.TransformPosition(FVector(A.BoneLocalPoint.X, 0, A.BoneLocalPoint.Y))));
	FAssemblyMuscleEndpoint Hit;
	TestTrue(TEXT("Continuous bone hit finds physical body"), C->FindBoneAtWorldPosition(Position, Hit));
	TestTrue(TEXT("Hit gives same bone-local endpoint"), Hit.BoneId == BoneId && Hit.BoneLocalPoint.Equals(A.BoneLocalPoint, 0.001));
	TestTrue(TEXT("Reading pose never transforms the physics body"), Body->GetComponentTransform().Equals(PhysicalPose));
	C->UnregisterBonePoseSource(BoneId);
	TestTrue(TEXT("Rebind to D"), C->TrySetMuscleKey(M1, EAssemblyMuscleKey::D, Reason));
	TestFalse(TEXT("Muscle cannot be rebound to None"), C->TrySetMuscleKey(M1, EAssemblyMuscleKey::None, Reason));
	const FAssemblyMuscleEndpoint NewA = Endpoint(BoneId, 5.5, 2.25);
	TestTrue(TEXT("Move muscle keeps identity"), C->TryMoveMuscle(M1, NewA, B, Reason));
	TestFalse(TEXT("Invalid muscle move refused"), C->TryMoveMuscle(M1, A, Endpoint(BoneId, 500, 0), Reason));
	C->GetMuscle(M1, Muscle);
	TestTrue(TEXT("Invalid move preserves previous endpoints and binding"), Muscle.EndpointA.BoneLocalPoint.Equals(NewA.BoneLocalPoint) && Muscle.BindingKey == EAssemblyMuscleKey::D);
	TestEqual(TEXT("Moving/rebinding consumes no inventory"), C->GetRemainingQuantity(TEXT("Muscle")), 1);
	TestTrue(TEXT("Remove first muscle"), C->TryRemoveInstance(M1, Reason));
	TestTrue(TEXT("Remove second muscle"), C->TryRemoveInstance(M2, Reason));
	TestEqual(TEXT("Muscle stock restored"), C->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestTrue(TEXT("Remove bone after muscles"), C->TryRemoveInstance(BoneId, Reason));
	TestTrue(TEXT("No orphan endpoint remains"), C->GetMuscles().IsEmpty() && C->ValidateAssembly().bValid);

	FAssemblyBoneDefinition L;
	L.Footprint = { FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, 1) };
	TestTrue(TEXT("Configure nonrectangular shape"), C->ConfigureAssembly(FAssemblyGridSettings(), { L }, { FAssemblyMuscleDefinition() }, Reason));
	TestTrue(TEXT("Install L"), C->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, BoneId, Reason));
	C->SelectMuscleKey(EAssemblyMuscleKey::W, Reason);
	TestFalse(TEXT("Empty cell within bounding rectangle is not a bone endpoint"), C->TryInstallMuscle(TEXT("Muscle"), Endpoint(BoneId, -32, 32), Endpoint(BoneId, 32, -32), Invalid, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInputTest, "JGD2026.Assembly.InputAndThreeRestarts", TestFlags)
bool FAssemblyInputTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* C = MakeAssembly();
	int32 Contractions = 0;
	int32 Relaxations = 0;
	int32 CoreRequests = 0;
	bool bReentrantMutationRejected = true;
	C->OnMuscleDriveRequestedNative.AddLambda([&](FGuid Id, bool bContracting)
	{
		if (bContracting)
		{
			++Contractions;
			FText ReentrantReason;
			bReentrantMutationRejected &= !C->RestartAssembly(ReentrantReason) && !ReentrantReason.IsEmpty();
		}
		else
		{
			++Relaxations;
		}
	});
	C->OnCoreRotationRequestedNative.AddLambda([&](bool bHeld) { ++CoreRequests; });
	FText Reason;
	TSet<FGuid> PreviousIds;
	for (int32 Round = 0; Round < 3; ++Round)
	{
		const int32 BeforeContract = Contractions;
		const int32 BeforeRelax = Relaxations;
		const int32 BeforeCore = CoreRequests;
		FGuid BoneId, M1, M2, M3, Invalid;
		TestTrue(TEXT("Every round starts Assembly"), C->GetPhase() == EAssemblyPhase::Assembly);
		TestTrue(TEXT("Each round installs bone"), C->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, BoneId, Reason));
		TestFalse(TEXT("Restart never reuses old instance IDs"), PreviousIds.Contains(BoneId));
		PreviousIds.Add(BoneId);
		const FAssemblyMuscleEndpoint A = Endpoint(BoneId, -20, 0);
		const FAssemblyMuscleEndpoint B = Endpoint(BoneId, 20, 0);
		C->SelectMuscleKey(EAssemblyMuscleKey::W, Reason);
		TestTrue(TEXT("Install first W muscle"), C->TryInstallMuscle(TEXT("Muscle"), A, B, M1, Reason));
		TestTrue(TEXT("Install second W muscle"), C->TryInstallMuscle(TEXT("Muscle"), A, B, M2, Reason));
		C->SelectMuscleKey(EAssemblyMuscleKey::A, Reason);
		TestTrue(TEXT("Install A muscle"), C->TryInstallMuscle(TEXT("Muscle"), A, B, M3, Reason));
		TestFalse(TEXT("Exhausted inventory refuses another muscle"), C->TryInstallMuscle(TEXT("Muscle"), A, B, Invalid, Reason));
		TestEqual(TEXT("Exhausted stock stays zero"), C->GetRemainingQuantity(TEXT("Muscle")), 0);
		TestFalse(TEXT("Assembly gates muscle input"), C->SetMuscleInput(EAssemblyMuscleKey::W, true));
		TestEqual(TEXT("No contraction request during Assembly"), Contractions, BeforeContract);
		TestFalse(TEXT("Assembly rejects core rotation intent"), C->SetCoreRotationInput(true));
		C->SetCoreRotationInput(true);
		TestEqual(TEXT("No core drive is emitted during Assembly"), CoreRequests - BeforeCore, 0);
		TestTrue(TEXT("Start Playing"), C->TryStartPlaying(Reason));
		TestTrue(TEXT("Repeated start is harmless"), C->TryStartPlaying(Reason));
		TestFalse(TEXT("An ignored Assembly press is not held across start"), C->IsCoreRotationHeld());
		TestTrue(TEXT("Playing accepts a new Space press"), C->SetCoreRotationInput(true));
		C->SetCoreRotationInput(true);
		TestEqual(TEXT("Playing core intent duplicates coalesce"), CoreRequests - BeforeCore, 1);
		TestFalse(TEXT("Playing blocks editing"), C->TryRemoveInstance(M1, Reason));
		TestFalse(TEXT("Playing blocks key selection"), C->SelectMuscleKey(EAssemblyMuscleKey::D, Reason));
		TestTrue(TEXT("W press"), C->SetMuscleInput(EAssemblyMuscleKey::W, true));
		C->SetMuscleInput(EAssemblyMuscleKey::W, true);
		TestEqual(TEXT("Both W muscles receive one request"), Contractions - BeforeContract, 2);
		C->SetMuscleInput(EAssemblyMuscleKey::A, true);
		TestEqual(TEXT("A only drives bound muscle"), Contractions - BeforeContract, 3);
		C->SetMuscleInput(EAssemblyMuscleKey::W, false);
		C->SetMuscleInput(EAssemblyMuscleKey::W, false);
		TestEqual(TEXT("W release relaxes exactly two muscles"), Relaxations - BeforeRelax, 2);
		C->SetMuscleInput(EAssemblyMuscleKey::W, true);
		C->ClearInputState();
		TestEqual(TEXT("Focus-loss clear relaxes remaining active muscles"), Relaxations - BeforeRelax, 5);
		TestFalse(TEXT("Focus-loss clear stops core rotation"), C->IsCoreRotationHeld());
		C->SetMuscleInput(EAssemblyMuscleKey::W, true);
		C->SetCoreRotationInput(true);
		TestTrue(TEXT("Playing assembly remains internally valid"), C->ValidateAssembly().bValid);
		TestTrue(TEXT("Restart clears round"), C->RestartAssembly(Reason));
		TestEqual(TEXT("Restart releases active muscles before destruction"), Relaxations - BeforeRelax, 7);
		TestEqual(TEXT("Input requests remain constant across three rounds"), Contractions - BeforeContract, 7);
		TestEqual(TEXT("Core begin/end events remain constant across rounds"), CoreRequests - BeforeCore, 4);
		TestTrue(TEXT("Restart removes all instances"), C->GetBones().IsEmpty() && C->GetMuscles().IsEmpty());
		TestEqual(TEXT("Restart restores all bone stock"), C->GetRemainingQuantity(TEXT("Bone")), 4);
		TestEqual(TEXT("Restart restores all muscle stock"), C->GetRemainingQuantity(TEXT("Muscle")), 3);
		TestTrue(TEXT("Restart clears selected key"), C->GetSelectedMuscleKey() == EAssemblyMuscleKey::None);
		TestTrue(TEXT("Restart returns to stable data"), C->ValidateAssembly().bValid);
	}
	TestTrue(TEXT("Listeners cannot mutate a transaction during notifications"), bReentrantMutationRejected);
	return true;
}

#endif
