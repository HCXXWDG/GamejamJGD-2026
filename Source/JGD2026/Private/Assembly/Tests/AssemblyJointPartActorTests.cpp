#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyJointPart.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"

namespace
{
	struct FAssemblyJointPartActorTestWorld
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UAssemblyComponent* Assembly = nullptr;
		UAssemblyInteractionComponent* Interaction = nullptr;
		bool bReady = false;
		FText SetupReason;

		FAssemblyJointPartActorTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Owner);
			Owner->AddInstanceComponent(Root);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Owner->SetActorTransform(FTransform(FQuat(FVector::YAxisVector, 0.25), FVector(500, 0, 100)));
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
			FAssemblyBoneDefinition Bone;
			Bone.Footprint.Reset();
			const FAssemblyBoneDefinition Cross = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Cross, 64.0f / 960.0f);
			FAssemblyFreeSettings Settings;
			Settings.bEnableSurfaceSnapping = false; // Keep existing exact-pose actor transaction coverage.
			bReady = Assembly->ConfigureFreeAssembly(Settings, { Bone, Cross }, {}, SetupReason);
			Interaction = NewObject<UAssemblyInteractionComponent>(Owner);
			Interaction->bAutoInitialize = false;
			Interaction->bAutoCreatePhysicsBridge = false;
			Owner->AddInstanceComponent(Interaction);
			Interaction->RegisterComponent();
			bReady = bReady && Interaction->InitializeInteraction(Assembly, SetupReason);
		}

		~FAssemblyJointPartActorTestWorld()
		{
			if (Interaction) { Interaction->ClearInteraction(); }
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		bool Install(FName TypeId, FVector2D Position, FGuid& OutId, FText& OutReason)
		{
			return Interaction->BeginBoneDrag(TypeId, FGuid(), OutReason)
				&& Interaction->UpdateBoneDragAtLocalPosition(Position, OutReason)
				&& Interaction->CommitBoneDrag(OutId, OutReason);
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
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyJointPartActorTransactionsTest, "JGD2026.AssemblyInteraction.JointParts.ActorAndDragTransactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAssemblyJointPartActorTransactionsTest::RunTest(const FString& Parameters)
{
	FAssemblyJointPartActorTestWorld T;
	if (!TestTrue(TEXT("Initialize a game world presenter with bone and independent Cross"), T.bReady))
	{
		AddError(T.SetupReason.ToString());
		return false;
	}
	FText Reason;
	AAssemblyJointPart* InvalidPreview = T.World->SpawnActor<AAssemblyJointPart>();
	if (!TestNotNull(TEXT("Create an independent preview for failure recovery"), InvalidPreview)) return false;
	const FAssemblyBoneDefinition CrossPreview = AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Cross, 64.0f / 960.0f);
	TestTrue(TEXT("Apply a valid preview definition"), InvalidPreview->ApplyRigidPartDefinition(CrossPreview, Reason));
	UPaperSprite* PreviousPreviewSprite = InvalidPreview->GetJointSprite()->GetSprite();
	UMaterialInterface* PreviousPreviewMaterial = InvalidPreview->GetJointSprite()->GetMaterial(0);
	InvalidPreview->BodyHalfThickness = 0.0f;
	TestFalse(TEXT("Invalid preview geometry refuses a different shape"), InvalidPreview->ApplyRigidPartDefinition(
		AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar), Reason));
	TestTrue(TEXT("Failed shape application restores its previous sprite and material"),
		InvalidPreview->GetJointSprite()->GetSprite() == PreviousPreviewSprite
		&& InvalidPreview->GetJointSprite()->GetMaterial(0) == PreviousPreviewMaterial
		&& InvalidPreview->GetRigidPartDefinition().JointShape == EAssemblyJointPartShape::Cross);
	InvalidPreview->Destroy();
	for (int32 Round = 0; Round < 3; ++Round)
	{
		FGuid RootBoneId, PartId, MovedId, ReinstalledId;
		if (!TestTrue(TEXT("Install an actual core-connected bone"), T.Install(TEXT("Bone"), FVector2D(96, 0), RootBoneId, Reason))
			|| !TestTrue(TEXT("Shelf drag commits an actual Cross actor"), T.Install(TEXT("Joint_Cross"), FVector2D(192, 0), PartId, Reason)))
		{
			AddError(Reason.ToString());
			return false;
		}
		AAssemblyJointPart* Part = T.Interaction->GetJointPartActor(PartId);
		if (!TestNotNull(TEXT("Bound view is a native independent JointPart"), Part)) { return false; }
		TestTrue(TEXT("Presenter selects the native JointPart class"), Part->GetClass() == AAssemblyJointPart::StaticClass());
		TestTrue(TEXT("Installed part registers its real body pose"), T.Assembly->GetBonePoseSource(PartId) == Part->GetJointBody());
		TestTrue(TEXT("Cross body keeps declared 64 by 64 dimensions"), Part->GetJointBody()->GetUnscaledBoxExtent().Equals(FVector(32, 4, 32), 0.001));
		UPaperSpriteComponent* SpriteView = Part->GetJointSprite();
		UPaperSprite* Sprite = SpriteView->GetSprite();
		if (!TestNotNull(TEXT("Cross uses existing Res artwork"), Sprite)) { return false; }
		TestFalse(TEXT("A public preview API cannot replace installed model geometry"), Part->ApplyRigidPartDefinition(
			AssemblyRigidPartMetrics::MakeJointPartDefinition(EAssemblyJointPartShape::Bar), Reason));
		TestTrue(TEXT("Refused installed override leaves sprite, definition and body unchanged"),
			Part->GetJointSprite()->GetSprite() == Sprite && Part->GetJointShape() == EAssemblyJointPartShape::Cross
			&& Part->GetJointBody()->GetUnscaledBoxExtent().Equals(FVector(32, 4, 32), 0.001));
		TestTrue(TEXT("Sprite comes from the authored Res sprite"), Sprite->GetPathName() == TEXT("/Game/Res/Art/Sprites/SP_Joint_Cross.SP_Joint_Cross"));
		const FVector Scale = SpriteView->GetRelativeScale3D();
		const double ExpectedScale = (64.0 / 960.0) * Sprite->GetPixelsPerUnrealUnit();
		TestTrue(TEXT("X and Z share the original canvas pixel scale"), FMath::IsNearlyEqual(Scale.X, ExpectedScale, 0.000001)
			&& FMath::IsNearlyEqual(Scale.Z, ExpectedScale, 0.000001));
		TestTrue(TEXT("Only the sprite moves into its rendering depth layer"), !FMath::IsNearlyZero(SpriteView->GetRelativeLocation().Y));
		TestTrue(TEXT("The actual body remains on Y=0"), FMath::IsNearlyZero(Part->GetJointBody()->GetComponentLocation().Y));
		TestEqual(TEXT("One commit consumes exactly one joint piece"), T.Assembly->GetRemainingQuantity(TEXT("Joint_Cross")), 1);
		TestEqual(TEXT("Only the bound part actor remains after the shelf drop"), T.CountViews<AAssemblyJointPart>(), 1);
		TestTrue(TEXT("The actual installed structure is connected"), T.Assembly->IsStructureConnected());

		TestTrue(TEXT("A second shelf drag creates a separate preview"), T.Interaction->BeginBoneDrag(TEXT("Joint_Cross"), FGuid(), Reason));
		TestTrue(TEXT("Second preview follows another legal local position"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(400, 120), Reason));
		TestEqual(TEXT("An active shelf preview does not consume remaining stock"), T.Assembly->GetRemainingQuantity(TEXT("Joint_Cross")), 1);
		TestEqual(TEXT("Bound actor and new preview both exist during drag"), T.CountViews<AAssemblyJointPart>(), 2);
		T.Interaction->CancelBoneDrag();
		TestEqual(TEXT("Cancel removes only the new preview"), T.CountViews<AAssemblyJointPart>(), 1);
		TestEqual(TEXT("Canceled shelf drag leaves inventory unchanged"), T.Assembly->GetRemainingQuantity(TEXT("Joint_Cross")), 1);

		const FTransform OriginalPose = Part->GetActorTransform();
		TestTrue(TEXT("Direct installed-part drag hides the original actor"), T.Interaction->BeginBoneDrag(TEXT("Joint_Cross"), PartId, Reason) && Part->IsHidden());
		TestFalse(TEXT("Moving a part into the core rejects the preview"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D::ZeroVector, Reason));
		TestFalse(TEXT("Illegal move cannot commit"), T.Interaction->CommitBoneDrag(MovedId, Reason));
		TestFalse(TEXT("Illegal drop returns no replacement ID"), MovedId.IsValid());
		TestTrue(TEXT("Illegal drop restores the exact installed actor and pose"), T.Interaction->GetJointPartActor(PartId) == Part
			&& !Part->IsHidden() && Part->GetActorTransform().Equals(OriginalPose, 0.001));
		TestEqual(TEXT("Illegal move keeps one actor and no stale preview"), T.CountViews<AAssemblyJointPart>(), 1);
		TestEqual(TEXT("Illegal move retains stock"), T.Assembly->GetRemainingQuantity(TEXT("Joint_Cross")), 1);
		TestTrue(TEXT("Begin another direct move"), T.Interaction->BeginBoneDrag(TEXT("Joint_Cross"), PartId, Reason));
		TestTrue(TEXT("Canceled move reaches a valid free position"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(400, 120), Reason));
		T.Interaction->CancelBoneDrag();
		TestTrue(TEXT("Explicit cancel restores original view and pose"), T.Interaction->GetJointPartActor(PartId) == Part
			&& !Part->IsHidden() && Part->GetActorTransform().Equals(OriginalPose, 0.001));
		TestTrue(TEXT("Begin a legal installed-part move"), T.Interaction->BeginBoneDrag(TEXT("Joint_Cross"), PartId, Reason));
		TestTrue(TEXT("Continuous moved position remains connected within tolerance"), T.Interaction->UpdateBoneDragAtLocalPosition(FVector2D(192, 8), Reason));
		TestTrue(TEXT("Commit legal move without a new instance"), T.Interaction->CommitBoneDrag(MovedId, Reason));
		TestTrue(TEXT("Move retains both model and actor identity"), MovedId == PartId && T.Interaction->GetJointPartActor(PartId) == Part);
		TestEqual(TEXT("Moving does not consume another piece"), T.Assembly->GetRemainingQuantity(TEXT("Joint_Cross")), 1);
		FAssemblyBoneInstance Model;
		TestTrue(TEXT("Actual moved actor agrees with authoritative local pose"), T.Assembly->GetBone(PartId, Model)
			&& Model.LocalPosition.Equals(FVector2D(192, 8), 0.001)
			&& Part->GetActorTransform().Equals(Model.LocalVisualTransform * T.Assembly->GetGridFrameTransform(), 0.001));
		TestTrue(TEXT("Moved body remains on the original gameplay plane"), FMath::IsNearlyZero(Part->GetJointBody()->GetComponentLocation().Y));

		TestTrue(TEXT("Removing a leaf part returns it to stock"), T.Interaction->RemoveInstance(PartId, Reason));
		TestFalse(TEXT("Removing the same ID twice cannot duplicate stock"), T.Interaction->RemoveInstance(PartId, Reason));
		TestEqual(TEXT("Removal restores the initial two pieces"), T.Assembly->GetRemainingQuantity(TEXT("Joint_Cross")), 2);
		TestTrue(TEXT("Removal releases actor lookup and the part model"), T.Interaction->GetJointPartActor(PartId) == nullptr && T.Assembly->GetJointParts().IsEmpty());
		TestEqual(TEXT("Removal destroys all independent part views"), T.CountViews<AAssemblyJointPart>(), 0);
		TestTrue(TEXT("Reinstall one part so restart exercises live actor cleanup"), T.Install(TEXT("Joint_Cross"), FVector2D(192, 0), ReinstalledId, Reason));
		TestTrue(TEXT("Restart clears the live round"), T.Assembly->RestartAssembly(Reason));
		TestTrue(TEXT("Restart has no residual rigid instances or edges"), T.Assembly->GetBones().IsEmpty() && T.Assembly->GetJointParts().IsEmpty() && T.Assembly->GetJoints().IsEmpty());
		TestEqual(TEXT("Every restart removes all JointPart actors"), T.CountViews<AAssemblyJointPart>(), 0);
		TestEqual(TEXT("Every restart also removes bone views"), T.CountViews<ANormalBone>(), 0);
		TestEqual(TEXT("Every restart restores exactly the level stock"), T.Assembly->GetRemainingQuantity(TEXT("Joint_Cross")), 2);
	}
	return true;
}

#endif
