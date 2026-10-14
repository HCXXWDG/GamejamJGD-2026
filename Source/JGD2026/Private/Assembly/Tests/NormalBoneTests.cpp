#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/NormalBone.h"
#include "Assembly/AssemblyComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PaperSpriteComponent.h"
#include <limits>

namespace
{
	constexpr EAutomationTestFlags BoneTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FBoneTestWorld
	{
		UWorld* World;
		UAssemblyComponent* Assembly;
		FBoneTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			AActor* Owner = World->SpawnActor<AActor>();
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->DefaultPlacementMode = EAssemblyPlacementMode::Grid;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
			FText Reason;
			Assembly->EnterAssembly(Reason);
		}
		~FBoneTestWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		ANormalBone* SpawnBone()
		{
			ANormalBone* Bone = World->SpawnActor<ANormalBone>();
			Bone->DispatchBeginPlay();
			return Bone;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalBoneTraitsTest, "JGD2026.NormalBone.DesignerTraits", BoneTestFlags)
bool FNormalBoneTraitsTest::RunTest(const FString& Parameters)
{
	const float Lengths[] = { 64.0f, 96.0f, 128.0f };
	const float Thicknesses[] = { 6.4f, 12.8f, 19.2f };
	TSet<FString> Names;
	FText Reason;
	for (int32 Length = 0; Length < 3; ++Length)
	{
		for (int32 Thickness = 0; Thickness < 3; ++Thickness)
		{
			for (int32 Density = 0; Density < 3; ++Density)
			{
				FNormalBoneParameters P;
				P.Length = static_cast<EBoneLengthTier>(Length);
				P.Thickness = static_cast<EBoneThicknessTier>(Thickness);
				P.Density = static_cast<EBoneDensityTier>(Density);
				FNormalBonePhysicalStats Stats;
				TestTrue(TEXT("All 27 designer trait combinations are valid"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
				TestEqual(TEXT("Designer length in world units"), Stats.Length, Lengths[Length]);
				TestTrue(TEXT("Designer thickness in world units"), FMath::IsNearlyEqual(Stats.Thickness, Thicknesses[Thickness]));
				const float ExpectedMass = (Lengths[Length] / 64.0f) * (Thicknesses[Thickness] / 64.0f) * (Density + 1);
				TestTrue(TEXT("Mass uses 2D area times surface density"), FMath::IsNearlyEqual(Stats.MassKg, ExpectedMass, 0.000001f));
				Names.Add(NormalBoneMetrics::Describe(P).ToString());
				const TArray<FIntPoint> Cells = ANormalBone::MakeRectangularBoneFootprint(P, 64.0f, Reason);
				TestEqual(TEXT("Shelf configuration builds the needed rectangular cells"), Cells.Num(), Length == 0 ? 1 : 2);
				TestEqual(TEXT("Short uses one column, other lengths use two"),
					NormalBoneMetrics::RequiredGridSize(Stats, 64.0f), FIntPoint(Length == 0 ? 1 : 2, 1));
			}
		}
	}
	TestEqual(TEXT("Every trait combination has a distinct name"), Names.Num(), 27);
	TestTrue(TEXT("Invalid grid size cannot allocate cells"), ANormalBone::MakeRectangularBoneFootprint(FNormalBoneParameters(), 0.0f, Reason).IsEmpty());
	TestTrue(TEXT("Extreme sizes cannot allocate unbounded memory"), ANormalBone::MakeRectangularBoneFootprint(FNormalBoneParameters(), 0.00001f, Reason).IsEmpty());
	FNormalBoneParameters P;
	P.UnitLength = 32.0f;
	P.StandardDensityKgPerSquareUnit = 5.0f;
	FNormalBonePhysicalStats Stats;
	TestTrue(TEXT("Physics programmer can tune both baselines"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
	TestEqual(TEXT("Changing design unit changes length"), Stats.Length, 64.0f);
	TestTrue(TEXT("Density baseline changes mass without using Y extrusion"), FMath::IsNearlyEqual(Stats.MassKg, 4.0f));
	P.UnitLength = 0.0f;
	TestFalse(TEXT("Zero unit rejected"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
	TestFalse(TEXT("Invalid evaluation clears old statistics"), Stats.bValid);
	P = FNormalBoneParameters();
	P.Density = static_cast<EBoneDensityTier>(255);
	TestFalse(TEXT("Invalid enum rejected"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
	P = FNormalBoneParameters();
	P.StandardDensityKgPerSquareUnit = -1.0f;
	TestFalse(TEXT("Negative density rejected"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
	P.UnitLength = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN rejected"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalBonePoseTest, "JGD2026.NormalBone.PoseAndContinuousEndpoints", BoneTestFlags)
bool FNormalBonePoseTest::RunTest(const FString& Parameters)
{
	FBoneTestWorld T;
	FText Reason;
	FGuid Id;
	TestTrue(TEXT("Install the model once"), T.Assembly->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, Id, Reason));
	USceneComponent* Frame = NewObject<USceneComponent>(T.Assembly->GetOwner());
	T.Assembly->GetOwner()->AddInstanceComponent(Frame);
	Frame->RegisterComponent();
	Frame->SetWorldTransform(FTransform(FQuat(FVector(0, 1, 0), 0.35), FVector(100, 0, 200), FVector(1.25, 1, 1.5)));
	T.Assembly->SetGridFrame(Frame);
	ANormalBone* Bone = T.SpawnBone();
	TestNotNull(TEXT("White model sprite is available"), Bone->GetBoneSprite()->GetSprite());
	if (!TestTrue(TEXT("Initialize installed bone"), Bone->InitializeBone(T.Assembly, Id, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Body is the actor root"), Bone->GetRootComponent() == Bone->GetBoneBody());
	TestFalse(TEXT("Bone does not start independent physics"), Bone->GetBoneBody()->IsSimulatingPhysics());
	TestTrue(TEXT("Pose source uses the body with the model pivot"), T.Assembly->GetBonePoseSource(Id) == Bone->GetBoneBody());
	TestTrue(TEXT("Physical width and thickness differ from allocated cells"),
		Bone->GetBoneBody()->GetUnscaledBoxExtent().Equals(FVector(64, 4, 6.4), 0.0001));
	const FBoxSphereBounds VisualBounds = Bone->GetBoneSprite()->CalcBounds(Bone->GetBoneSprite()->GetRelativeTransform());
	TestTrue(TEXT("Visible sprite is centered in XZ despite transparent padding"),
		FMath::IsNearlyZero(VisualBounds.Origin.X, 0.0001) && FMath::IsNearlyZero(VisualBounds.Origin.Z, 0.0001));
	TestTrue(TEXT("The sprite has a display-only depth separate from its physical body"),
		!FMath::IsNearlyZero(VisualBounds.Origin.Y) && FMath::IsNearlyZero(Bone->GetBoneBody()->GetComponentLocation().Y));
	TestTrue(TEXT("Visible sprite length agrees with the physical length"), FMath::IsNearlyEqual(VisualBounds.BoxExtent.X, 64.0, 0.0001));
	TestTrue(TEXT("Visible sprite thickness agrees with the physical thickness"), FMath::IsNearlyEqual(VisualBounds.BoxExtent.Z, 6.4, 0.0001));
	TestEqual(TEXT("Rendering did not deduct inventory again"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 3);
	for (int32 Turn = 0; Turn < 4; ++Turn)
	{
		TestTrue(TEXT("Rotate installed model"), T.Assembly->TryMoveBone(Id, FIntPoint(3, 2), Turn, Reason));
		const FAssemblyBoneInstance Data = Bone->GetBoneData();
		TestTrue(TEXT("View follows authoritative grid rotation and frame"),
			Bone->GetActorTransform().Equals(Data.LocalVisualTransform * Frame->GetComponentTransform(), 0.001));
		FVector EndWorld;
		TestTrue(TEXT("Joint endpoint follows every rotation"), Bone->GetEndWorldPosition(EBoneEnd::End, EndWorld));
		TestTrue(TEXT("Joint endpoint is the actual end, not the anchor cell center"),
			EndWorld.Equals(Bone->GetActorTransform().TransformPosition(FVector(64, 0, 0)), 0.001));
		FAssemblyMuscleEndpoint Endpoint;
		const FVector WorldPoint = Bone->GetActorTransform().TransformPosition(FVector(23.25, 0, 3.5));
		TestTrue(TEXT("Fractional bone-local points survive world rotation and scale"), Bone->TryGetMuscleEndpoint(WorldPoint, Endpoint));
		TestTrue(TEXT("Continuous endpoint is not quantized to a grid"), Endpoint.BoneLocalPoint.Equals(FVector2D(23.25, 3.5), 0.001));
	}
	FAssemblyMuscleEndpoint A, B, Invalid;
	TestTrue(TEXT("First point on a bone"), Bone->TryMakeMuscleEndpoint(FVector2D(-20.25, 2.5), A));
	TestTrue(TEXT("Second distinct point on the same bone"), Bone->TryMakeMuscleEndpoint(FVector2D(25.75, -3.5), B));
	TestFalse(TEXT("Outside actual thickness is rejected even inside the grid footprint"), Bone->TryMakeMuscleEndpoint(FVector2D(0, 10), Invalid));
	TestFalse(TEXT("Outside actual length is rejected"), Bone->TryMakeMuscleEndpoint(FVector2D(65, 0), Invalid));
	FGuid MuscleId;
	TestTrue(TEXT("Choose muscle key"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	TestTrue(TEXT("Two points on one bone can form a muscle"), T.Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, MuscleId, Reason));
	TestTrue(TEXT("Enter Playing"), T.Assembly->TryStartPlaying(Reason));
	const FTransform PhysicalPose(FQuat(FVector(0, 1, 0), 0.7), FVector(230, 0, 350));
	Bone->SetActorTransform(PhysicalPose);
	TestFalse(TEXT("Assembly refresh refuses to overwrite Playing physics"), Bone->RefreshAssemblyPose(Reason));
	TestTrue(TEXT("Physical pose stays intact"), Bone->GetActorTransform().Equals(PhysicalPose));
	FVector Connection;
	TestTrue(TEXT("Connection lookup reads the actual body pose"), Bone->GetConnectionWorldPosition(A.BoneLocalPoint, Connection));
	TestTrue(TEXT("Connection follows physics rather than the saved grid pose"),
		Connection.Equals(PhysicalPose.TransformPosition(FVector(A.BoneLocalPoint.X, 0, A.BoneLocalPoint.Y))));
	TestTrue(TEXT("Restart"), T.Assembly->RestartAssembly(Reason));
	TestTrue(TEXT("Restart destroys the bound actor"), Bone->IsActorBeingDestroyed());
	TestEqual(TEXT("Restart restores the model stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalBoneLifecycleTest, "JGD2026.NormalBone.BindingAndPhysicsOwnership", BoneTestFlags)
bool FNormalBoneLifecycleTest::RunTest(const FString& Parameters)
{
	FBoneTestWorld T;
	FText Reason;
	for (int32 Round = 0; Round < 3; ++Round)
	{
		FGuid Id;
		TestTrue(TEXT("Install for this round"), T.Assembly->TryInstallBone(TEXT("Bone"), FIntPoint(3, 2), 0, Id, Reason));
		ANormalBone* Bone = T.SpawnBone();
		const FTransform Before = Bone->GetActorTransform();
		TestFalse(TEXT("Unknown ID does not initialize a view"), Bone->InitializeBone(T.Assembly, FGuid::NewGuid(), Reason));
		TestTrue(TEXT("Failed initialization retains position"), Bone->GetActorTransform().Equals(Before));
		TestTrue(TEXT("Bind real ID"), Bone->InitializeBone(T.Assembly, Id, Reason));
		TestTrue(TEXT("Same binding can be repeated"), Bone->InitializeBone(T.Assembly, Id, Reason));
		TestFalse(TEXT("Changing instance without clearing is rejected"), Bone->InitializeBone(T.Assembly, FGuid::NewGuid(), Reason));
		TestEqual(TEXT("Failed rebind retains ID"), Bone->GetInstanceId(), Id);
		ANormalBone* Duplicate = T.SpawnBone();
		TestFalse(TEXT("Two actors cannot take the same model pose source"), Duplicate->InitializeBone(T.Assembly, Id, Reason));
		Duplicate->Destroy();
		TestTrue(TEXT("Destroying an unbound actor leaves the real registration intact"), T.Assembly->GetBonePoseSource(Id) == Bone->GetBoneBody());
		if (Round == 0)
		{
			Bone->GetBoneBody()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Bone->GetBoneBody()->SetSimulatePhysics(true);
			TestTrue(TEXT("Physics programmer can enable the supplied body"), Bone->GetBoneBody()->IsSimulatingPhysics());
			TestFalse(TEXT("Simulation also blocks Assembly teleports"), Bone->RefreshAssemblyPose(Reason));
			Bone->GetBoneBody()->SetSimulatePhysics(false);
		}
		if (Round == 1)
		{
			USceneComponent* Replacement = NewObject<USceneComponent>(T.Assembly->GetOwner());
			T.Assembly->GetOwner()->AddInstanceComponent(Replacement);
			Replacement->RegisterComponent();
			TestTrue(TEXT("Physics adapter may replace the pose source"), T.Assembly->RegisterBonePoseSource(Id, Replacement, Reason));
			Bone->ClearAssemblyBinding();
			TestTrue(TEXT("Clearing a view does not erase the adapter's replacement"), T.Assembly->GetBonePoseSource(Id) == Replacement);
			Bone->Destroy();
		}
		else
		{
			TestTrue(TEXT("Removing the model succeeds"), T.Assembly->TryRemoveInstance(Id, Reason));
			TestTrue(TEXT("Removal destroys its view"), Bone->IsActorBeingDestroyed());
		}
		TestTrue(TEXT("Reset between rounds"), T.Assembly->RestartAssembly(Reason));
		TestTrue(TEXT("Stock and installed-instance invariant"), T.Assembly->ValidateAssembly().bValid);
		TestEqual(TEXT("Exact stock after each round"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	}
	FAssemblyBoneDefinition InvalidDefinition = T.Assembly->DefaultBones[0];
	InvalidDefinition.BoneParameters.UnitLength = 0.0f;
	TestFalse(TEXT("Invalid traits rejected before replacing the assembly round"),
		T.Assembly->ConfigureAssembly(T.Assembly->GetGridSettings(), { InvalidDefinition }, {}, Reason));
	TestEqual(TEXT("Bad traits cannot consume stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	return true;
}

#endif
