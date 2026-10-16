#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

namespace AssemblyMuscleSideWorkbenchTests
{
	struct FTestWorld
	{
		UWorld* World;
		UAssemblyComponent* Assembly;
		UAssemblyInteractionComponent* Interaction;
		UAssemblyWorkbenchComponent* Workbench;
		FAssemblyBoneDefinition Definition;
		bool bReady = false;
		FText Reason;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			AActor* Owner = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Owner);
			Owner->AddInstanceComponent(Root);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
			Definition.Footprint.Reset();
			FAssemblyFreeSettings Settings;
			Settings.bEnableSurfaceSnapping = false;
			bReady = Assembly->ConfigureFreeAssembly(Settings, {Definition}, {FAssemblyMuscleDefinition()}, Reason);
			Interaction = NewObject<UAssemblyInteractionComponent>(Owner);
			Interaction->bAutoInitialize = false;
			Interaction->bAutoCreatePhysicsBridge = false;
			Owner->AddInstanceComponent(Interaction);
			Interaction->RegisterComponent();
			bReady = bReady && Interaction->InitializeInteraction(Assembly, Reason);
			APlayerController* Player = World->SpawnActor<APlayerController>();
			Workbench = NewObject<UAssemblyWorkbenchComponent>(Owner);
			Owner->AddInstanceComponent(Workbench);
			Workbench->RegisterComponent();
			bReady = bReady && Workbench->InitializeWorkbench(Assembly, Interaction, Player, Reason);
		}

		~FTestWorld()
		{
			Workbench->ShutdownWorkbench();
			Interaction->ClearInteraction();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		bool InstallBone(FVector2D Position, float Rotation, FGuid& OutId)
		{
			return Interaction->BeginBoneDrag(Definition.TypeId, FGuid(), Reason)
				&& Interaction->UpdateBoneDragAtLocalPosition(Position, Reason)
				&& Interaction->RotateBoneDrag(Rotation, Reason)
				&& Interaction->CommitBoneDrag(OutId, Reason);
		}

		bool PickSide(FGuid BoneId, double Side, FAssemblyMuscleEndpoint& OutEndpoint)
		{
			ANormalBone* Bone = Interaction->GetBoneActor(BoneId);
			if (!IsValid(Bone)) { return false; }
			// Pick just beyond the physical side, as when clicking the visible artwork rim.
			const double HalfThickness = Bone->GetPhysicalStats().Thickness * 0.5;
			const FVector Pointer = Bone->GetBoneBody()->GetComponentTransform().TransformPosition(
				FVector(20.25, 0.0, Side * (HalfThickness + 2.0)));
			return Assembly->FindMuscleEndpointAtWorldPosition(Pointer, OutEndpoint,
				Workbench->MuscleEndpointSnapDistance);
		}

		int32 CountMuscleViews() const
		{
			int32 Count = 0;
			for (TActorIterator<AAssemblyMuscle> It(World); It; ++It)
			{
				if (IsValid(*It) && !It->IsActorBeingDestroyed()) { ++Count; }
			}
			return Count;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyMuscleSideWorkbenchSelectionTest,
	"JGD2026.AssemblyWorkbench.MuscleSides.IndependentSelectionAndCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAssemblyMuscleSideWorkbenchSelectionTest::RunTest(const FString& Parameters)
{
	using namespace AssemblyMuscleSideWorkbenchTests;
	FTestWorld T;
	if (!TestTrue(TEXT("Initialize the public workbench and presenter"), T.bReady))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	FGuid BoneA, BoneB;
	if (!TestTrue(TEXT("Install the first bone"), T.InstallBone(FVector2D(160, 0), 0, BoneA))
		|| !TestTrue(TEXT("Install a differently angled bone"), T.InstallBone(FVector2D(224, -96), 90, BoneB)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	FAssemblyMuscleEndpoint InnerA, InnerB, OuterA, OuterB;
	if (!TestTrue(TEXT("Pick both inner attachment surfaces"), T.PickSide(BoneA, -1, InnerA) && T.PickSide(BoneB, -1, InnerB))
		|| !TestTrue(TEXT("Pick both outer attachment surfaces"), T.PickSide(BoneA, 1, OuterA) && T.PickSide(BoneB, 1, OuterB)))
	{
		return false;
	}
	FGuid InnerId, OuterId;
	if (!TestTrue(TEXT("Choose the inner muscle key"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, T.Reason))
		|| !TestTrue(TEXT("Install the inner muscle"), T.Interaction->InstallMuscleAtEndpoints(TEXT("Muscle"), InnerA, InnerB, InnerId, T.Reason))
		|| !TestTrue(TEXT("Choose a different outer muscle key"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::D, T.Reason))
		|| !TestTrue(TEXT("Install an independent outer muscle on the same bone pair"),
			T.Interaction->InstallMuscleAtEndpoints(TEXT("Muscle"), OuterA, OuterB, OuterId, T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	AAssemblyMuscle* InnerView = T.Interaction->GetMuscleActor(InnerId);
	AAssemblyMuscle* OuterView = T.Interaction->GetMuscleActor(OuterId);
	if (!TestNotNull(TEXT("Inner muscle has a view"), InnerView)
		|| !TestNotNull(TEXT("Outer muscle has its own view"), OuterView)) { return false; }
	TestTrue(TEXT("Inner and outer muscles keep separate instance identities"), InnerId != OuterId && InnerView != OuterView);
	TestEqual(TEXT("Only the two installed muscle views exist"), T.CountMuscleViews(), 2);
	TestEqual(TEXT("Two muscles consume two inventory items"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 1);
	FVector InnerWorldA, InnerWorldB, OuterWorldA, OuterWorldB;
	TestTrue(TEXT("Both installed curves expose their real attachment points"),
		InnerView->GetCurveEndpoints(InnerWorldA, InnerWorldB) && OuterView->GetCurveEndpoints(OuterWorldA, OuterWorldB));
	TestFalse(TEXT("The two sides do not collapse to the same first world anchor"), InnerWorldA.Equals(OuterWorldA, 0.001));
	TestFalse(TEXT("The two sides do not collapse to the same second world anchor"), InnerWorldB.Equals(OuterWorldB, 0.001));

	for (int32 Round = 0; Round < 3; ++Round)
	{
		if (!TestTrue(TEXT("Select only the inner muscle"), T.Workbench->SelectInstance(InnerId, EAssemblySelectionKind::Muscle, T.Reason))
			|| !TestTrue(TEXT("Begin moving the selected inner muscle"), T.Workbench->BeginMoveSelected(T.Reason)))
		{
			AddError(T.Reason.ToString());
			return false;
		}
		TestTrue(TEXT("Only the moving inner muscle is hidden"), InnerView->IsHidden() && !OuterView->IsHidden());
		FGuid RejectedId;
		TestFalse(TEXT("A move without a fresh pointer cannot commit stale anchors"), T.Workbench->CommitTool(RejectedId, T.Reason));
		TestFalse(TEXT("Rejected move returns no replacement identity"), RejectedId.IsValid());
		T.Workbench->CancelTool();
		TestFalse(TEXT("Cancel restores the inner muscle view"), InnerView->IsHidden());
		TestTrue(TEXT("Cancel retains both existing view identities"), T.Interaction->GetMuscleActor(InnerId) == InnerView
			&& T.Interaction->GetMuscleActor(OuterId) == OuterView);
		TestEqual(TEXT("Cancel removes the preview without leaving extra views"), T.CountMuscleViews(), 2);
		TestEqual(TEXT("Repeated canceled moves preserve stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 1);
	}
	FAssemblyMuscleInstance Inner, Outer;
	TestTrue(TEXT("Both original muscle instances remain installed"), T.Assembly->GetMuscle(InnerId, Inner) && T.Assembly->GetMuscle(OuterId, Outer));
	TestTrue(TEXT("Canceled moves preserve the inner side anchors and binding"),
		Inner.EndpointA.BoneLocalPoint.Equals(InnerA.BoneLocalPoint, 0.001)
		&& Inner.EndpointB.BoneLocalPoint.Equals(InnerB.BoneLocalPoint, 0.001)
		&& Inner.BindingKey == EAssemblyMuscleKey::W);
	TestTrue(TEXT("Canceled moves leave the outer side anchors and binding unchanged"),
		Outer.EndpointA.BoneLocalPoint.Equals(OuterA.BoneLocalPoint, 0.001)
		&& Outer.EndpointB.BoneLocalPoint.Equals(OuterB.BoneLocalPoint, 0.001)
		&& Outer.BindingKey == EAssemblyMuscleKey::D);
	return true;
}

#endif
