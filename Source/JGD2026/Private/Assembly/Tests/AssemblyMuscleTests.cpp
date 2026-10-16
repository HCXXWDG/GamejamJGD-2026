#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PaperSpriteComponent.h"
#include <limits>

namespace
{
	constexpr EAutomationTestFlags ViewTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FAssemblyViewTestWorld
	{
		UWorld* World;
		UAssemblyComponent* Assembly;
		FAssemblyViewTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			AActor* Owner = World->SpawnActor<AActor>();
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
		}
		~FAssemblyViewTestWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyFreeViewTest, "JGD2026.AssemblyViews.FreeBoneAndMuscleCurve", ViewTestFlags)
bool FAssemblyFreeViewTest::RunTest(const FString& Parameters)
{
	FAssemblyViewTestWorld T;
	FText Reason;
	FAssemblyBoneDefinition Definition;
	Definition.TypeId = TEXT("LargeFreeBone");
	Definition.BoneParameters.UnitLength = 256.0f;
	Definition.Footprint.Reset();
	const FAssemblyFreeSettings Settings;
	if (!TestTrue(TEXT("Free mode accepts physical dimensions without a footprint"),
		T.Assembly->ConfigureFreeAssembly(Settings, { Definition }, { FAssemblyMuscleDefinition() }, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	ANormalBone* Bone = T.World->SpawnActor<ANormalBone>();
	Bone->DispatchBeginPlay();
	Bone->PreviewParameters = Definition.BoneParameters;
	Bone->SetActorTransform(FTransform(FQuat(FVector(0, 1, 0), 0.3), FVector(20, 0, 60)));
	Bone->OnConstruction(Bone->GetActorTransform());
	FVector PreviewEnd;
	TestTrue(TEXT("An unbound shelf preview exposes its physical endpoint"), Bone->GetEndWorldPosition(EBoneEnd::End, PreviewEnd));
	TestTrue(TEXT("Unbound endpoint uses the preview body pose"),
		PreviewEnd.Equals(Bone->GetActorTransform().TransformPosition(FVector(256, 0, 0)), 0.001));

	const double Angle = FMath::DegreesToRadians(37.0);
	const FVector2D Center(FMath::Cos(Angle) * 288.0, -FMath::Sin(Angle) * 288.0);
	FGuid BoneId;
	if (!TestTrue(TEXT("Install an arbitrary free angle outside a 6x6 grid"),
		T.Assembly->TryInstallFreeBone(Definition.TypeId, Center, 37.0f, BoneId, Reason))
		|| !TestTrue(TEXT("View binds using physical dimensions rather than footprint bounds"),
			Bone->InitializeBone(T.Assembly, BoneId, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Large free dimensions are fitted onto the body"),
		Bone->GetBoneBody()->GetUnscaledBoxExtent().Equals(FVector(256, 4, 25.6), 0.001));
	FAssemblyMuscleEndpoint A, B, Invalid;
	TestTrue(TEXT("Free endpoints support continuous points beyond legacy footprint bounds"),
		Bone->TryMakeMuscleEndpoint(FVector2D(-96.25, 3.75), A));
	TestTrue(TEXT("Two distinct points on the same physical bone are supported"),
		Bone->TryMakeMuscleEndpoint(FVector2D(230.125, -20.25), B));
	TestFalse(TEXT("Continuous endpoint must remain inside actual bone thickness"),
		Bone->TryMakeMuscleEndpoint(FVector2D(0, 26), Invalid));
	FGuid MuscleId;
	if (!TestTrue(TEXT("Select the muscle input key"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason))
		|| !TestTrue(TEXT("Install muscle at two continuous bone positions"),
			T.Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, MuscleId, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	AAssemblyMuscle* Muscle = T.World->SpawnActor<AAssemblyMuscle>();
	Muscle->DispatchBeginPlay();
	TestFalse(TEXT("A view cannot bind a non-existent muscle"), Muscle->InitializeMuscle(T.Assembly, FGuid::NewGuid(), Reason));
	if (!TestTrue(TEXT("Initialize the installed curve view"), Muscle->InitializeMuscle(T.Assembly, MuscleId, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Repeating the same initialization is safe"), Muscle->InitializeMuscle(T.Assembly, MuscleId, Reason));
	TestEqual(TEXT("Rendering does not consume muscle inventory"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 2);
	for (int32 Refresh = 0; Refresh < 5; ++Refresh)
	{
		Muscle->RefreshCurve();
	}
	TArray<UPaperSpriteComponent*> Segments;
	Muscle->GetComponents(Segments);
	TestEqual(TEXT("Refresh does not duplicate sprite segments"), Segments.Num(), Muscle->CurveSegments);
	FVector WorldA, WorldB;
	TestTrue(TEXT("Bound curve endpoints can be read"), Muscle->GetCurveEndpoints(WorldA, WorldB));
	USplineComponent* Spline = Muscle->GetCurveSpline();
	TestEqual(TEXT("A bent curve has two ends and one midpoint"), Spline->GetNumberOfSplinePoints(), 3);
	TestTrue(TEXT("Curve starts exactly at the first physical anchor"),
		Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(WorldA, 0.001));
	TestTrue(TEXT("Curve ends exactly at the second physical anchor"),
		Spline->GetLocationAtSplinePoint(2, ESplineCoordinateSpace::World).Equals(WorldB, 0.001));
	TestFalse(TEXT("Configured curve bends instead of being a straight line"),
		Spline->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World).Equals((WorldA + WorldB) * 0.5, 0.001));
	if (!TestTrue(TEXT("Core-connected bone can enter Playing"), T.Assembly->TryStartPlaying(Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	const FTransform BeforeDrive = Bone->GetActorTransform();
	TestTrue(TEXT("W requests contraction"), T.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("Drive intent is visible in the curve data"), Muscle->GetMuscleData().bContracting);
	TestTrue(TEXT("Contraction feedback has its configured colour"), Segments[0]->GetSpriteColor().Equals(Muscle->ContractingColor));
	TestTrue(TEXT("The curve does not fake bone movement"), Bone->GetActorTransform().Equals(BeforeDrive));
	const FTransform PhysicalPose(FQuat(FVector(0, 1, 0), 0.7), FVector(300, 0, 180));
	Bone->SetActorTransform(PhysicalPose);
	Muscle->Tick(0.0f);
	TestTrue(TEXT("Curve follows the changed physical body pose"), Muscle->GetCurveEndpoints(WorldA, WorldB));
	TestTrue(TEXT("Endpoint follows the registered body rather than saved placement"),
		WorldB.Equals(PhysicalPose.TransformPosition(FVector(B.BoneLocalPoint.X, 0, B.BoneLocalPoint.Y)), 0.001));
	TestTrue(TEXT("Spline is refreshed after physics"),
		Spline->GetLocationAtSplinePoint(2, ESplineCoordinateSpace::World).Equals(WorldB, 0.001));
	TestTrue(TEXT("Release requests relaxation"), T.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, false));
	TestFalse(TEXT("Released drive intent is visible in the curve data"), Muscle->GetMuscleData().bContracting);
	TestTrue(TEXT("Released curve returns to its configured colour"), Segments[0]->GetSpriteColor().Equals(Muscle->RelaxedColor));
	TestTrue(TEXT("Restart the assembly"), T.Assembly->RestartAssembly(Reason));
	TestTrue(TEXT("Restart destroys the muscle view"), Muscle->IsActorBeingDestroyed());
	TestTrue(TEXT("Restart destroys the bone view"), Bone->IsActorBeingDestroyed());
	TestEqual(TEXT("Restart restores initial muscle stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyMusclePreviewTest, "JGD2026.AssemblyViews.MusclePreviewIntegrity", ViewTestFlags)
bool FAssemblyMusclePreviewTest::RunTest(const FString& Parameters)
{
	FAssemblyViewTestWorld T;
	AAssemblyMuscle* Muscle = T.World->SpawnActor<AAssemblyMuscle>();
	FText Reason;
	const FVector A(-64, 0, -20), B(160, 0, 90);
	TestTrue(TEXT("Preview uses two continuous world coordinates"), Muscle->SetPreviewEndpoints(A, B, Reason));
	TestFalse(TEXT("A coincident preview is rejected"), Muscle->SetPreviewEndpoints(A, A, Reason));
	TestFalse(TEXT("NaN preview is rejected"), Muscle->SetPreviewEndpoints(
		FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), B, Reason));
	FVector ReadA, ReadB;
	TestTrue(TEXT("Rejected updates retain a usable preview"), Muscle->GetCurveEndpoints(ReadA, ReadB));
	TestTrue(TEXT("Rejected updates do not corrupt the first anchor"), ReadA.Equals(A));
	TestTrue(TEXT("Rejected updates do not corrupt the second anchor"), ReadB.Equals(B));
	Muscle->SetPreviewValid(false);
	TArray<UPaperSpriteComponent*> Segments;
	Muscle->GetComponents(Segments);
	TestTrue(TEXT("Curve creates visible sprite feedback"), !Segments.IsEmpty());
	if (!Segments.IsEmpty())
	{
		TestTrue(TEXT("Invalid candidate uses the configured invalid colour"),
			Segments[0]->GetSpriteColor().Equals(Muscle->InvalidPreviewColor));
		Muscle->SetPreviewValid(true);
		TestTrue(TEXT("Valid candidate uses the configured valid colour"),
			Segments[0]->GetSpriteColor().Equals(Muscle->ValidPreviewColor));
	}
	Muscle->CurveSprite = nullptr;
	Muscle->RefreshCurve();
	TestEqual(TEXT("Missing art never leaves a stale spline"), Muscle->GetCurveSpline()->GetNumberOfSplinePoints(), 0);
	for (const UPaperSpriteComponent* Segment : Segments)
	{
		TestFalse(TEXT("Missing art hides old curve fragments"), Segment->IsVisible());
	}
	return true;
}

#endif
