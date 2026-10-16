#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "PaperSpriteComponent.h"

namespace
{
	constexpr EAutomationTestFlags InteractionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FAssemblyInteractionTestWorld
	{
		UWorld* World;
		AActor* Owner;
		UAssemblyComponent* Assembly;
		UAssemblyInteractionComponent* Interaction;
		bool bReady = false;
		FText SetupReason;

		FAssemblyInteractionTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Owner);
			Owner->AddInstanceComponent(Root);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Owner->SetActorTransform(FTransform(FQuat(FVector::YAxisVector, 0.25), FVector(500.0, 0.0, 100.0)));

			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
			FAssemblyBoneDefinition Bone;
			Bone.Footprint.Reset(); // Free mode has no cell allocation requirement.
			FAssemblyFreeSettings Settings;
			Settings.bEnableSurfaceSnapping = false; // Baseline presentation transactions preserve exact requested poses.
			bReady = Assembly->ConfigureFreeAssembly(Settings, { Bone }, { FAssemblyMuscleDefinition() }, SetupReason);

			Interaction = NewObject<UAssemblyInteractionComponent>(Owner);
			Interaction->bAutoInitialize = false;
			// These tests exercise presentation transactions without a GameInstance message bus.
			Interaction->bAutoCreatePhysicsBridge = false;
			Owner->AddInstanceComponent(Interaction);
			Interaction->RegisterComponent();
			bReady = bReady && Interaction->InitializeInteraction(Assembly, SetupReason);
		}

		~FAssemblyInteractionTestWorld()
		{
			Interaction->ClearInteraction();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		bool InstallBone(FVector2D Position, FGuid& OutId, FText& Reason)
		{
			return Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason)
				&& Interaction->UpdateBoneDragAtLocalPosition(Position, Reason)
				&& Interaction->CommitBoneDrag(OutId, Reason);
		}

		FVector PointOnBone(FGuid Id, FVector2D Point) const
		{
			const ANormalBone* Bone = Interaction->GetBoneActor(Id);
			return Bone ? Bone->GetBoneBody()->GetComponentTransform().TransformPosition(FVector(Point.X, 0.0, Point.Y))
				: FVector::ZeroVector;
		}

		template <typename T>
		int32 CountViews() const
		{
			int32 Count = 0;
			for (TActorIterator<T> It(World); It; ++It)
			{
				if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetOwner() == Owner) { ++Count; }
			}
			return Count;
		}

		template <typename T>
		int32 CountComponents() const
		{
			TArray<T*> Components;
			Owner->GetComponents<T>(Components);
			return Components.Num();
		}

		ANormalBone* GetPreview() const
		{
			for (TActorIterator<ANormalBone> It(World); It; ++It)
			{
				if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetOwner() == Owner && !It->GetInstanceId().IsValid()) { return *It; }
			}
			return nullptr;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInteractionDragTest, "JGD2026.AssemblyInteraction.DragTransactions", InteractionTestFlags)
