#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "../AssemblyFreeRules.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr EAutomationTestFlags JointPartTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UAssemblyComponent* MakeJointPartTestAssembly(EAssemblyJointPartShape Shape = EAssemblyJointPartShape::Cross)
	{
		UAssemblyComponent* Assembly = NewObject<UAssemblyComponent>();
		FAssemblyBoneDefinition Bone;
		Bone.InitialQuantity = 8;
		FAssemblyBoneDefinition Part = AssemblyRigidPartMetrics::MakeJointPartDefinition(Shape, 64.0f / (Shape == EAssemblyJointPartShape::Cross ? 960.0f : 640.0f));
		FAssemblyFreeSettings Settings;
		Settings.ConnectionDistance = 16;
		FText Reason;
		Assembly->ConfigureFreeAssembly(Settings, { Bone, Part }, { FAssemblyMuscleDefinition() }, Reason);
		return Assembly;
	}

	FAssemblyMuscleEndpoint JointPartEndpoint(FGuid Id, FVector2D Point)
	{
		FAssemblyMuscleEndpoint Result;
		Result.BoneId = Id;
		Result.BoneLocalPoint = Point;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPartMetricsTest, "JGD2026.Assembly.JointParts.ArtworkRatioAndConfiguration", JointPartTestFlags)
bool FAssemblyJointPartMetricsTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = NewObject<UAssemblyComponent>();
	FText Reason;
	TestEqual(TEXT("Seven joint shapes have independent default stocks"), Assembly->DefaultJointParts.Num(), 7);
	for (const FAssemblyBoneDefinition& Part : Assembly->DefaultJointParts)
	{
		FNormalBonePhysicalStats Stats;
		TestTrue(TEXT("Every default shape has valid body metrics"), AssemblyRigidPartMetrics::Evaluate(Part, Stats, Reason));
		const FVector2D Canvas = AssemblyRigidPartMetrics::GetJointArtworkPixelSize(Part.JointShape);
		TestTrue(TEXT("Both body axes retain one source-pixel scale"), FMath::IsNearlyEqual(Part.JointPartSize.X / Canvas.X, Part.JointPartSize.Y / Canvas.Y, 0.000001));
		TestEqual(TEXT("Each shape defaults to two pieces"), Part.InitialQuantity, 2);
		TestTrue(TEXT("Purple defaults to hinge while yellow shapes default to fixed"), Part.DefaultJointKind
			== (Part.JointShape == EAssemblyJointPartShape::Round ? EAssemblyJointKind::Hinge : EAssemblyJointKind::Fixed));
		for (FVector2D Port : AssemblyRigidPartMetrics::GetConnectionPoints(Part))
		{
			TestTrue(TEXT("Every port is inside its declared body"), AssemblyFreeRules::IsPointOnRigidPart(Part, Port));
		}
	}
	FAssemblyBoneDefinition Invalid = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar);
	Invalid.JointPartSize.Y *= 2;
	FNormalBonePhysicalStats Stats;
	TestFalse(TEXT("Nonuniform joint art/body scaling is refused"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Cross);
	const FVector2D DuplicatePort = Invalid.ConnectionPoints[0];
	Invalid.ConnectionPoints.Add(DuplicatePort);
	TestFalse(TEXT("Duplicate slots are refused"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid.ConnectionPoints = { FVector2D(10000, 0) };
	TestFalse(TEXT("A slot cannot leave the body frame"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Round);
	Invalid.DefaultJointKind = EAssemblyJointKind::Fixed;
	TestFalse(TEXT("A purple Round definition cannot silently become fixed"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid.DefaultJointKind = EAssemblyJointKind::BreakableHinge;
	TestTrue(TEXT("Purple Round can opt into its own breakable variant"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Cross);
	Invalid.DefaultJointKind = EAssemblyJointKind::BreakableHinge;
	TestFalse(TEXT("A yellow connector definition cannot opt into an active variant"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid.DefaultJointKind = EAssemblyJointKind::BreakableFixed;
	TestTrue(TEXT("A yellow connector can opt into its fixed breakable variant"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::FixedRound);
	TestEqual(TEXT("The yellow circular piece has its own stable type ID"), Invalid.TypeId, FName(TEXT("Joint_FixedRound")));
	TestTrue(TEXT("Both circular colors retain the same source canvas"), AssemblyRigidPartMetrics::GetJointArtworkPixelSize(Invalid.JointShape) == FVector2D(640, 640));
	TestEqual(TEXT("Yellow circular pieces have four distinct attachment slots"), AssemblyRigidPartMetrics::GetConnectionPoints(Invalid).Num(), 4);
	TestTrue(TEXT("A yellow circular piece starts with valid fixed metrics"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid.DefaultJointKind = EAssemblyJointKind::Hinge;
	TestFalse(TEXT("A yellow circular piece cannot become active"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid.DefaultJointKind = EAssemblyJointKind::BreakableHinge;
	TestFalse(TEXT("A yellow circular piece cannot become breakable active"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	Invalid.DefaultJointKind = EAssemblyJointKind::BreakableFixed;
	TestTrue(TEXT("A yellow circular piece permits its breakable fixed variant"), AssemblyRigidPartMetrics::Evaluate(Invalid, Stats, Reason));
	TestTrue(TEXT("Default entry initializes all stock categories"), Assembly->EnterAssembly(Reason));
	TestEqual(TEXT("Default entry includes seven joint categories"), Assembly->GetJointPartDefinitions().Num(), 7);
	TestEqual(TEXT("Default entry has bone plus seven joint definitions"), Assembly->GetBoneDefinitions().Num(), 8);
	TestTrue(TEXT("Explicit Free configuration keeps its own complete list"), Assembly->ConfigureFreeAssembly(FAssemblyFreeSettings(), { FAssemblyBoneDefinition() }, {}, Reason));
	TestTrue(TEXT("Explicit configuration never reinjects defaults"), Assembly->GetJointPartDefinitions().IsEmpty());
	TestTrue(TEXT("Restart preserves the explicit configuration"), Assembly->RestartAssembly(Reason));
	TestTrue(TEXT("Restart still has no injected joints"), Assembly->GetJointPartDefinitions().IsEmpty());
	const FAssemblyBoneDefinition Joint = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Cross);
	TestFalse(TEXT("Legacy Grid configuration rejects independent joints"), Assembly->ConfigureAssembly(FAssemblyGridSettings(), { Joint }, {}, Reason));
	TestTrue(TEXT("Rejected Grid configure preserves Free state"), Assembly->GetPlacementMode() == EAssemblyPlacementMode::Free && Assembly->GetBoneDefinitions().Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPartMultiportTest, "JGD2026.Assembly.JointParts.MultiportConnectivity", JointPartTestFlags)
bool FAssemblyJointPartMultiportTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeJointPartTestAssembly();
	FText Reason;
	FGuid Left, Part, Right, Top, Bottom;
	if (!TestTrue(TEXT("Root bone touches the core"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, Left, Reason))) { return false; }
	if (!TestTrue(TEXT("Cross piece connects at its left slot without a muscle key"), Assembly->TryInstallJointPart(TEXT("Joint_Cross"), FVector2D(192, 0), 0, Part, Reason))) { return false; }
	TestTrue(TEXT("Right bone uses another slot"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(288, 0), 0, Right, Reason));
	const bool bTopInstalled = Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(192, 96), -90, Top, Reason);
	TestTrue(FString::Printf(TEXT("Top bone touches the upper Cross surface: %s"), *Reason.ToString()), bTopInstalled);
	const bool bBottomInstalled = Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(192, -96), 90, Bottom, Reason);
	TestTrue(FString::Printf(TEXT("Bottom bone touches the lower Cross surface: %s"), *Reason.ToString()), bBottomInstalled);
	TestEqual(TEXT("One independently installed joint piece is returned"), Assembly->GetJointParts().Num(), 1);
	TestEqual(TEXT("The common rigid array contains five bodies"), Assembly->GetBones().Num(), 5);
	TestEqual(TEXT("Core edge plus four piece edges exist"), Assembly->GetJoints().Num(), 5);
	TestTrue(TEXT("All four bones reach the core through the piece"), Assembly->IsStructureConnected());
	TestTrue(TEXT("Metrics, anchors and inventory validate together"), Assembly->ValidateAssembly().bValid);
	TSet<FVector2D> UsedPorts;
	FGuid ConfiguredEdge;
	for (const FAssemblyJointInstance& Joint : Assembly->GetJoints())
	{
		if (Joint.BoneA == Part) { UsedPorts.Add(Joint.LocalAnchorA); ConfiguredEdge = Joint.InstanceId; }
		else if (Joint.BoneB == Part) { UsedPorts.Add(Joint.LocalAnchorB); ConfiguredEdge = Joint.InstanceId; }
		else
		{
			TestTrue(TEXT("Legacy core-bone edge keeps all four choices"), Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::Hinge)
				&& Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::Fixed)
				&& Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::BreakableHinge)
				&& Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::BreakableFixed));
			continue;
		}
		TestTrue(TEXT("Every yellow Cross attachment defaults to fixed"), Joint.Kind == EAssemblyJointKind::Fixed);
	}
	TestEqual(TEXT("Four independent surface contacts use distinct anchors"), UsedPorts.Num(), 4);
	TestFalse(TEXT("Yellow piece does not allow hinge settings"), Assembly->IsJointKindAllowed(ConfiguredEdge, EAssemblyJointKind::Hinge));
	TestFalse(TEXT("Yellow piece rejects the active kind transaction"), Assembly->TrySetJointKind(ConfiguredEdge, EAssemblyJointKind::Hinge, Reason));
	TestTrue(TEXT("Rejected kind change preserves the original fixed edge"), Assembly->GetJoints().ContainsByPredicate([ConfiguredEdge](const FAssemblyJointInstance& J)
	{
		return J.InstanceId == ConfiguredEdge && J.Kind == EAssemblyJointKind::Fixed;
	}));
	TestTrue(TEXT("Yellow piece may select its breakable fixed variant"), Assembly->TrySetJointKind(ConfiguredEdge, EAssemblyJointKind::BreakableFixed, Reason));
	TestTrue(TEXT("An attached constraint may change limits"), Assembly->TrySetJointLimits(ConfiguredEdge, -20, 25, Reason));
	TestTrue(TEXT("A leaf may slide inside the connection tolerance"), Assembly->TryMoveFreeBone(Top, FVector2D(192, 100), -90, Reason));
	const TArray<FAssemblyJointInstance> Rebuilt = Assembly->GetJoints();
	const FAssemblyJointInstance* Kept = Rebuilt.FindByPredicate([ConfiguredEdge](const FAssemblyJointInstance& J) { return J.InstanceId == ConfiguredEdge; });
	TestTrue(TEXT("Rebuild preserves configured edge identity/type/limits"), Kept && Kept->Kind == EAssemblyJointKind::BreakableFixed && Kept->MinAngleDegrees == -20 && Kept->MaxAngleDegrees == 25);
	TestTrue(TEXT("Full connected structure starts Playing"), Assembly->TryStartPlaying(Reason));
	TestFalse(TEXT("Joint pieces cannot be repositioned while Playing"), Assembly->TryMoveJointPart(Part, FVector2D(300, 100), 12, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPartNearestPortTest, "JGD2026.Assembly.JointParts.SurfaceContactsAndDeterminism", JointPartTestFlags)
bool FAssemblyJointPartNearestPortTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeJointPartTestAssembly(EAssemblyJointPartShape::Bar);
	FText Reason;
	FGuid RootBone, Part, Closest, Nearby;
	TestTrue(TEXT("Install rooted bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, RootBone, Reason));
	TestTrue(TEXT("Install bar"), Assembly->TryInstallJointPart(TEXT("Joint_Bar"), FVector2D(192, 0), 0, Part, Reason));
	TestTrue(TEXT("Exact port contact"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(288, 0), 0, Closest, Reason));
	TestTrue(TEXT("Second close endpoint remains separate geometrically"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(292, 14), 0, Nearby, Reason));
	const TArray<FAssemblyJointInstance> Original = Assembly->GetJoints();
	int32 BarEdges = 0;
	bool bClosestSelected = false, bNearbySelected = false;
	for (const FAssemblyJointInstance& Joint : Original)
	{
		if (Joint.BoneA != Part && Joint.BoneB != Part) { continue; }
		++BarEdges;
		bClosestSelected |= Joint.BoneA == Closest || Joint.BoneB == Closest;
		bNearbySelected |= Joint.BoneA == Nearby || Joint.BoneB == Nearby;
	}
	TestEqual(TEXT("Bar connects only the two bones whose endpoints are collinear with its center"), BarEdges, 2);
	TestTrue(TEXT("The nearest endpoint occupies the right slot"), bClosestSelected);
	TestFalse(TEXT("An off-axis nearby bone does not obtain a connector edge"), bNearbySelected);
	TArray<FAssemblyBoneInstance> Reversed = Assembly->GetBones();
	for (int32 I = 0, K = Reversed.Num() - 1; I < K; ++I, --K) { Swap(Reversed[I], Reversed[K]); }
	const TArray<FAssemblyJointInstance> Stable = AssemblyFreeRules::BuildJoints(Assembly->GetFreeSettings(), Assembly->GetBoneDefinitions(), Reversed, Original);
	TestEqual(TEXT("Array reordering keeps the same edge count"), Stable.Num(), Original.Num());
	for (const FAssemblyJointInstance& Joint : Original)
	{
		TestTrue(TEXT("Persistent edge identity survives reordering"), Stable.ContainsByPredicate([&Joint](const FAssemblyJointInstance& J)
		{
			return J.InstanceId == Joint.InstanceId && J.BoneA == Joint.BoneA && J.BoneB == Joint.BoneB
				&& J.LocalAnchorA.Equals(Joint.LocalAnchorA) && J.LocalAnchorB.Equals(Joint.LocalAnchorB);
		}));
	}

	// Color policy is preserved through an ordinary-bone bridge; connectors cannot connect directly.
	FAssemblyBoneDefinition Ordinary;
	Ordinary.InitialQuantity = 8;
	FAssemblyBoneDefinition Round = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Round, 64.0f / 640.0f);
	FAssemblyBoneDefinition Bar = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar, 64.0f / 640.0f);
	TestTrue(TEXT("Configure mixed-color joint pieces"), Assembly->ConfigureFreeAssembly(FAssemblyFreeSettings(), { Ordinary, Round, Bar }, {}, Reason));
	FGuid RoundId, BarId, LeafId, BridgeId;
	TestTrue(TEXT("Mixed-color root bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, RootBone, Reason));
	TestTrue(TEXT("Purple Round attaches to the bone"), Assembly->TryInstallJointPart(TEXT("Joint_Round"), FVector2D(192, 0), 0, RoundId, Reason));
	TestTrue(TEXT("Ordinary bone bridges the two connector pieces"),Assembly->TryInstallFreeBone(TEXT("Bone"),FVector2D(288,0),0,BridgeId,Reason));
	TestTrue(TEXT("Yellow Bar attaches to the bridge endpoint"), Assembly->TryInstallJointPart(TEXT("Joint_Bar"), FVector2D(384, 0), 0, BarId, Reason));
	TestTrue(TEXT("Leaf bone attaches to Bar"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(480, 0), 0, LeafId, Reason));
	FGuid MixedEdge;
	for (const FAssemblyJointInstance& Joint : Assembly->GetJoints())
	{
		const bool bRoundEdge = Joint.BoneA == RoundId || Joint.BoneB == RoundId;
		if (bRoundEdge)
		{
			TestTrue(TEXT("Round supplies hinge activity at either attached edge"), Joint.Kind == EAssemblyJointKind::Hinge);
			TestFalse(TEXT("Round connection never permits a fixed variant"), Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::Fixed));
			TestFalse(TEXT("Round connection transaction rejects fixed"), Assembly->TrySetJointKind(Joint.InstanceId, EAssemblyJointKind::Fixed, Reason));
		}
		if ((Joint.BoneA == RoundId && Joint.BoneB == BridgeId) || (Joint.BoneA == BridgeId && Joint.BoneB == RoundId)) { MixedEdge = Joint.InstanceId; }
		if ((Joint.BoneA == BarId && Joint.BoneB == LeafId) || (Joint.BoneA == LeafId && Joint.BoneB == BarId))
		{
			TestTrue(TEXT("The other yellow Bar edge stays fixed"), Joint.Kind == EAssemblyJointKind::Fixed);
		}
	}
	TestTrue(TEXT("Mixed-color attachment produced an identified edge"), MixedEdge.IsValid());
	TestTrue(TEXT("Purple connection can select its breakable active variant"), Assembly->TrySetJointKind(MixedEdge, EAssemblyJointKind::BreakableHinge, Reason));
	TestTrue(TEXT("Mixed-color graph and permitted family validate"), Assembly->ValidateAssembly().bValid);
	for (int32 GuidOrder = 0; GuidOrder < 2; ++GuidOrder)
	{
		TArray<FAssemblyBoneInstance> Renamed = Assembly->GetBones();
		const FGuid TestRoundId(GuidOrder == 0 ? 1 : 100, 0, 0, 1);
		const FGuid TestBarId(GuidOrder == 0 ? 100 : 1, 0, 0, 2);
		for (FAssemblyBoneInstance& Instance : Renamed)
		{
			if (Instance.InstanceId == RoundId) { Instance.InstanceId = TestRoundId; }
			else if (Instance.InstanceId == BarId) { Instance.InstanceId = TestBarId; }
		}
		const TArray<FAssemblyJointInstance> Fresh = AssemblyFreeRules::BuildJoints(Assembly->GetFreeSettings(), Assembly->GetBoneDefinitions(), Renamed, {});
		TestTrue(TEXT("Round preserves activity policy regardless of relative persistent IDs"), Fresh.ContainsByPredicate([TestRoundId, BridgeId](const FAssemblyJointInstance& J)
		{
			return ((J.BoneA == TestRoundId && J.BoneB == BridgeId) || (J.BoneA == BridgeId && J.BoneB == TestRoundId))
				&& J.Kind == EAssemblyJointKind::Hinge;
		}));
		TestFalse(TEXT("Persistent ID ordering cannot introduce a forbidden connector-connector edge"),Fresh.ContainsByPredicate([TestRoundId,TestBarId](const FAssemblyJointInstance& J)
		{
			return (J.BoneA==TestRoundId && J.BoneB==TestBarId) || (J.BoneA==TestBarId && J.BoneB==TestRoundId);
		}));
	}
	const FAssemblyBoneDefinition FixedRound = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::FixedRound, 64.0f / 640.0f);
	TestTrue(TEXT("Configure active and fixed circular pieces together"), Assembly->ConfigureFreeAssembly(FAssemblyFreeSettings(), { Ordinary, Round, FixedRound }, {}, Reason));
	FGuid FixedRoundId;
	TestTrue(TEXT("Circular-color graph root bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, RootBone, Reason));
	TestTrue(TEXT("Active circular piece attaches to the root"), Assembly->TryInstallJointPart(TEXT("Joint_Round"), FVector2D(192, 0), 0, RoundId, Reason));
	TestTrue(TEXT("Ordinary bone bridges the circular pieces"),Assembly->TryInstallFreeBone(TEXT("Bone"),FVector2D(288,0),0,BridgeId,Reason));
	TestTrue(TEXT("Fixed circular piece attaches to the ordinary bridge"), Assembly->TryInstallJointPart(TEXT("Joint_FixedRound"), FVector2D(384, 0), 0, FixedRoundId, Reason));
	TestTrue(TEXT("Leaf bone uses a fixed circular piece slot"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(480, 0), 0, LeafId, Reason));
	FGuid CircularMixedEdge, CircularFixedEdge;
	for (const FAssemblyJointInstance& Joint : Assembly->GetJoints())
	{
		if ((Joint.BoneA == RoundId && Joint.BoneB == BridgeId) || (Joint.BoneA == BridgeId && Joint.BoneB == RoundId))
		{
			CircularMixedEdge = Joint.InstanceId;
			TestTrue(TEXT("Only the purple circular piece supplies activity on the mixed-color edge"), Joint.Kind == EAssemblyJointKind::Hinge);
			TestFalse(TEXT("The mixed-color edge still rejects fixed conversion"), Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::Fixed));
		}
		if ((Joint.BoneA == FixedRoundId && Joint.BoneB == LeafId) || (Joint.BoneA == LeafId && Joint.BoneB == FixedRoundId))
		{
			CircularFixedEdge = Joint.InstanceId;
			TestTrue(TEXT("The yellow circular piece's ordinary-bone edge stays fixed"), Joint.Kind == EAssemblyJointKind::Fixed);
			TestFalse(TEXT("The yellow circular edge rejects activity"), Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::Hinge));
			TestFalse(TEXT("The yellow circular edge rejects breakable activity"), Assembly->IsJointKindAllowed(Joint.InstanceId, EAssemblyJointKind::BreakableHinge));
		}
	}
	TestTrue(TEXT("Both circular attachment edges were generated"), CircularMixedEdge.IsValid() && CircularFixedEdge.IsValid());
	TestTrue(TEXT("The yellow circular edge accepts its fixed breakable variant"), Assembly->TrySetJointKind(CircularFixedEdge, EAssemblyJointKind::BreakableFixed, Reason));
	TestTrue(TEXT("Circular-color graph and inventory validate"), Assembly->ValidateAssembly().bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPartTransactionsTest, "JGD2026.Assembly.JointParts.InventoryMoveAndRemovalTransactions", JointPartTestFlags)
bool FAssemblyJointPartTransactionsTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeJointPartTestAssembly(EAssemblyJointPartShape::Bar);
	FText Reason;
	FGuid Bone, Part, Leaf, Invalid;
	TestTrue(TEXT("Install rooted bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, Bone, Reason));
	for (int32 I = 0; I < 20; ++I)
	{
		TestTrue(TEXT("Legal preview has no inventory reservation"), Assembly->ValidateJointPartPlacement(TEXT("Joint_Bar"), FVector2D(192, 0), 0, FGuid()).bValid);
	}
	TestEqual(TEXT("Canceled previews leave all pieces in stock"), Assembly->GetRemainingQuantity(TEXT("Joint_Bar")), 2);
	FAssemblyBoneDefinition JointDefinition;
	TestTrue(TEXT("Public definition lookup exposes joint size and ports"), Assembly->GetBoneDefinition(TEXT("Joint_Bar"), JointDefinition)
		&& JointDefinition.PartKind == EAssemblyRigidPartKind::JointPart && JointDefinition.JointShape == EAssemblyJointPartShape::Bar);
	TestFalse(TEXT("Unknown public lookup returns false"), Assembly->GetBoneDefinition(TEXT("MissingPart"), JointDefinition));
	TestFalse(TEXT("Part cannot overlap the core"), Assembly->TryInstallJointPart(TEXT("Joint_Bar"), FVector2D::ZeroVector, 0, Invalid, Reason));
	TestFalse(TEXT("Failed install returns no ID"), Invalid.IsValid());
	TestEqual(TEXT("Invalid release retains stock"), Assembly->GetRemainingQuantity(TEXT("Joint_Bar")), 2);
	TestTrue(TEXT("Install independent part"), Assembly->TryInstallJointPart(TEXT("Joint_Bar"), FVector2D(192, 0), 0, Part, Reason));
	TestEqual(TEXT("Commit consumes one piece"), Assembly->GetRemainingQuantity(TEXT("Joint_Bar")), 1);
	FAssemblyBoneInstance Before, After;
	Assembly->GetBone(Part, Before);
	TestFalse(TEXT("Invalid move through core fails"), Assembly->TryMoveJointPart(Part, FVector2D::ZeroVector, 37, Reason));
	TestTrue(TEXT("Failed move retains instance"), Assembly->GetBone(Part, After));
	TestTrue(TEXT("Failed move retains original pose"), Before.LocalVisualTransform.Equals(After.LocalVisualTransform));
	TestTrue(TEXT("Leaf attaches to the independent part"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(288, 0), 0, Leaf, Reason));
	TestFalse(TEXT("Removing the part cannot strand a rooted leaf"), Assembly->RemoveJointPart(Part, Reason));
	TestEqual(TEXT("Rejected removal returns no extra piece"), Assembly->GetRemainingQuantity(TEXT("Joint_Bar")), 1);
	TestFalse(TEXT("Joint removal wrapper cannot remove an ordinary bone"), Assembly->RemoveJointPart(Bone, Reason));
	TestFalse(TEXT("Joint install wrapper cannot consume ordinary-bone stock"), Assembly->TryInstallJointPart(TEXT("Bone"), FVector2D(400, 0), 0, Invalid, Reason));
	TestTrue(TEXT("Remove leaf first"), Assembly->TryRemoveInstance(Leaf, Reason));
	TestTrue(TEXT("Now remove the part"), Assembly->RemoveJointPart(Part, Reason));
	TestFalse(TEXT("Second removal cannot duplicate stock"), Assembly->RemoveJointPart(Part, Reason));
	TestEqual(TEXT("Exactly the initial piece stock is restored"), Assembly->GetRemainingQuantity(TEXT("Joint_Bar")), 2);
	TestTrue(TEXT("Restart retains the explicit round"), Assembly->RestartAssembly(Reason));
	TestTrue(TEXT("Restart clears part instances and constraint edges"), Assembly->GetJointParts().IsEmpty() && Assembly->GetJoints().IsEmpty());
	TestEqual(TEXT("Restart restores joint stock"), Assembly->GetRemainingQuantity(TEXT("Joint_Bar")), 2);
	TestEqual(TEXT("Explicit snapshot still has exactly two rigid definitions"), Assembly->GetBoneDefinitions().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPartMusclePolicyTest, "JGD2026.Assembly.JointParts.MusclesOnlyAttachToBones", JointPartTestFlags)
bool FAssemblyJointPartMusclePolicyTest::RunTest(const FString& Parameters)
{
	UAssemblyComponent* Assembly = MakeJointPartTestAssembly(EAssemblyJointPartShape::Bar);
	FText Reason;
	FGuid Bone, Part, Muscle;
	TestTrue(TEXT("Install rooted ordinary bone"), Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, Bone, Reason));
	TestTrue(TEXT("Install part without selecting WASD"), Assembly->TryInstallJointPart(TEXT("Joint_Bar"), FVector2D(192, 0), 0, Part, Reason));
	TestTrue(TEXT("Only the piece is recognized as a JointPart"), Assembly->IsJointPart(Part) && !Assembly->IsJointPart(Bone));
	TestTrue(TEXT("Select muscle W key"), Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestFalse(TEXT("A part cannot be used as either muscle endpoint"), Assembly->TryInstallMuscle(TEXT("Muscle"), JointPartEndpoint(Bone, FVector2D(10, 0)), JointPartEndpoint(Part, FVector2D::ZeroVector), Muscle, Reason));
	TestFalse(TEXT("Two distinct positions on the same part still cannot host a muscle"), Assembly->TryInstallMuscle(TEXT("Muscle"), JointPartEndpoint(Part, FVector2D(-10, 0)), JointPartEndpoint(Part, FVector2D(10, 0)), Muscle, Reason));
	TestEqual(TEXT("Rejected muscle installations retain inventory"), Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestTrue(TEXT("Same ordinary bone at different positions remains supported"), Assembly->TryInstallMuscle(TEXT("Muscle"), JointPartEndpoint(Bone, FVector2D(-10, 0)), JointPartEndpoint(Bone, FVector2D(10, 0)), Muscle, Reason));
	TestTrue(TEXT("Structure and stocks still validate"), Assembly->ValidateAssembly().bValid);
	return true;
}

#endif
