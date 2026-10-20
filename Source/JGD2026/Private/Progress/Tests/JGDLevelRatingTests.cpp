#if WITH_DEV_AUTOMATION_TESTS

#include "Progress/JGDLevelProgressSubsystem.h"
#include "Progress/JGDLevelRatingComponent.h"
#include "Progress/JGDLevelStar.h"
#include "Assembly/AssemblyComponent.h"
#include "../../Assembly/AssemblyTutorial.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace { constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDLevelRecordTest, "JGD2026.Rating.HighestRecordAndDisk", Flags)
bool FJGDLevelRecordTest::RunTest(const FString& Parameters)
{
	UJGDLevelProgressSave* Save = NewObject<UJGDLevelProgressSave>();
	TestEqual(TEXT("Unplayed level has no score"), Save->GetBest(TEXT("Level_0")), 0);
	TestTrue(TEXT("First completion records one"), Save->Improve(TEXT("/Game/Maps/Levels/Level_0.Level_0"), 1));
	TestEqual(TEXT("PIE and asset paths share a key"), Save->GetBest(TEXT("UEDPIE_12_Level_0")), 1);
	TestTrue(TEXT("Three stars improve the record"), Save->Improve(TEXT("Level_0"), 3));
	TestFalse(TEXT("Later two-star run cannot lower it"), Save->Improve(TEXT("Level_0"), 2));
	TestFalse(TEXT("Repeated completion cannot change it"), Save->Improve(TEXT("Level_0"), 3));
	TestTrue(TEXT("Separate levels have separate records"), Save->Improve(TEXT("Level_1"), 2));
	TestEqual(TEXT("First record stays three"), Save->GetBest(TEXT("Level_0")), 3);
	TestEqual(TEXT("Other level stays two"), Save->GetBest(TEXT("Level_1")), 2);
	const FString Slot = TEXT("JGD_RatingTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TestTrue(TEXT("Save to an isolated test profile"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
	UJGDLevelProgressSave* Reloaded = Cast<UJGDLevelProgressSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	TestNotNull(TEXT("Save class loads from disk"), Reloaded);
	if (Reloaded)
	{
		TestEqual(TEXT("Three stars persist after reload"), Reloaded->GetBest(TEXT("Level_0")), 3);
		TestEqual(TEXT("Other level persists independently"), Reloaded->GetBest(TEXT("Level_1")), 2);
	}
	TestTrue(TEXT("Remove only the test's own save"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDLevelStockTest, "JGD2026.Rating.TutorialInventory", Flags)
bool FJGDLevelStockTest::RunTest(const FString& Parameters)
{
	TArray<FAssemblyBoneDefinition> Bones = GetDefault<UAssemblyComponent>()->DefaultBones;
	Bones.Append(GetDefault<UAssemblyComponent>()->DefaultJointParts);
	TArray<FAssemblyMuscleDefinition> Muscles = GetDefault<UAssemblyComponent>()->DefaultMuscles;
	AssemblyTutorial::ConfigureInventory(1, Bones, Muscles);
	int32 BoneStock = 0, JointStock = 0, MuscleStock = 0;
	for (const auto& Part : Bones)
	{
		if (Part.PartKind == EAssemblyRigidPartKind::Bone) BoneStock += Part.InitialQuantity;
		else
		{
			JointStock += Part.InitialQuantity;
			if (Part.InitialQuantity) TestEqual(TEXT("Only purple movable joints supplied"), Part.JointShape, EAssemblyJointPartShape::Round);
		}
	}
	for (const auto& Muscle : Muscles) MuscleStock += Muscle.InitialQuantity;
	TestEqual(TEXT("Second level has four bones"), BoneStock, 4);
	TestEqual(TEXT("Second level has two movable joints"), JointStock, 2);
	TestEqual(TEXT("Second level has four muscles"), MuscleStock, 4);
	AssemblyTutorial::ConfigureInventory(0, Bones, Muscles);
	for (const auto& Part : Bones) TestEqual(TEXT("First level remains core-only"), Part.InitialQuantity, 0);
	for (const auto& Muscle : Muscles) TestEqual(TEXT("First level has no muscle stock"), Muscle.InitialQuantity, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDLevelPickupTest, "JGD2026.Rating.PickupResetAndCompletion", Flags)
bool FJGDLevelPickupTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AActor* Owner = World->SpawnActor<AActor>();
	USphereComponent* Core = NewObject<USphereComponent>(Owner);
	Owner->SetRootComponent(Core); Owner->AddInstanceComponent(Core); Core->RegisterComponent();
	UAssemblyComponent* Model = NewObject<UAssemblyComponent>(Owner);
	Model->bAutoEnterAssembly = false; Owner->AddInstanceComponent(Model); Model->RegisterComponent();
	UJGDLevelRatingComponent* Run = NewObject<UJGDLevelRatingComponent>(Owner);
	Owner->AddInstanceComponent(Run); Run->RegisterComponent();
	AJGDLevelStar* First = World->SpawnActor<AJGDLevelStar>();
	First->StarIndex = 0; First->SetActorLocation(FVector(200, 0, 0));
	AJGDLevelStar* Second = World->SpawnActor<AJGDLevelStar>();
	Second->StarIndex = 1; Second->SetActorLocation(FVector(600, 0, 0));
	FText Reason;
	TestTrue(TEXT("Enter core-only assembly"), Model->EnterAssembly(Reason));
	Run->InitializeRun(Model);
	Core->SetWorldLocation(First->GetActorLocation()); Run->UpdateCollection(Core);
	TestEqual(TEXT("Assembly cannot collect"), Run->GetCollectedStars(), 0);
	TestEqual(TEXT("Assembly cannot complete"), Run->CompleteRun().EarnedStars, 0);
	TestTrue(TEXT("Start play"), Model->TryStartPlaying(Reason));
	Run->UpdateCollection(Core); Run->UpdateCollection(Core);
	TestEqual(TEXT("Touching a star twice collects once"), Run->GetCollectedStars(), 1);
	TestTrue(TEXT("Collected art is hidden"), First->IsCollected());
	Core->SetWorldLocation(FVector(800, 0, 0)); Run->UpdateCollection(Core);
	TestEqual(TEXT("Swept core catches a crossed star"), Run->GetCollectedStars(), 2);
	TestEqual(TEXT("Completion adds base star"), Run->CompleteRun().EarnedStars, 3);
	TestEqual(TEXT("Repeated completion stays three"), Run->CompleteRun().EarnedStars, 3);
	Run->ResetRun();
	TestFalse(TEXT("Retry clears completion"), Run->IsRunComplete());
	TestEqual(TEXT("Retry clears pickups"), Run->GetCollectedStars(), 0);
	TestFalse(TEXT("Retry restores first star"), First->IsCollected());
	TestFalse(TEXT("Retry restores second star"), Second->IsCollected());
	TestEqual(TEXT("No pickup completion gives base one"), Run->CompleteRun().EarnedStars, 1);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