bool FAssemblyInteractionDragTest::RunTest(const FString& Parameters)
{
	FAssemblyInteractionTestWorld T;
	if (!TestTrue(TEXT("Initialize world-space interaction"), T.bReady)) { AddError(T.SetupReason.ToString()); return false; }
	FText Reason;
	FGuid Id;
	if (!TestTrue(TEXT("Bones can be dragged without a selected muscle key"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason))) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("Starting a drag does not consume stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	TestEqual(TEXT("Starting a drag does not install a model instance"), T.Assembly->GetBones().Num(), 0);
	TestFalse(TEXT("The default preview inside the core is invalid"), T.Interaction->GetBoneDragPreview().bValid);
	TestEqual(TEXT("Only one preview exists"), T.CountViews<ANormalBone>(), 1);
	ANormalBone* Preview = T.GetPreview();
	if (!TestNotNull(TEXT("Drag created a world actor preview"), Preview)) { return false; }
	TestTrue(TEXT("Valid cursor position is evaluated"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96.0, 0.0), Reason));
	const FAssemblyPlacementResult Placement = T.Interaction->GetBoneDragPreview();
	TestTrue(TEXT("Preview has the same world transform as the placement model"),
		Preview->GetActorTransform().Equals(Placement.LocalVisualTransform * T.Assembly->GetGridFrameTransform(), 0.001));
	TestTrue(TEXT("Valid geometry changes the preview feedback"), Preview->GetBoneSprite()->GetSpriteColor().Equals(T.Interaction->ValidPreviewColor));
	T.Interaction->CancelBoneDrag();
	TestFalse(TEXT("Cancel closes the drag"), T.Interaction->IsBoneDragActive());
	TestEqual(TEXT("Cancel destroys the preview"), T.CountViews<ANormalBone>(), 0);
	TestEqual(TEXT("Cancel preserves stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);

	TestTrue(TEXT("Begin another shelf drag"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason));
	TestFalse(TEXT("Core overlap is rejected during preview"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D::ZeroVector, Reason));
	TestTrue(TEXT("Invalid geometry changes feedback"), T.GetPreview()->GetBoneSprite()->GetSpriteColor().Equals(T.Interaction->InvalidPreviewColor));
	TestFalse(TEXT("Invalid release is rejected"), T.Interaction->CommitBoneDrag(Id, Reason));
	TestFalse(TEXT("Invalid release returns no instance identity"), Id.IsValid());
	TestFalse(TEXT("Invalid release automatically cancels"), T.Interaction->IsBoneDragActive());
	TestEqual(TEXT("Invalid release does not consume stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	TestEqual(TEXT("Invalid release leaves no preview or installed actor"), T.CountViews<ANormalBone>(), 0);

	TestTrue(TEXT("Start a drag before testing a lost cursor"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason));
	TestTrue(TEXT("Cursor was previously at a legal location"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96.0, 0.0), Reason));
	TestFalse(TEXT("Missing PlayerController cannot deproject the mouse"), T.Interaction->UpdateBoneDragFromMouse(nullptr, Reason));
	TestFalse(TEXT("Lost cursor does not commit the previous valid position"), T.Interaction->CommitBoneDrag(Id, Reason));
	TestEqual(TEXT("Lost cursor preserves stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);

	if (!TestTrue(TEXT("Commit a legal installation"), T.InstallBone(FVector2D(96.0, 0.0), Id, Reason))) { AddError(Reason.ToString()); return false; }
	ANormalBone* Bone = T.Interaction->GetBoneActor(Id);
	if (!TestNotNull(TEXT("Installation creates its bound actor"), Bone)) { return false; }
	TestEqual(TEXT("Installation consumes exactly one bone"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 3);
	TestTrue(TEXT("Only the installed actor remains"), T.CountViews<ANormalBone>() == 1 && Bone->GetInstanceId() == Id);
	const FTransform OriginalPose = Bone->GetActorTransform();
	TestTrue(TEXT("Begin moving an installed instance"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, Reason));
	TestTrue(TEXT("Original view is hidden while the moving preview is visible"), Bone->IsHidden());
	TestTrue(TEXT("Moving preview can be freely positioned"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(140.25, 65.5), Reason));
	TestTrue(TEXT("Moving preview accepts an arbitrary angle"), T.Interaction->RotateBoneDrag(37.0f, Reason));
	TestTrue(TEXT("Drag has not changed the installed actor pose"), Bone->GetActorTransform().Equals(OriginalPose, 0.001));
	T.Interaction->CancelBoneDrag();
	TestFalse(TEXT("Canceled move restores the original actor visibility"), Bone->IsHidden());
	TestTrue(TEXT("Canceled move restores the exact original pose"), Bone->GetActorTransform().Equals(OriginalPose, 0.001));
	TestEqual(TEXT("Canceled move does not duplicate or consume stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 3);

	TestTrue(TEXT("Begin move that will fail"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, Reason));
	TestFalse(TEXT("Moving into the core is invalid"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D::ZeroVector, Reason));
	FGuid MovedId;
	TestFalse(TEXT("Illegal move is not committed"), T.Interaction->CommitBoneDrag(MovedId, Reason));
	TestTrue(TEXT("Illegal move preserves original view and visibility"), T.Interaction->GetBoneActor(Id) == Bone && !Bone->IsHidden());
	TestTrue(TEXT("Illegal move preserves original pose"), Bone->GetActorTransform().Equals(OriginalPose, 0.001));
	TestTrue(TEXT("Begin legal move"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, Reason));
	TestTrue(TEXT("Set fractional free position"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(140.25, 65.5), Reason));
	TestTrue(TEXT("Set non-quarter-turn angle"), T.Interaction->RotateBoneDrag(37.0f, Reason));
	TestTrue(TEXT("Commit the legal move"), T.Interaction->CommitBoneDrag(MovedId, Reason));
	TestEqual(TEXT("Moving preserves instance identity"), MovedId, Id);
	TestTrue(TEXT("Moving preserves the actor identity"), T.Interaction->GetBoneActor(Id) == Bone);
	FAssemblyBoneInstance Data;
	TestTrue(TEXT("Original ID still resolves"), T.Assembly->GetBone(Id, Data));
	TestTrue(TEXT("Move position stays continuous"), Data.LocalPosition.Equals(FVector2D(140.25, 65.5), 0.001));
	TestTrue(TEXT("Move angle is not snapped to 90 degrees"), FMath::IsNearlyEqual(Data.RotationDegrees, 37.0f));
	TestTrue(TEXT("Installed actor and model agree after moving"), Bone->GetActorTransform().Equals(Data.LocalVisualTransform * T.Assembly->GetGridFrameTransform(), 0.001));
	TestEqual(TEXT("Move leaves one installed view and no preview"), T.CountViews<ANormalBone>(), 1);
	TestEqual(TEXT("Move leaves stock unchanged"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInteractionMuscleLifecycleTest, "JGD2026.AssemblyInteraction.MuscleAndViewLifecycle", InteractionTestFlags)
bool FAssemblyInteractionMuscleLifecycleTest::RunTest(const FString& Parameters)
{
	FAssemblyInteractionTestWorld T;
	if (!TestTrue(TEXT("Initialize world-space interaction"), T.bReady)) { AddError(T.SetupReason.ToString()); return false; }
	FText Reason;
	FGuid BoneA, BoneB, MuscleA, MuscleB;
	if (!TestTrue(TEXT("Install the core-connected bone"), T.InstallBone(FVector2D(96.0, 0.0), BoneA, Reason))
		|| !TestTrue(TEXT("Stage a surface-touching second bone without a direct rigid joint"), T.InstallBone(FVector2D(224.0, 0.0), BoneB, Reason))) { AddError(Reason.ToString()); return false; }
	TestFalse(TEXT("Touching ordinary bones remain disconnected until a muscle or independent joint connects them"), T.Assembly->IsStructureConnected());
	const FVector A = T.PointOnBone(BoneA, FVector2D(20.25, 2.5));
	const FVector B = T.PointOnBone(BoneB, FVector2D(-20.75, -2.5));
	TestFalse(TEXT("Muscle installation requires a selected key"), T.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"), A, B, MuscleA, Reason));
	TestEqual(TEXT("No-key failure preserves muscle stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestEqual(TEXT("No-key failure does not leave a muscle view"), T.CountViews<AAssemblyMuscle>(), 0);
	TestTrue(TEXT("Select W for muscles"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	const FVector CorePoint = T.Assembly->GetGridFrameTransform().TransformPosition(FVector::ZeroVector);
	TestFalse(TEXT("The core cannot be used as a muscle endpoint"), T.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"), CorePoint, B, MuscleA, Reason));
	if (!TestTrue(TEXT("Install muscle using two continuous world points"), T.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"), A, B, MuscleA, Reason))) { AddError(Reason.ToString()); return false; }
	AAssemblyMuscle* Curve = T.Interaction->GetMuscleActor(MuscleA);
	if (!TestNotNull(TEXT("Installed muscle has a curve actor"), Curve)) { return false; }
	TestEqual(TEXT("Muscle installs exactly one model instance"), T.Assembly->GetMuscles().Num(), 1);
	TestEqual(TEXT("Muscle actor creation does not deduct stock again"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 2);
	const FAssemblyMuscleInstance InitialData = Curve->GetMuscleData();
	TestTrue(TEXT("World hits retain both bone identities"), InitialData.EndpointA.BoneId == BoneA && InitialData.EndpointB.BoneId == BoneB);
	TestTrue(TEXT("World hits retain fractional local points"), InitialData.EndpointA.BoneLocalPoint.Equals(FVector2D(20.25, 2.5), 0.001)
		&& InitialData.EndpointB.BoneLocalPoint.Equals(FVector2D(-20.75, -2.5), 0.001));
	FVector CurveA, CurveB;
	TestTrue(TEXT("Curve endpoints can be read"), Curve->GetCurveEndpoints(CurveA, CurveB));
	TestTrue(TEXT("Curve starts and ends at the real clicked world points"), CurveA.Equals(A, 0.001) && CurveB.Equals(B, 0.001));
	TestFalse(TEXT("An off-bone muscle move is rejected"), T.Interaction->MoveMuscleAtWorldPoints(MuscleA, CorePoint, B, Reason));
	TestTrue(TEXT("Failed muscle movement preserves endpoints and view identity"), T.Interaction->GetMuscleActor(MuscleA) == Curve
		&& Curve->GetMuscleData().EndpointA.BoneLocalPoint.Equals(InitialData.EndpointA.BoneLocalPoint, 0.001));
	const FVector SameBoneA = T.PointOnBone(BoneA, FVector2D(-20.0, 1.0));
	const FVector SameBoneB = T.PointOnBone(BoneA, FVector2D(25.0, -1.0));
	TestTrue(TEXT("Two distinct points on the same bone are allowed"), T.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"), SameBoneA, SameBoneB, MuscleB, Reason));
	AAssemblyMuscle* SameBoneCurve = T.Interaction->GetMuscleActor(MuscleB);
	if (!TestNotNull(TEXT("Second muscle has its own curve"), SameBoneCurve)) { return false; }
	TestTrue(TEXT("Two muscles can share W"), Curve->GetMuscleData().BindingKey == EAssemblyMuscleKey::W && SameBoneCurve->GetMuscleData().BindingKey == EAssemblyMuscleKey::W);
	TestTrue(TEXT("Connected assembly enters Playing"), T.Assembly->TryStartPlaying(Reason));
	TestFalse(TEXT("Playing phase rejects shelf editing"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason));
	TestTrue(TEXT("W requests contraction"), T.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("Both bound curve views receive contraction feedback"), Curve->GetMuscleData().bContracting && SameBoneCurve->GetMuscleData().bContracting);
	TestTrue(TEXT("W release requests relaxation"), T.Assembly->SetMuscleInput(EAssemblyMuscleKey::W, false));
	TestFalse(TEXT("First curve relaxes"), Curve->GetMuscleData().bContracting);
	TestFalse(TEXT("Second curve relaxes"), SameBoneCurve->GetMuscleData().bContracting);
	TestTrue(TEXT("Restart returns to editable assembly"), T.Assembly->RestartAssembly(Reason));
	TestTrue(TEXT("Reset destroys both muscle views"), Curve->IsActorBeingDestroyed() && SameBoneCurve->IsActorBeingDestroyed());
	TestNull(TEXT("Old first muscle ID no longer resolves to a view"), T.Interaction->GetMuscleActor(MuscleA));
	TestEqual(TEXT("Reset leaves no bone actor"), T.CountViews<ANormalBone>(), 0);
	TestEqual(TEXT("Reset leaves no muscle actor"), T.CountViews<AAssemblyMuscle>(), 0);
	TestEqual(TEXT("Reset restores bone stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	TestEqual(TEXT("Reset restores muscle stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);

	if (!TestTrue(TEXT("Install for removal validation"), T.InstallBone(FVector2D(96.0, 0.0), BoneA, Reason))) { return false; }
	ANormalBone* Bone = T.Interaction->GetBoneActor(BoneA);
	TestTrue(TEXT("Select A for this muscle"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::A, Reason));
	TestTrue(TEXT("Install a muscle on the new bone"), T.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"),
		T.PointOnBone(BoneA, FVector2D(-20.0, 0.0)), T.PointOnBone(BoneA, FVector2D(20.0, 0.0)), MuscleA, Reason));
	Curve = T.Interaction->GetMuscleActor(MuscleA);
	if (!TestNotNull(TEXT("Removable muscle view exists"), Curve)) { return false; }
	TestFalse(TEXT("A bone with attached muscles cannot be removed"), T.Interaction->RemoveInstance(BoneA, Reason));
	TestTrue(TEXT("Rejected removal preserves original bone view"), T.Interaction->GetBoneActor(BoneA) == Bone && !Bone->IsActorBeingDestroyed());
	TestTrue(TEXT("Remove the muscle first"), T.Interaction->RemoveInstance(MuscleA, Reason));
	TestTrue(TEXT("Removal destroys its muscle view"), Curve->IsActorBeingDestroyed());
	TestNull(TEXT("Removed muscle ID no longer resolves"), T.Interaction->GetMuscleActor(MuscleA));
	TestEqual(TEXT("Muscle removal restores stock once"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestFalse(TEXT("Removing the same ID twice is rejected"), T.Interaction->RemoveInstance(MuscleA, Reason));
	TestEqual(TEXT("Rejected duplicate removal does not duplicate stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestTrue(TEXT("Remove the now-unattached bone"), T.Interaction->RemoveInstance(BoneA, Reason));
	TestTrue(TEXT("Removal destroys its bone view"), Bone->IsActorBeingDestroyed());
	TestNull(TEXT("Removed bone pose source is cleared"), T.Assembly->GetBonePoseSource(BoneA));
	TestEqual(TEXT("Bone removal restores stock once"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	TestTrue(TEXT("Final inventory and identity invariants hold"), T.Assembly->ValidateAssembly().bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInteractionRollbackTest, "JGD2026.AssemblyInteraction.ViewFailureRollback", InteractionTestFlags)
bool FAssemblyInteractionRollbackTest::RunTest(const FString& Parameters)
{
	FAssemblyInteractionTestWorld T;
	if (!TestTrue(TEXT("Initialize world-space interaction"), T.bReady)) { AddError(T.SetupReason.ToString()); return false; }
	FText Reason;
	FGuid Id;
	TestTrue(TEXT("Begin a valid shelf transaction"), T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason));
	TestTrue(TEXT("Preview at the core tangency is valid"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96.0, 0.0), Reason));
	// Simulate presentation configuration disappearing between drag and release.
	T.Interaction->BoneActorClass = nullptr;
	TestFalse(TEXT("Installation fails if the final bone actor cannot be generated"), T.Interaction->CommitBoneDrag(Id, Reason));
	TestFalse(TEXT("View failure invalidates the new identity"), Id.IsValid());
	TestFalse(TEXT("View failure supplies a reason"), Reason.IsEmpty());
	TestEqual(TEXT("Bone view failure restores the exact stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
	TestEqual(TEXT("Bone view failure removes the unpresentable instance"), T.Assembly->GetBones().Num(), 0);
	TestEqual(TEXT("Bone view failure removes generated joints"), T.Assembly->GetJoints().Num(), 0);
	TestEqual(TEXT("Bone view failure leaves no orphan actor"), T.CountViews<ANormalBone>(), 0);
	TestFalse(TEXT("Bone view failure terminates the drag"), T.Interaction->IsBoneDragActive());
	T.Interaction->BoneActorClass = ANormalBone::StaticClass();
	if (!TestTrue(TEXT("A valid installation succeeds after restoring presentation"), T.InstallBone(FVector2D(96.0, 0.0), Id, Reason))) { AddError(Reason.ToString()); return false; }
	ANormalBone* Bone = T.Interaction->GetBoneActor(Id);
	if (!TestEqual(TEXT("Recovered installation has one core joint"), T.Assembly->GetJoints().Num(), 1)) { return false; }
	const FGuid JointId = T.Assembly->GetJoints()[0].InstanceId;
	TestTrue(TEXT("Select the muscle key"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::D, Reason));
	T.Interaction->MuscleActorClass = nullptr;
	FGuid MuscleId;
	TestFalse(TEXT("Muscle installation fails if a curve view cannot be created"), T.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"),
		T.PointOnBone(Id, FVector2D(-20.0, 0.0)), T.PointOnBone(Id, FVector2D(20.0, 0.0)), MuscleId, Reason));
	TestFalse(TEXT("Curve failure invalidates the new identity"), MuscleId.IsValid());
	TestEqual(TEXT("Curve failure restores muscle stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);
	TestEqual(TEXT("Curve failure removes its model instance"), T.Assembly->GetMuscles().Num(), 0);
	TestTrue(TEXT("Curve failure does not destroy an existing bone"), T.Interaction->GetBoneActor(Id) == Bone);
	TestTrue(TEXT("Curve failure preserves existing joints"), T.Assembly->GetJoints().Num() == 1 && T.Assembly->GetJoints()[0].InstanceId == JointId);
	TestEqual(TEXT("Curve failure leaves no orphan curve actor"), T.CountViews<AAssemblyMuscle>(), 0);
	TestTrue(TEXT("All stock and structure invariants survive view failures"), T.Assembly->ValidateAssembly().bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInteractionRestartTest, "JGD2026.AssemblyInteraction.ReinitializeAndThreeRestarts", InteractionTestFlags)
bool FAssemblyInteractionRestartTest::RunTest(const FString& Parameters)
{
	FAssemblyInteractionTestWorld T;
	if (!TestTrue(TEXT("Initialize world-space interaction"), T.bReady)) { AddError(T.SetupReason.ToString()); return false; }
	FText Reason;
	UPrimitiveComponent* Core = T.Interaction->GetCoreBody();
	const FTransform SpawnTransform = T.Owner->GetActorTransform();
	if (!TestNotNull(TEXT("Core placeholder has a body"), Core)) { return false; }
	for (int32 Round = 0; Round < 3; ++Round)
	{
		TestTrue(TEXT("Repeated initialization succeeds"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
		TestTrue(TEXT("Repeated initialization reuses the core body"), T.Interaction->GetCoreBody() == Core);
		TestEqual(TEXT("Exactly one core body component exists"), T.CountComponents<USphereComponent>(), 1);
		TestEqual(TEXT("Exactly one core visual component exists"), T.CountComponents<UStaticMeshComponent>(), 1);
		TestEqual(TEXT("A disabled automatic bridge is never created"), T.CountComponents<UAssemblyPhysicsBridgeComponent>(), 0);
		FGuid Id, MuscleId;
		if (!TestTrue(TEXT("Install one bone this round"), T.InstallBone(FVector2D(96.0, 0.0), Id, Reason))) { return false; }
		ANormalBone* Bone = T.Interaction->GetBoneActor(Id);
		if (!TestNotNull(TEXT("Round's bound bone actor exists"), Bone)) { return false; }
		TestTrue(TEXT("Repeated initialization preserves the bone actor"), T.Interaction->InitializeInteraction(T.Assembly, Reason) && T.Interaction->GetBoneActor(Id) == Bone);
		TestEqual(TEXT("Repeated initialization cannot duplicate bone views"), T.CountViews<ANormalBone>(), 1);
		TestTrue(TEXT("Select this round's binding"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::S, Reason));
		if (!TestTrue(TEXT("Install this round's curve"), T.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"),
			T.PointOnBone(Id, FVector2D(-20.0, 0.0)), T.PointOnBone(Id, FVector2D(20.0, 0.0)), MuscleId, Reason))) { return false; }
		AAssemblyMuscle* Curve = T.Interaction->GetMuscleActor(MuscleId);
		if (!TestNotNull(TEXT("Round's bound muscle actor exists"), Curve)) { return false; }
		TestTrue(TEXT("Repeated initialization preserves the curve actor"), T.Interaction->InitializeInteraction(T.Assembly, Reason) && T.Interaction->GetMuscleActor(MuscleId) == Curve);
		TestEqual(TEXT("Repeated initialization cannot duplicate curve views"), T.CountViews<AAssemblyMuscle>(), 1);
		TestTrue(TEXT("Begin a drag before phase switching"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, Reason));
		TestTrue(TEXT("Begin Playing"), T.Assembly->TryStartPlaying(Reason));
		TestFalse(TEXT("Playing automatically cancels the drag"), T.Interaction->IsBoneDragActive());
		TestFalse(TEXT("Playing restores the installed view's visibility"), Bone->IsHidden());
		TestEqual(TEXT("Playing leaves no drag preview"), T.CountViews<ANormalBone>(), 1);
		T.Owner->SetActorLocation(SpawnTransform.GetLocation() + FVector(350.0, 0.0, 40.0));
		TestTrue(TEXT("Restart this round"), T.Assembly->RestartAssembly(Reason));
		TestTrue(TEXT("Nonphysical owner returns to its spawn pose"), T.Owner->GetActorTransform().Equals(SpawnTransform, 0.001));
		TestTrue(TEXT("Each restart destroys old bound actors"), Bone->IsActorBeingDestroyed() && Curve->IsActorBeingDestroyed());
		TestNull(TEXT("Previous bone identity cannot resolve after restart"), T.Interaction->GetBoneActor(Id));
		TestNull(TEXT("Previous muscle identity cannot resolve after restart"), T.Interaction->GetMuscleActor(MuscleId));
		TestNull(TEXT("Previous pose registration is removed"), T.Assembly->GetBonePoseSource(Id));
		TestEqual(TEXT("Restart leaves no residual bone or preview actor"), T.CountViews<ANormalBone>(), 0);
		TestEqual(TEXT("Restart leaves no residual curve actor"), T.CountViews<AAssemblyMuscle>(), 0);
		TestEqual(TEXT("Restart restores bone stock exactly"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 4);
		TestEqual(TEXT("Restart restores muscle stock exactly"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 3);
		TestTrue(TEXT("Core body survives without duplication"), T.Interaction->GetCoreBody() == Core && T.CountComponents<USphereComponent>() == 1);
		TestTrue(TEXT("Restart keeps model initialized and ready to assemble"), T.Interaction->IsInteractionInitialized() && T.Assembly->GetPhase() == EAssemblyPhase::Assembly);
		TestTrue(TEXT("Inventory and connected structure invariants hold after each restart"), T.Assembly->ValidateAssembly().bValid);
	}
	TestFalse(TEXT("Invalid reinitialization is rejected"), T.Interaction->InitializeInteraction(nullptr, Reason));
	TestTrue(TEXT("Invalid reinitialization preserves the working binding and core"), T.Interaction->GetAssemblyComponent() == T.Assembly && T.Interaction->GetCoreBody() == Core);
	T.Interaction->ClearInteraction();
	T.Interaction->ClearInteraction();
	TestFalse(TEXT("Clear is idempotent and removes the model binding"), T.Interaction->IsInteractionInitialized());
	TestNull(TEXT("Clear removes its core body reference"), T.Interaction->GetCoreBody());
	TestEqual(TEXT("Clear removes its owned core body"), T.CountComponents<USphereComponent>(), 0);
	TestEqual(TEXT("Clear removes its owned core visual"), T.CountComponents<UStaticMeshComponent>(), 0);
	TestTrue(TEXT("Interaction can initialize again after clear"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestEqual(TEXT("Reinitialization after clear creates one core body"), T.CountComponents<USphereComponent>(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInteractionOwnershipTest, "JGD2026.AssemblyInteraction.ExternalBodyOwnership", InteractionTestFlags)
bool FAssemblyInteractionOwnershipTest::RunTest(const FString& Parameters)
{
	FAssemblyInteractionTestWorld T;
	if (!TestTrue(TEXT("Initialize world-space interaction"), T.bReady)) { AddError(T.SetupReason.ToString()); return false; }
	FText Reason;
	FGuid Id;
	if (!TestTrue(TEXT("Install the displayed bone"), T.InstallBone(FVector2D(96.0, 0.0), Id, Reason))) { return false; }
	ANormalBone* Bone = T.Interaction->GetBoneActor(Id);
	if (!TestNotNull(TEXT("Installed bone view exists"), Bone)) { return false; }
	USceneComponent* ExternalPose = NewObject<USceneComponent>(T.Owner);
	T.Owner->AddInstanceComponent(ExternalPose);
	ExternalPose->RegisterComponent();
	const FTransform PhysicalPose(FQuat(FVector::YAxisVector, 0.6), FVector(850.0, 0.0, 210.0));
	ExternalPose->SetWorldTransform(PhysicalPose);
	TestTrue(TEXT("Physics provider can take over the pose source"), T.Assembly->RegisterBonePoseSource(Id, ExternalPose, Reason));
	TestFalse(TEXT("Interaction refuses to drag a physics-owned bone"), T.Interaction->BeginBoneDrag(TEXT("Bone"), Id, Reason));
	TestFalse(TEXT("Rejected drag leaves no active transaction"), T.Interaction->IsBoneDragActive());
	TestEqual(TEXT("Rejected drag leaves no preview"), T.CountViews<ANormalBone>(), 1);
	TestTrue(TEXT("Synchronization does not overwrite external physical pose"), T.Interaction->SynchronizeViews(Reason) && ExternalPose->GetComponentTransform().Equals(PhysicalPose, 0.001));
	T.Interaction->ClearInteraction();
	TestTrue(TEXT("Destroying the view preserves a provider's replacement registration"), T.Assembly->GetBonePoseSource(Id) == ExternalPose);
	TestTrue(TEXT("Reinitialize with the external pose source"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestNull(TEXT("Reinitialize does not duplicate the external physical bone"), T.Interaction->GetBoneActor(Id));
	TestEqual(TEXT("Reinitialize leaves no owned bone view for the provider's bone"), T.CountViews<ANormalBone>(), 0);
	T.Interaction->ClearInteraction();

	USphereComponent* ExternalCore = NewObject<USphereComponent>(T.Owner);
	T.Owner->AddInstanceComponent(ExternalCore);
	ExternalCore->RegisterComponent();
	T.Interaction->ExistingCoreBody = ExternalCore;
	TestTrue(TEXT("Initialize using a provider's existing core"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestTrue(TEXT("The supplied core is authoritative"), T.Interaction->GetCoreBody() == ExternalCore);
	TestEqual(TEXT("Existing core does not create an extra sphere body"), T.CountComponents<USphereComponent>(), 1);
	TestEqual(TEXT("Existing core does not create a placeholder mesh"), T.CountComponents<UStaticMeshComponent>(), 0);
	TestTrue(TEXT("Repeated initialization keeps the supplied core"), T.Interaction->InitializeInteraction(T.Assembly, Reason) && T.Interaction->GetCoreBody() == ExternalCore);
	T.Interaction->ClearInteraction();
	TestTrue(TEXT("Clear never destroys a core owned by the physics provider"), IsValid(ExternalCore) && ExternalCore->IsRegistered());
	TestEqual(TEXT("Only the supplied core remains after clear"), T.CountComponents<USphereComponent>(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInteractionWorldIsolationTest, "JGD2026.AssemblyInteraction.CrossWorldIsolation", InteractionTestFlags)
bool FAssemblyInteractionWorldIsolationTest::RunTest(const FString& Parameters)
{
	FAssemblyInteractionTestWorld Local;
	FAssemblyInteractionTestWorld Foreign;
	if (!TestTrue(TEXT("Both independent test worlds are initialized"), Local.bReady && Foreign.bReady)) { return false; }
	TestTrue(TEXT("Fixture worlds are different"), Local.World != Foreign.World);
	FText Reason;
	FGuid LocalBoneId, ForeignBoneId, LocalMuscleId, ForeignMuscleId;
	if (!TestTrue(TEXT("Install the local bone"), Local.InstallBone(FVector2D(96.0, 0.0), LocalBoneId, Reason))
		|| !TestTrue(TEXT("Install the foreign bone"), Foreign.InstallBone(FVector2D(96.0, 0.0), ForeignBoneId, Reason))) { return false; }
	ANormalBone* LocalBone = Local.Interaction->GetBoneActor(LocalBoneId);
	if (!TestNotNull(TEXT("Local installed bone exists"), LocalBone)) { return false; }
	UPrimitiveComponent* LocalCore = Local.Interaction->GetCoreBody();
	UPrimitiveComponent* ForeignCore = Foreign.Interaction->GetCoreBody();
	if (!TestNotNull(TEXT("Foreign core exists for isolation checks"), ForeignCore)) { return false; }
	Local.Interaction->ExistingCoreBody = ForeignCore;
	TestFalse(TEXT("Interaction rejects an existing core from another world even without an automatic bridge"), Local.Interaction->InitializeInteraction(Local.Assembly, Reason));
	TestTrue(TEXT("Rejected foreign core preserves the existing model binding"), Local.Interaction->GetAssemblyComponent() == Local.Assembly);
	TestTrue(TEXT("Rejected foreign core preserves the installed bone view"), Local.Interaction->GetBoneActor(LocalBoneId) == LocalBone);
	TestEqual(TEXT("Rejected foreign core preserves the existing local placeholder"), Local.CountComponents<USphereComponent>(), 1);
	TestTrue(TEXT("Rejected foreign core does not destroy the other world's body"), IsValid(ForeignCore) && ForeignCore->IsRegistered());
	Local.Interaction->ExistingCoreBody = nullptr;
	TestTrue(TEXT("The original core remains available after correcting the configuration"), Local.Interaction->GetCoreBody() == LocalCore);
	TestTrue(TEXT("Select a key in the local model"), Local.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	if (!TestTrue(TEXT("Install the local muscle"), Local.Interaction->InstallMuscleAtWorldPoints(TEXT("Muscle"),
		Local.PointOnBone(LocalBoneId, FVector2D(-20.0, 0.0)), Local.PointOnBone(LocalBoneId, FVector2D(20.0, 0.0)), LocalMuscleId, Reason))) { return false; }
	AAssemblyMuscle* LocalMuscle = Local.Interaction->GetMuscleActor(LocalMuscleId);
	if (!TestNotNull(TEXT("Local installed muscle exists"), LocalMuscle)) { return false; }

	// Keep a valid foreign model, but release its presenter and pose registration. Thus an
	// unbound actor cannot fail merely because another actor already owns the foreign ID.
	Foreign.Interaction->ClearInteraction();
	Foreign.Owner->SetActorTransform(FTransform(FQuat(FVector::YAxisVector, -0.5), FVector(900.0, 0.0, -250.0)));
	TestNull(TEXT("Foreign bone has no view pose source"), Foreign.Assembly->GetBonePoseSource(ForeignBoneId));
	FAssemblyMuscleEndpoint A, B;
	A.BoneId = ForeignBoneId;
	A.BoneLocalPoint = FVector2D(-20.0, 0.0);
	B.BoneId = ForeignBoneId;
	B.BoneLocalPoint = FVector2D(20.0, 0.0);
	TestTrue(TEXT("Select a key in the foreign model"), Foreign.Assembly->SelectMuscleKey(EAssemblyMuscleKey::D, Reason));
	if (!TestTrue(TEXT("Foreign model has a valid muscle without a curve actor"), Foreign.Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, ForeignMuscleId, Reason))) { return false; }

	TestFalse(TEXT("Interaction cannot switch to a model in another world"), Local.Interaction->InitializeInteraction(Foreign.Assembly, Reason));
	TestFalse(TEXT("Cross-world interaction rejection supplies a reason"), Reason.IsEmpty());
	TestTrue(TEXT("Rejected cross-world initialization preserves the original model"), Local.Interaction->GetAssemblyComponent() == Local.Assembly);
	TestTrue(TEXT("Rejected cross-world initialization preserves the original core"), Local.Interaction->GetCoreBody() == LocalCore);
	TestTrue(TEXT("Rejected cross-world initialization preserves both original views"),
		Local.Interaction->GetBoneActor(LocalBoneId) == LocalBone && Local.Interaction->GetMuscleActor(LocalMuscleId) == LocalMuscle);
	TestEqual(TEXT("Rejected cross-world initialization creates no duplicate bone"), Local.CountViews<ANormalBone>(), 1);
	TestEqual(TEXT("Rejected cross-world initialization creates no duplicate curve"), Local.CountViews<AAssemblyMuscle>(), 1);

	TestFalse(TEXT("A bound bone cannot switch to a foreign model"), LocalBone->InitializeBone(Foreign.Assembly, ForeignBoneId, Reason));
	TestTrue(TEXT("Rejected bone rebind preserves its original model and identity"), LocalBone->GetAssemblyComponent() == Local.Assembly && LocalBone->GetInstanceId() == LocalBoneId);
	ANormalBone* UnboundBone = Local.World->SpawnActor<ANormalBone>();
	if (!TestNotNull(TEXT("Local unbound bone exists"), UnboundBone)) { return false; }
	const FTransform UnboundBonePose = UnboundBone->GetActorTransform();
	TestFalse(TEXT("An unbound bone also refuses a model from another world"), UnboundBone->InitializeBone(Foreign.Assembly, ForeignBoneId, Reason));
	TestFalse(TEXT("Cross-world bone rejection does not bind the actor"), UnboundBone->IsBoneInitialized());
	TestTrue(TEXT("Cross-world bone rejection does not teleport the actor"), UnboundBone->GetActorTransform().Equals(UnboundBonePose, 0.001));
	TestNull(TEXT("Cross-world bone rejection does not register a foreign pose source"), Foreign.Assembly->GetBonePoseSource(ForeignBoneId));
	UnboundBone->Destroy();

	TestFalse(TEXT("A bound muscle cannot switch to a foreign model"), LocalMuscle->InitializeMuscle(Foreign.Assembly, ForeignMuscleId, Reason));
	TestTrue(TEXT("Rejected muscle rebind preserves its original model and identity"), LocalMuscle->GetAssemblyComponent() == Local.Assembly && LocalMuscle->GetInstanceId() == LocalMuscleId);
	AAssemblyMuscle* UnboundMuscle = Local.World->SpawnActor<AAssemblyMuscle>();
	if (!TestNotNull(TEXT("Local unbound curve exists"), UnboundMuscle)) { return false; }
	TestFalse(TEXT("An unbound muscle also refuses a model from another world"), UnboundMuscle->InitializeMuscle(Foreign.Assembly, ForeignMuscleId, Reason));
	TestFalse(TEXT("Cross-world muscle rejection does not bind the actor"), UnboundMuscle->IsMuscleInitialized());
	UnboundMuscle->Destroy();

	TestTrue(TEXT("Set a valid same-world assembly frame"), Local.Assembly->TrySetAssemblyFrame(Local.Owner->GetRootComponent(), Reason));
	const FTransform LocalFrame = Local.Assembly->GetGridFrameTransform();
	TestFalse(TEXT("Assembly frame cannot refer to another world"), Local.Assembly->TrySetAssemblyFrame(Foreign.Owner->GetRootComponent(), Reason));
	TestFalse(TEXT("Cross-world frame rejection supplies a reason"), Reason.IsEmpty());
	TestTrue(TEXT("Rejected cross-world frame keeps the original valid frame"), Local.Assembly->GetGridFrameTransform().Equals(LocalFrame, 0.001));
	TestFalse(TEXT("Pose sources cannot refer to another world"), Local.Assembly->RegisterBonePoseSource(LocalBoneId, Foreign.Owner->GetRootComponent(), Reason));
	TestFalse(TEXT("Cross-world pose rejection supplies a reason"), Reason.IsEmpty());
	TestTrue(TEXT("Rejected cross-world pose keeps the installed body's registration"), Local.Assembly->GetBonePoseSource(LocalBoneId) == LocalBone->GetBoneBody());
	TestTrue(TEXT("Local world inventory and structure remain valid"), Local.Assembly->ValidateAssembly().bValid);
	TestTrue(TEXT("Foreign world inventory and structure remain valid"), Foreign.Assembly->ValidateAssembly().bValid);

	// Also check first initialization: no local placeholder or binding may be created
	// before the supplied core's world has been validated.
	USphereComponent* ForeignProvidedCore = NewObject<USphereComponent>(Foreign.Owner);
	Foreign.Owner->AddInstanceComponent(ForeignProvidedCore);
	ForeignProvidedCore->RegisterComponent();
	Local.Interaction->ClearInteraction();
	Local.Interaction->ExistingCoreBody = ForeignProvidedCore;
	TestFalse(TEXT("First initialization also rejects a core from another world"), Local.Interaction->InitializeInteraction(Local.Assembly, Reason));
	TestFalse(TEXT("Rejected first initialization leaves no model binding"), Local.Interaction->IsInteractionInitialized());
	TestEqual(TEXT("Rejected first initialization creates no local core body"), Local.CountComponents<USphereComponent>(), 0);
	TestEqual(TEXT("Rejected first initialization creates no local core visual"), Local.CountComponents<UStaticMeshComponent>(), 0);
	TestEqual(TEXT("Rejected first initialization creates no bone view"), Local.CountViews<ANormalBone>(), 0);
	TestEqual(TEXT("Rejected first initialization creates no curve view"), Local.CountViews<AAssemblyMuscle>(), 0);
	TestTrue(TEXT("Rejected first initialization leaves the external core intact"), IsValid(ForeignProvidedCore) && ForeignProvidedCore->IsRegistered());
	Local.Interaction->ExistingCoreBody = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyInteractionInitializationFailureTest, "JGD2026.AssemblyInteraction.InitializationFailureCleanup", InteractionTestFlags)
bool FAssemblyInteractionInitializationFailureTest::RunTest(const FString& Parameters)
{
	FAssemblyInteractionTestWorld T;
	if (!TestTrue(TEXT("Initialize the fixture"), T.bReady)) { AddError(T.SetupReason.ToString()); return false; }
	T.Interaction->ClearInteraction();
	FText Reason;
	FGuid BoneId, MuscleId;
	if (!TestTrue(TEXT("Prepare a valid model without a presenter"), T.Assembly->TryInstallFreeBone(TEXT("Bone"), FVector2D(96.0, 0.0), 0.0f, BoneId, Reason))) { return false; }
	T.Interaction->BoneActorClass = nullptr;
	TestFalse(TEXT("First initialization fails when the bone view class is unavailable"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestFalse(TEXT("Failed first initialization supplies the presentation error"), Reason.IsEmpty());
	TestFalse(TEXT("Failed first initialization leaves no binding"), T.Interaction->IsInteractionInitialized());
	TestNull(TEXT("Failed first initialization leaves no assembly reference"), T.Interaction->GetAssemblyComponent());
	TestNull(TEXT("Failed first initialization leaves no owned core reference"), T.Interaction->GetCoreBody());
	TestEqual(TEXT("Failed first initialization leaves no core body"), T.CountComponents<USphereComponent>(), 0);
	TestEqual(TEXT("Failed first initialization leaves no core visual"), T.CountComponents<UStaticMeshComponent>(), 0);
	TestEqual(TEXT("Failed first initialization leaves no bone actor"), T.CountViews<ANormalBone>(), 0);
	TestEqual(TEXT("Failed first initialization leaves no curve actor"), T.CountViews<AAssemblyMuscle>(), 0);
	TestEqual(TEXT("Failed first initialization preserves the existing model stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 3);

	// Fail after the bone was successfully presented, proving cleanup of partial views
	// and their pose registrations rather than only failure before the first spawn.
	T.Interaction->BoneActorClass = ANormalBone::StaticClass();
	T.Interaction->MuscleActorClass = nullptr;
	FAssemblyMuscleEndpoint A, B;
	A.BoneId = BoneId; A.BoneLocalPoint = FVector2D(-20.0, 0.0);
	B.BoneId = BoneId; B.BoneLocalPoint = FVector2D(20.0, 0.0);
	TestTrue(TEXT("Prepare a muscle binding"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason));
	if (!TestTrue(TEXT("Prepare a muscle model without a curve view"), T.Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, MuscleId, Reason))) { return false; }
	TestFalse(TEXT("First initialization fails after creating only a partial set of views"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestFalse(TEXT("Partial first initialization also leaves no model binding"), T.Interaction->IsInteractionInitialized());
	TestEqual(TEXT("Partial first initialization destroys the already-spawned bone view"), T.CountViews<ANormalBone>(), 0);
	TestEqual(TEXT("Partial first initialization leaves no muscle view"), T.CountViews<AAssemblyMuscle>(), 0);
	TestNull(TEXT("Partial first initialization clears its bone pose registration"), T.Assembly->GetBonePoseSource(BoneId));
	TestEqual(TEXT("Partial first initialization destroys its core body"), T.CountComponents<USphereComponent>(), 0);
	TestEqual(TEXT("Partial first initialization destroys its core visual"), T.CountComponents<UStaticMeshComponent>(), 0);
	TestEqual(TEXT("Cleanup does not discard the authoritative bone model"), T.Assembly->GetBones().Num(), 1);
	TestEqual(TEXT("Cleanup does not discard the authoritative muscle model"), T.Assembly->GetMuscles().Num(), 1);
	TestEqual(TEXT("Cleanup preserves muscle stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 2);
	TestTrue(TEXT("The authoritative model still passes validation"), T.Assembly->ValidateAssembly().bValid);

	T.Interaction->MuscleActorClass = AAssemblyMuscle::StaticClass();
	TestTrue(TEXT("Correcting the view class permits a clean retry"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestTrue(TEXT("Successful retry establishes the binding"), T.Interaction->IsInteractionInitialized());
	TestNotNull(TEXT("Successful retry presents the original bone identity"), T.Interaction->GetBoneActor(BoneId));
	TestNotNull(TEXT("Successful retry presents the original muscle identity"), T.Interaction->GetMuscleActor(MuscleId));
	TestEqual(TEXT("Successful retry creates exactly one core"), T.CountComponents<USphereComponent>(), 1);
	TestEqual(TEXT("Successful retry creates exactly one bone view"), T.CountViews<ANormalBone>(), 1);
	TestEqual(TEXT("Successful retry creates exactly one curve view"), T.CountViews<AAssemblyMuscle>(), 1);
	TestEqual(TEXT("Successful retry consumes no additional bone stock"), T.Assembly->GetRemainingQuantity(TEXT("Bone")), 3);
	TestEqual(TEXT("Successful retry consumes no additional muscle stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 2);

	// A later synchronization error has a different contract: keep a working binding
	// and its old views so correcting the configuration can recover the missing view.
	UPrimitiveComponent* WorkingCore = T.Interaction->GetCoreBody();
	ANormalBone* WorkingBone = T.Interaction->GetBoneActor(BoneId);
	AAssemblyMuscle* WorkingCurve = T.Interaction->GetMuscleActor(MuscleId);
	T.Interaction->MuscleActorClass = nullptr;
	FGuid MissingCurveId;
	A.BoneLocalPoint = FVector2D(-10.0, 1.0);
	B.BoneLocalPoint = FVector2D(10.0, -1.0);
	if (!TestTrue(TEXT("An external model edit introduces another muscle"), T.Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, MissingCurveId, Reason))) { return false; }
	TestNull(TEXT("The unavailable class prevents only the new curve view"), T.Interaction->GetMuscleActor(MissingCurveId));
	TestFalse(TEXT("Same-model retry reports the pending synchronization error"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestTrue(TEXT("Same-model retry failure preserves the established binding"), T.Interaction->GetAssemblyComponent() == T.Assembly);
	TestTrue(TEXT("Same-model retry failure preserves the established core and views"),
		T.Interaction->GetCoreBody() == WorkingCore && T.Interaction->GetBoneActor(BoneId) == WorkingBone && T.Interaction->GetMuscleActor(MuscleId) == WorkingCurve);
	T.Interaction->MuscleActorClass = AAssemblyMuscle::StaticClass();
	TestTrue(TEXT("Same-model synchronization recovers after correcting the class"), T.Interaction->InitializeInteraction(T.Assembly, Reason));
	TestNotNull(TEXT("Recovered synchronization creates the missing original identity"), T.Interaction->GetMuscleActor(MissingCurveId));
	TestEqual(TEXT("Recovered synchronization has exactly two curve views"), T.CountViews<AAssemblyMuscle>(), 2);
	TestEqual(TEXT("Recovered synchronization never consumes presentation stock"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 1);
	return true;
}

#endif
