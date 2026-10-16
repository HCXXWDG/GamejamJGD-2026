#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyJointPart.h"
#include "Assembly/NormalBone.h"
#include "../AssemblyRenderLayers.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PaperSpriteComponent.h"

namespace
{
	constexpr EAutomationTestFlags ReferenceLayerTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FReferenceLayerTestWorld
	{
		UWorld* World = nullptr;
		UAssemblyComponent* Assembly = nullptr;
		FText SetupReason;
		bool bReady = false;

		FReferenceLayerTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			AActor* Owner = World->SpawnActor<AActor>();
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
			FAssemblyFreeSettings Settings;
			Settings.bEnableSurfaceSnapping = false;
			bReady = Assembly->ConfigureFreeAssembly(Settings, { FAssemblyBoneDefinition() }, {}, SetupReason);
		}

		~FReferenceLayerTestWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceCategoryLayersTest,
	"JGD2026.AssemblyViews.ReferenceLayers.LargeCategoryRanges", ReferenceLayerTestFlags)
bool FAssemblyReferenceCategoryLayersTest::RunTest(const FString& Parameters)
{
	const AssemblyRenderLayers::FCounts Counts { 10000, 1500, 9000 };
	const double BackBias = -1000.0;
	const double FrontBias = 1000.0;
	const double CoreFront = AssemblyRenderLayers::CoreDepth(Counts, FrontBias);
	const double JointBack = AssemblyRenderLayers::RigidDepth(Counts, 0, true, false, BackBias);
	const double JointFront = AssemblyRenderLayers::RigidDepth(Counts, Counts.NumRigid - 1, true, false, FrontBias);
	const double CapBack = AssemblyRenderLayers::OverlayDepth(Counts, 0, false, BackBias);
	const double CapFront = AssemblyRenderLayers::OverlayDepth(Counts, Counts.NumJoints - 1, false, FrontBias);
	const double BoneBack = AssemblyRenderLayers::RigidDepth(Counts, 0, false, false, BackBias);
	const double BoneFront = AssemblyRenderLayers::RigidDepth(Counts, Counts.NumRigid - 1, false, false, FrontBias);
	const double MuscleBack = AssemblyRenderLayers::MuscleDepth(Counts, 0, 0, false, BackBias);
	const double MuscleFront = AssemblyRenderLayers::MuscleDepth(Counts, Counts.NumMuscles - 1,
		AssemblyRenderLayers::MaxMuscleSlices - 1, false, FrontBias);
	const double MarkerBack = AssemblyRenderLayers::OverlayDepth(Counts, 0, true, BackBias);
	const double MarkerFront = AssemblyRenderLayers::OverlayDepth(Counts, Counts.NumJoints - 1, true, FrontBias);
	const double PinBack = AssemblyRenderLayers::ConnectionPinDepth(Counts, 0, BackBias);
	const double PinFront = AssemblyRenderLayers::ConnectionPinDepth(Counts, Counts.NumJoints - 1, FrontBias);
	const double BonePreview = AssemblyRenderLayers::RigidDepth(Counts, 0, false, true, BackBias);
	const double JointPreview = AssemblyRenderLayers::RigidDepth(Counts, 0, true, true, BackBias);
	const double MusclePreview = AssemblyRenderLayers::MuscleDepth(Counts, 0, 0, true, BackBias);
	TestTrue(TEXT("Core remains behind all joint parts even with legacy bias values"), CoreFront < JointBack);
	TestTrue(TEXT("Independent parts and legacy caps have separate slots"), JointFront < CapBack);
	TestTrue(TEXT("Every bone covers every joint and cap despite large instance counts"), CapFront < BoneBack);
	TestTrue(TEXT("Every muscle slice covers every bone"), BoneFront < MuscleBack);
	TestTrue(TEXT("All markers remain in front of all installed muscles"), MuscleFront < MarkerBack);
	TestTrue(TEXT("Connection pins also cover installed muscles"), MuscleFront < PinBack);
	TestTrue(TEXT("A generic pin and a distinct breakable badge never share a depth slot"), PinFront
		< AssemblyRenderLayers::OverlayDepth(Counts, Counts.NumJoints - 1, true, BackBias));
	TestTrue(TEXT("The next connection pin remains in front of the previous badge"),
		AssemblyRenderLayers::OverlayDepth(Counts, 0, true, FrontBias)
		< AssemblyRenderLayers::ConnectionPinDepth(Counts, 1, BackBias));
	TestTrue(TEXT("A dragged bone or joint is in front of the complete installed structure"),
		MarkerFront < BonePreview && MarkerFront < JointPreview);
	TestTrue(TEXT("A dragged muscle is also in front of installed markers"), MarkerFront < MusclePreview);
	TestTrue(TEXT("Old positive and negative offsets stay inside their own slot"),
		AssemblyRenderLayers::CoreDepth(Counts, FrontBias) - AssemblyRenderLayers::CoreDepth(Counts, BackBias)
		< AssemblyRenderLayers::DepthStep);
	const double LastSlice = AssemblyRenderLayers::MuscleDepth(Counts, 400,
		AssemblyRenderLayers::MaxMuscleSlices - 1, false, FrontBias);
	const double NextMuscle = AssemblyRenderLayers::MuscleDepth(Counts, 401, 0, false, BackBias);
	TestTrue(TEXT("Adjacent muscle instances cannot interleave their slices"), LastSlice < NextMuscle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceGuidRankTest,
	"JGD2026.AssemblyViews.ReferenceLayers.GuidRanksAndMembership", ReferenceLayerTestFlags)
bool FAssemblyReferenceGuidRankTest::RunTest(const FString& Parameters)
{
	FAssemblyBoneInstance Low, Target, High;
	Low.InstanceId = FGuid(1, 0, 0, 0);
	Target.InstanceId = FGuid(2, 0, 0, 0);
	High.InstanceId = FGuid(3, 0, 0, 0);
	TArray<FAssemblyBoneInstance> Instances { High, Target };
	TestEqual(TEXT("Rank uses GUID rather than insertion order"), AssemblyRenderLayers::GetRank(Instances, Target.InstanceId), 0);
	Instances.Insert(Low, 1);
	TestEqual(TEXT("Adding an earlier GUID updates an existing instance rank"), AssemblyRenderLayers::GetRank(Instances, Target.InstanceId), 1);
	Instances.RemoveAt(1);
	TestEqual(TEXT("Removing that GUID restores the original rank"), AssemblyRenderLayers::GetRank(Instances, Target.InstanceId), 0);

	FReferenceLayerTestWorld T;
	if (!TestTrue(TEXT("Initialize the free placement model"), T.bReady))
	{
		AddError(T.SetupReason.ToString());
		return false;
	}
	FText Reason;
	FGuid FirstId, SecondId;
	if (!TestTrue(TEXT("Install an isolated first bone"), T.Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(300, 0), 0, FirstId, Reason))) return false;
	ANormalBone* Bone = T.World->SpawnActor<ANormalBone>();
	if (!TestTrue(TEXT("Bind the visible first bone"), Bone->InitializeBone(T.Assembly, FirstId, Reason))) return false;
	const FTransform PhysicalPose = Bone->GetBoneBody()->GetComponentTransform();
	const double OriginalDepth = Bone->GetBoneSprite()->GetRelativeLocation().Y;
	if (!TestTrue(TEXT("Install a second isolated bone"), T.Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(600, 0), 0, SecondId, Reason))) return false;
	const AssemblyRenderLayers::FCounts Counts { T.Assembly->GetBones().Num(), 0, T.Assembly->GetJoints().Num() };
	const double ExpectedDepth = AssemblyRenderLayers::RigidDepth(Counts,
		AssemblyRenderLayers::GetRank(T.Assembly->GetBones(), FirstId), false, false, Bone->VisualDepthOffset);
	TestTrue(TEXT("An unchanged pose still refreshes its depth after insertion"),
		FMath::IsNearlyEqual(Bone->GetBoneSprite()->GetRelativeLocation().Y, ExpectedDepth, 0.000001)
		&& Bone->GetBoneSprite()->GetRelativeLocation().Y > OriginalDepth);
	TestTrue(TEXT("Depth reassignment never moves the actual body"), Bone->GetBoneBody()->GetComponentTransform().Equals(PhysicalPose));
	FActorSpawnParameters PreviewParams;
	PreviewParams.Owner = T.Assembly->GetOwner();
	ANormalBone* Preview = T.World->SpawnActor<ANormalBone>(ANormalBone::StaticClass(), FTransform::Identity, PreviewParams);
	TestTrue(TEXT("An unbound shelf preview uses its owner's complete assembly depth ranges"),
		Preview->GetBoneSprite()->GetRelativeLocation().Y > Bone->GetBoneSprite()->GetRelativeLocation().Y
		&& FMath::IsNearlyEqual(Preview->GetBoneSprite()->GetRelativeLocation().Y,
			AssemblyRenderLayers::RigidDepth(Counts, 0, false, true, Preview->VisualDepthOffset), 0.000001));
	TestTrue(TEXT("Remove the isolated extra piece"), T.Assembly->TryRemoveInstance(SecondId, Reason));
	TestTrue(TEXT("An unchanged pose refreshes its depth after removal too"),
		FMath::IsNearlyEqual(Bone->GetBoneSprite()->GetRelativeLocation().Y, OriginalDepth, 0.000001));
	TestTrue(TEXT("Removal depth reassignment preserves the body's transform"), Bone->GetBoneBody()->GetComponentTransform().Equals(PhysicalPose));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceArtworkRimTest,
	"JGD2026.AssemblyViews.ReferenceLayers.ArtworkRimKeepsPhysics", ReferenceLayerTestFlags)
bool FAssemblyReferenceArtworkRimTest::RunTest(const FString& Parameters)
{
	FReferenceLayerTestWorld T;
	if (!TestTrue(TEXT("Initialize the free placement model"), T.bReady)) return false;
	FText Reason;
	FGuid BoneId;
	if (!TestTrue(TEXT("Install a core-tangent bone"), T.Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96, 0), 0, BoneId, Reason))) return false;
	ANormalBone* Bone = T.World->SpawnActor<ANormalBone>();
	Bone->bUseArtworkProportions = true;
	if (!TestTrue(TEXT("Bind the artwork view"), Bone->InitializeBone(T.Assembly, BoneId, Reason))) return false;
	const FVector OriginalScale = Bone->GetBoneSprite()->GetRelativeScale3D();
	const FVector PhysicalExtent = Bone->GetBoneBody()->GetUnscaledBoxExtent();
	FVector AnchorBefore;
	if (!TestTrue(TEXT("Read the actual bone endpoint"), Bone->GetEndWorldPosition(EBoneEnd::End, AnchorBefore))) return false;
	Bone->ArtworkOverlapScale = 1.12f;
	T.Assembly->OnAssemblyChanged.Broadcast();
	const FVector RimScale = Bone->GetBoneSprite()->GetRelativeScale3D();
	TestTrue(TEXT("The seam-covering rim uniformly grows artwork in X and Z"),
		FMath::IsNearlyEqual(RimScale.X, OriginalScale.X * 1.12, 0.000001)
		&& FMath::IsNearlyEqual(RimScale.Z, OriginalScale.Z * 1.12, 0.000001)
		&& FMath::IsNearlyEqual(RimScale.Y, OriginalScale.Y, 0.000001));
	TestTrue(TEXT("Visual seam coverage leaves physical dimensions untouched"), Bone->GetBoneBody()->GetUnscaledBoxExtent().Equals(PhysicalExtent));
	FVector AnchorAfter;
	TestTrue(TEXT("Visual seam coverage leaves the endpoint on the original body"),
		Bone->GetEndWorldPosition(EBoneEnd::End, AnchorAfter) && AnchorAfter.Equals(AnchorBefore));
	TestTrue(TEXT("No visual change consumes inventory"), T.Assembly->GetRemainingQuantity(TEXT("Bone")) == 3);
	TestTrue(TEXT("The actual non-overlapping structure remains valid"), T.Assembly->ValidateAssembly().bValid);

	AAssemblyJointPart* Joint = T.World->SpawnActor<AAssemblyJointPart>();
	Joint->bUseArtworkProportions = true;
	Joint->OnConstruction(Joint->GetActorTransform());
	const FVector JointScale = Joint->GetBoneSprite()->GetRelativeScale3D();
	Joint->ArtworkOverlapScale = 2.0f;
	Joint->OnConstruction(Joint->GetActorTransform());
	TestTrue(TEXT("Joint artwork is excluded from the bone-only rim"), Joint->GetBoneSprite()->GetRelativeScale3D().Equals(JointScale, 0.000001));

	if (!TestTrue(TEXT("Enter Playing with a core-connected structure"), T.Assembly->TryStartPlaying(Reason))) return false;
	const FTransform SimulatedPose(FQuat(FVector::YAxisVector, 0.7), FVector(210, 0, 240));
	Bone->SetActorTransform(SimulatedPose);
	// A sentinel extent makes any accidental SetBoxExtent from a visual refresh observable.
	const FVector PhysicsOwnedExtent(51, 6, 9);
	Bone->GetBoneBody()->SetBoxExtent(PhysicsOwnedExtent, false);
	Bone->ArtworkOverlapScale = 1.24f;
	T.Assembly->OnAssemblyChanged.Broadcast();
	Bone->OnConstruction(Bone->GetActorTransform());
	TestTrue(TEXT("Playing display refresh does not overwrite a physics-owned pose"), Bone->GetActorTransform().Equals(SimulatedPose));
	TestTrue(TEXT("Playing display refresh does not write the body's shape"), Bone->GetBoneBody()->GetUnscaledBoxExtent().Equals(PhysicsOwnedExtent));
	TestTrue(TEXT("Playing can refresh the sprite rim independently"),
		FMath::IsNearlyEqual(Bone->GetBoneSprite()->GetRelativeScale3D().X, OriginalScale.X * 1.24, 0.000001));
	return true;
}

#endif
