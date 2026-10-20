#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Assembly/NormalBone.h"
#include "Assembly/AssemblyJointPart.h"
#include "Character/BaseCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Physics/JGDAssemblyPhysicsExecutorComponent.h"
#include "Physics/JGDPhysicsProfiles.h"
#include "Physics/JGDPhysicsSystemSettings.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include <limits>

struct FJGDAssemblyPhysicsExecutorTestAccessor
{
	static bool HasCoreDrive(const UJGDAssemblyPhysicsExecutorComponent* Executor)
	{
		return Executor && Executor->bCoreRotationDriveContextValid && !FMath::IsNearlyZero(Executor->CoreRotationInput);
	}
	static FVector GetCoreDriveAxis(const UJGDAssemblyPhysicsExecutorComponent* Executor)
	{
		return Executor ? Executor->CoreRotationWorldAxis : FVector::ZeroVector;
	}
	static UPhysicsConstraintComponent* GetConstraint(
		const UJGDAssemblyPhysicsExecutorComponent* Executor, FGuid JointId)
	{
		const TObjectPtr<UPhysicsConstraintComponent>* Found = Executor ? Executor->Constraints.Find(JointId) : nullptr;
		return Found ? Found->Get() : nullptr;
	}
	static bool HasMuscleDrive(const UJGDAssemblyPhysicsExecutorComponent* Executor, FGuid MuscleId)
	{
		return Executor && Executor->MuscleDrives.Contains(MuscleId);
	}
	static bool IsMuscleContracting(const UJGDAssemblyPhysicsExecutorComponent* Executor, FGuid MuscleId)
	{
		const auto* Drive = Executor ? Executor->MuscleDrives.Find(MuscleId) : nullptr;
		return Drive && Drive->bContracting;
	}
	static float GetMuscleRestLength(const UJGDAssemblyPhysicsExecutorComponent* Executor, FGuid MuscleId)
	{
		const auto* Rest = Executor ? Executor->MuscleRestStates.Find(MuscleId) : nullptr;
		return Rest ? Rest->Length : 0.0f;
	}
	static FVector GetMuscleDirection(const UJGDAssemblyPhysicsExecutorComponent* Executor, FGuid MuscleId)
	{
		const auto* Drive = Executor ? Executor->MuscleDrives.Find(MuscleId) : nullptr;
		return Drive ? Drive->LastDirection : FVector::ZeroVector;
	}
	static float GetMuscleForceOnB(const UJGDAssemblyPhysicsExecutorComponent* Executor, FGuid MuscleId,
		float Length, float RelativeSpeed = 0.0f, float DeltaSeconds = 0.0f)
	{
		const auto* Drive = Executor ? Executor->MuscleDrives.Find(MuscleId) : nullptr;
		return Drive ? Executor->ComputeMuscleForceOnB(*Drive, Length, RelativeSpeed, DeltaSeconds) : 0.0f;
	}
	static int32 GetMuscleRestCount(const UJGDAssemblyPhysicsExecutorComponent* Executor)
	{
		return Executor ? Executor->MuscleRestStates.Num() : 0;
	}
	static void ApplyMuscleDrives(UJGDAssemblyPhysicsExecutorComponent* Executor)
	{
		if (Executor) { Executor->ApplyMuscleDrives(0.016f); }
	}
};

namespace
{
	constexpr EAutomationTestFlags ExecutorFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FAssemblyPhysicsExecutorWorld
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		ULocalPlayer* LocalPlayer = nullptr;
		APlayerController* Controller = nullptr;
		ACharacter* Character = nullptr;
		UAssemblyComponent* Model = nullptr;
		UAssemblyInteractionComponent* Interaction = nullptr;
		UAssemblySessionComponent* Session = nullptr;
		UAssemblyPhysicsBridgeComponent* Bridge = nullptr;
		UJGDAssemblyPhysicsExecutorComponent* Executor = nullptr;
		UBoxComponent* Core = nullptr;
		TSoftObjectPtr<UJGDPhysicsWorldProfile> OriginalWorldProfile;
		FText Reason;
		bool bReady = false;

		explicit FAssemblyPhysicsExecutorWorld(bool bUseRealPlayerAsset = false)
		{
			UJGDPhysicsSystemSettings* PhysicsSettings = GetMutableDefault<UJGDPhysicsSystemSettings>();
			OriginalWorldProfile = PhysicsSettings->DefaultWorldProfile;
			PhysicsSettings->DefaultWorldProfile.Reset();
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone(FName(*FString::Printf(
				TEXT("AssemblyPhysicsExecutor_%s"), *FGuid::NewGuid().ToString())));
			World = GameInstance->GetWorld();
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			if (!World || !World->SetGameMode(URL))
			{
				Reason = FText::FromString(TEXT("Could not create the native GameMode for the executor fixture."));
				return;
			}
			World->InitializeActorsForPlay(URL);
			UClass* CharacterClass = bUseRealPlayerAsset
				? LoadClass<ABaseCharacter>(nullptr, TEXT("/Game/Character/Player/BP_Player.BP_Player_C"))
				: ACharacter::StaticClass();
			Character = CharacterClass ? World->SpawnActor<ACharacter>(CharacterClass) : nullptr;
			if (!Character)
			{
				Reason = FText::FromString(TEXT("Could not spawn the Character or BP_Player for the executor fixture."));
				return;
			}

			if (!bUseRealPlayerAsset)
			{
				Model = NewObject<UAssemblyComponent>(Character);
				Model->bAutoEnterAssembly = false;
				Character->AddInstanceComponent(Model);
				Model->RegisterComponent();
				if (!Model->ConfigureFreeAssembly(FAssemblyFreeSettings(),
					{ FAssemblyBoneDefinition() }, { FAssemblyMuscleDefinition() }, Reason))
				{
					return;
				}

				Core = NewObject<UBoxComponent>(Character);
				Character->AddInstanceComponent(Core);
				Core->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Core->RegisterComponent();

				Bridge = NewObject<UAssemblyPhysicsBridgeComponent>(Character);
				Character->AddInstanceComponent(Bridge);
				Bridge->RegisterComponent();

				Interaction = NewObject<UAssemblyInteractionComponent>(Character);
				Interaction->ExistingCoreBody = Core;
				Interaction->bCreateCorePlaceholder = false;
				Character->AddInstanceComponent(Interaction);
				Interaction->RegisterComponent();

				Session = NewObject<UAssemblySessionComponent>(Character);
				Character->AddInstanceComponent(Session);
				Session->RegisterComponent();

				Executor = NewObject<UJGDAssemblyPhysicsExecutorComponent>(Character);
				Character->AddInstanceComponent(Executor);
				Executor->RegisterComponent();
			}
			else
			{
				Model = Character->FindComponentByClass<UAssemblyComponent>();
				Interaction = Character->FindComponentByClass<UAssemblyInteractionComponent>();
				Session = Character->FindComponentByClass<UAssemblySessionComponent>();
				if (!Model || !Interaction || !Session)
				{
					Reason = FText::FromString(TEXT("BP_Player lacks its Assembly, Interaction or Session component."));
					return;
				}
			}
			Session->bAutoInitialize = false;
			Session->bCreateHUD = false;
			Session->bConfigureHUDInputMode = false;
			Session->InputMappingContext = NewObject<UInputMappingContext>(Session);
			Session->WAction = NewObject<UInputAction>(Session);
			Session->AAction = NewObject<UInputAction>(Session);
			Session->SAction = NewObject<UInputAction>(Session);
			Session->DAction = NewObject<UInputAction>(Session);

			Controller = World->SpawnActor<APlayerController>();
			LocalPlayer = NewObject<ULocalPlayer>(GEngine);
			LocalPlayer->PlayerAdded(nullptr, 0);
			Controller->SetPlayer(LocalPlayer);
			if (!Cast<UEnhancedPlayerInput>(Controller->PlayerInput))
			{
				Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
			}
			Controller->Possess(Character);
			World->BeginPlay();
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Character->GetCharacterMovement()->SetComponentTickEnabled(true);

			if (!bUseRealPlayerAsset && !Executor->InitializeExecutor(Bridge, Session, Reason))
			{
				return;
			}
			bReady = Session->InitializeSession(Controller, Reason);
			if (bUseRealPlayerAsset)
			{
				Bridge = Interaction->GetPhysicsBridge();
				Executor = Character->FindComponentByClass<UJGDAssemblyPhysicsExecutorComponent>();
			}
			bReady = bReady && IsValid(Bridge) && IsValid(Executor)
				&& Bridge->GetPhysicsLifecycleProvider() == Executor
				&& Session->CanOwnPhysicsLifecycle(Model);
			if (!bReady && Reason.IsEmpty())
			{
				Reason = FText::FromString(TEXT("Session, Bridge and Executor did not establish one lifecycle owner."));
			}
		}

		~FAssemblyPhysicsExecutorWorld()
		{
			if (IsValid(Session)) { Session->ShutdownSession(); }
			if (IsValid(Interaction)) { Interaction->ClearInteraction(); }
			if (IsValid(Bridge)) { Bridge->ClearBridge(); }
			if (IsValid(Controller)) { Controller->UnPossess(); }
			if (World) { World->EndPlay(EEndPlayReason::Quit); }
			if (IsValid(LocalPlayer)) { LocalPlayer->PlayerRemoved(); }
			if (World) { World->DestroyWorld(false); }
			if (GameInstance)
			{
				GameInstance->Shutdown();
				if (World) { GEngine->DestroyWorldContext(World); }
			}
			GetMutableDefault<UJGDPhysicsSystemSettings>()->DefaultWorldProfile = OriginalWorldProfile;
		}

		bool BroadcastRotation(bool bHeld, EAssemblyPhase Phase, FVector Axis,
			FAssemblyPhysicsContext Context = FAssemblyPhysicsContext())
		{
			UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(Model);
			if (!Bus) { return false; }
			if (!Context.SourceId.IsValid()) { Context = Bridge->GetPhysicsContext(); }
			FAssemblyPhysicsCoreRotationMessage Request;
			Request.Context = Context;
			Request.Phase = Phase;
			Request.bHeld = bHeld;
			Request.CoreBody = IsValid(Core) ? Core : Interaction->GetCoreBody();
			Request.WorldClockwiseAxis = Axis;
			Bus->BroadcastMessage(AssemblyMessageTags::CoreRotationRequested, FInstancedStruct::Make(Request));
			return true;
		}

		void Step(float Seconds = 0.025f)
		{
			if (World) { World->Tick(LEVELTICK_All, Seconds); }
		}

		bool GetCoreBonePlacement(FVector2D& OutPosition)
		{
			FAssemblyBoneDefinition Definition;
			FNormalBonePhysicalStats Stats;
			if (!Model->GetBoneDefinition(TEXT("Bone"), Definition)
				|| !ANormalBone::CalculateBoneStats(Definition.BoneParameters, Stats, Reason))
			{
				return false;
			}
			// Exercise a real surface connection at the configured gameplay scale.
			const FAssemblyFreeSettings Settings = Model->GetFreeSettings();
			OutPosition = Settings.CoreLocalPosition + FVector2D(Settings.CoreRadius + Stats.Length * .5f, 0);
			return true;
		}

		FGuid InstallBoneWithJoint()
		{
			FGuid BoneId;
			FVector2D Position;
			if (!GetCoreBonePlacement(Position)
				|| !Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason)
				|| !Interaction->UpdateBoneDragAtLocalPosition(Position, Reason)
				|| !Interaction->CommitBoneDrag(BoneId, Reason))
			{
				return FGuid();
			}
			const TArray<FAssemblyJointInstance> Joints = Model->GetJoints();
			return Joints.Num() == 1 ? Joints[0].InstanceId : FGuid();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyContactGripTest,
	"JGD2026.Assembly.PhysicsExecutor.ContactGripRestoresMaterials", ExecutorFlags)

bool FJGDAssemblyContactGripTest::RunTest(const FString& Parameters)
{
	FAssemblyPhysicsExecutorWorld T;
	if (!TestTrue(TEXT("Contact fixture initializes"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
	if (!TestTrue(TEXT("Contact fixture installs a physical bone"), T.InstallBoneWithJoint().IsValid())) { return false; }
	UPrimitiveComponent* BoneBody = T.Interaction->GetBoneActor(T.Model->GetBones()[0].InstanceId)->GetBoneBody();
	UPhysicalMaterial* Original = NewObject<UPhysicalMaterial>(T.Character);
	Original->Friction = 0.15f;
	Original->Density = 2.5f;
	T.Core->SetPhysMaterialOverride(Original);
	UPhysicalMaterial* OriginalBoneOverride = BoneBody->GetBodyInstance()->GetPhysMaterialOverride();
	if (!TestTrue(TEXT("Contact fixture starts"), T.Session->StartPlaying(T.Reason))) { AddError(T.Reason.ToString()); return false; }
	UPhysicalMaterial* Grip = T.Core->GetBodyInstance()->GetPhysMaterialOverride();
	if (!TestNotNull(TEXT("Playing has a contact material"), Grip)) { return false; }
	TestTrue(TEXT("Contact settings use a copy of the original"), Grip != Original);
	TestEqual(TEXT("Dynamic grip comes from the drive profile"), Grip->Friction, T.Executor->DriveProfile->ContactFriction);
	TestEqual(TEXT("Static grip comes from the drive profile"), Grip->StaticFriction, T.Executor->DriveProfile->ContactStaticFriction);
	TestEqual(TEXT("Contact bounce comes from the drive profile"), Grip->Restitution, T.Executor->DriveProfile->ContactRestitution);
	TestTrue(TEXT("Floor friction cannot average away the configured grip"),
		Grip->bOverrideFrictionCombineMode && Grip->FrictionCombineMode == EFrictionCombineMode::Max);
	TestEqual(TEXT("Material copy preserves density"), Grip->Density, Original->Density);
	// Compare both materials on the same initialized dynamic body, not the query-only preview.
	const float GripMass = T.Core->GetMass();
	T.Core->SetPhysMaterialOverride(Original);
	const float OriginalMass = T.Core->GetMass();
	T.Core->SetPhysMaterialOverride(Grip);
	TestTrue(TEXT("Contact grip does not change body mass"), FMath::IsNearlyEqual(GripMass, OriginalMass, 0.001f));
	TestEqual(TEXT("Source material is not edited"), Original->Friction, 0.15f);
	UPhysicalMaterial* BoneGrip = BoneBody->GetBodyInstance()->GetPhysMaterialOverride();
	if (!TestNotNull(TEXT("Bone feet receive contact grip too"), BoneGrip)) { return false; }
	TestEqual(TEXT("Core and bone use the same grip strength"), BoneGrip->Friction, Grip->Friction);
	if (!TestTrue(TEXT("Restart returns to assembly"), T.Session->RestartSession(T.Reason))) { return false; }
	TestTrue(TEXT("Restart restores the precise core override"), T.Core->GetBodyInstance()->GetPhysMaterialOverride() == Original);
	TestTrue(TEXT("Restart restores a bone's original null override"), BoneBody->GetBodyInstance()->GetPhysMaterialOverride() == OriginalBoneOverride);
	if (!TestTrue(TEXT("A second round re-creates contact grip"), T.Session->StartPlaying(T.Reason))) { return false; }
	TestTrue(TEXT("A later round uses a fresh physical material"), T.Core->GetBodyInstance()->GetPhysMaterialOverride() != Grip);
	T.Session->ShutdownSession();
	TestTrue(TEXT("Shutdown restores the borrowed core's material"), T.Core->GetBodyInstance()->GetPhysMaterialOverride() == Original);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyExecutorAsymmetricHingeTest,
	"JGD2026.Assembly.PhysicsExecutor.AsymmetricHingeLimits", ExecutorFlags)

bool FJGDAssemblyExecutorAsymmetricHingeTest::RunTest(const FString& Parameters)
{
	for (const TPair<float, float> Limits : { TPair<float, float>(0.0f, 60.0f), TPair<float, float>(-50.0f, -10.0f) })
	{
		FAssemblyPhysicsExecutorWorld T;
		if (!TestTrue(TEXT("Hinge fixture initializes"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		const FGuid JointId = T.InstallBoneWithJoint();
		if (!TestTrue(TEXT("Physical bone creates a core joint"), JointId.IsValid())
			|| !TestTrue(TEXT("Asymmetric limits are accepted"),
				T.Model->TrySetJointLimits(JointId, Limits.Key, Limits.Value, T.Reason))
			|| !TestTrue(TEXT("Hinge starts physical simulation"), T.Session->StartPlaying(T.Reason)))
		{
			AddError(T.Reason.ToString());
			return false;
		}
		UPhysicsConstraintComponent* Constraint = FJGDAssemblyPhysicsExecutorTestAccessor::GetConstraint(T.Executor, JointId);
		if (!TestNotNull(TEXT("Executor built the hinge"), Constraint)) { return false; }
		const float HalfRange = (Limits.Value - Limits.Key) * 0.5f;
		const float Midpoint = (Limits.Value + Limits.Key) * 0.5f;
		TestTrue(TEXT("Twist span uses half the authored interval"),
			FMath::IsNearlyEqual(Constraint->ConstraintInstance.GetAngularTwistLimit(), HalfRange));
		TestTrue(TEXT("Twist reference shifts to the authored midpoint"),
			FMath::IsNearlyEqual(Constraint->ConstraintInstance.AngularRotationOffset.Roll, -Midpoint));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyExecutorJointBreakTest,
	"JGD2026.Assembly.PhysicsExecutor.NaturalAndRequestedJointBreaks", ExecutorFlags)

bool FJGDAssemblyExecutorJointBreakTest::RunTest(const FString& Parameters)
{
	for (const bool bNaturalBreak : { false, true })
	{
		FAssemblyPhysicsExecutorWorld T;
		if (!TestTrue(TEXT("Joint break fixture initializes"), T.bReady)) { AddError(T.Reason.ToString()); return false; }
		const FGuid JointId = T.InstallBoneWithJoint();
		if (!TestTrue(TEXT("Physical bone creates a breakable joint"), JointId.IsValid())
			|| !TestTrue(TEXT("Configure physical breakability"),
				T.Model->TrySetJointKind(JointId, EAssemblyJointKind::BreakableHinge, T.Reason))
			|| !TestTrue(TEXT("Begin physical round"), T.Session->StartPlaying(T.Reason)))
		{
			AddError(T.Reason.ToString());
			return false;
		}
		UPhysicsConstraintComponent* Constraint = FJGDAssemblyPhysicsExecutorTestAccessor::GetConstraint(T.Executor, JointId);
		if (!TestNotNull(TEXT("Executor built a breakable constraint"), Constraint)) { return false; }
		UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(T.Model);
		if (!TestNotNull(TEXT("Physical confirmation bus is available"), Bus)) { return false; }
		FAssemblyPhysicsJointBrokenMessage Stale;
		Stale.Context = T.Bridge->GetPhysicsContext();
		Stale.JointId = JointId;
		Stale.PhysicsProvider = T.Executor;
		--Stale.Context.Revision;
		Bus->BroadcastMessage(AssemblyMessageTags::JointBroken, FInstancedStruct::Make(Stale));
		Stale.Context = T.Bridge->GetPhysicsContext();
		--Stale.Context.RoundGeneration;
		Bus->BroadcastMessage(AssemblyMessageTags::JointBroken, FInstancedStruct::Make(Stale));
		Stale.Context = T.Bridge->GetPhysicsContext();
		Stale.PhysicsProvider = T.Model;
		Bus->BroadcastMessage(AssemblyMessageTags::JointBroken, FInstancedStruct::Make(Stale));
		T.Bridge->TickComponent(0.016f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Stale revision, round and provider cannot remove a joint"), T.Model->GetJoints().Num(), 1);
		if (bNaturalBreak)
		{
			Constraint->BreakConstraint();
			T.Executor->TickComponent(0.016f, LEVELTICK_All, nullptr);
		}
		else
		{
			TestTrue(TEXT("Player requests a physical break"), T.Model->RequestJointBreak(JointId, T.Reason));
		}
		TestEqual(TEXT("Break confirmation is queued before model update"), T.Model->GetJoints().Num(), 1);
		T.Bridge->TickComponent(0.016f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Both break paths remove the model joint"), T.Model->GetJoints().Num(), 0);
		TestNull(TEXT("Broken constraint is removed from the executor"),
			FJGDAssemblyPhysicsExecutorTestAccessor::GetConstraint(T.Executor, JointId));
		T.Bridge->TickComponent(0.016f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Duplicate bridge tick cannot remove another joint"), T.Model->GetJoints().Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyCircularPivotTest,
	"JGD2026.Assembly.PhysicsExecutor.CircularSurfaceOrbit", ExecutorFlags)

bool FJGDAssemblyCircularPivotTest::RunTest(const FString& Parameters)
{
	FAssemblyPhysicsExecutorWorld T;
	if (!TestTrue(TEXT("Circular articulation fixture initializes"), T.bReady)) return false;
	TArray<FAssemblyBoneDefinition> Definitions = T.Model->GetBoneDefinitions();
	FAssemblyBoneDefinition Round = AAssemblyJointPart::MakeJointPartDefinition(EAssemblyJointPartShape::Round);
	Definitions.Add(Round);
	if (!TestTrue(TEXT("Configure independent round connector"), T.Model->ConfigureFreeAssembly(
		T.Model->GetFreeSettings(), Definitions, T.Model->GetMuscleDefinitions(), T.Reason))) return false;
	const FGuid CoreEdgeId = T.InstallBoneWithJoint();
	if (!TestTrue(TEXT("Install the core-connected bone"), CoreEdgeId.IsValid())) return false;
	const FAssemblyBoneInstance Bone = T.Model->GetBones()[0];
	FNormalBonePhysicalStats Stats;
	FText Reason;
	ANormalBone::CalculateBoneStats(Definitions[0].BoneParameters, Stats, Reason);
	FGuid RoundId;
	if (!TestTrue(TEXT("Place round connector at the far endpoint"),
		T.Interaction->BeginBoneDrag(Round.TypeId, FGuid(), T.Reason)
		&& T.Interaction->UpdateBoneDragAtLocalPosition(Bone.LocalPosition + FVector2D(Stats.Length * 0.5 + Round.JointPartSize.X * 0.5, 0), T.Reason)
		&& T.Interaction->CommitBoneDrag(RoundId, T.Reason))
		|| !TestTrue(TEXT("Start circular articulation simulation"), T.Session->StartPlaying(T.Reason)))
	{
		AddError(T.Reason.ToString()); return false;
	}
	UPrimitiveComponent* RoundBody = Cast<UPrimitiveComponent>(T.Model->GetBonePoseSource(RoundId));
	TestNotNull(TEXT("Round part has its own body"), RoundBody);
	for (const FAssemblyJointInstance& Joint : T.Model->GetJoints())
	{
		UPhysicsConstraintComponent* Constraint = FJGDAssemblyPhysicsExecutorTestAccessor::GetConstraint(T.Executor, Joint.InstanceId);
		if (!TestNotNull(TEXT("Circular connection has a Chaos constraint"), Constraint)) return false;
		const bool bCore = !Joint.BoneA.IsValid() || !Joint.BoneB.IsValid();
		const FVector Center = bCore ? T.Core->GetComponentLocation() : RoundBody->GetComponentLocation();
		TestTrue(bCore ? TEXT("Core hinge pivot is at core center") : TEXT("Round joint hinge pivot is at joint center"),
			Constraint->GetComponentLocation().Equals(Center, 0.001));
		UPrimitiveComponent* A = Joint.BoneA.IsValid() ? Cast<UPrimitiveComponent>(T.Model->GetBonePoseSource(Joint.BoneA)) : T.Core;
		UPrimitiveComponent* B = Cast<UPrimitiveComponent>(T.Model->GetBonePoseSource(Joint.BoneB));
		TestTrue(TEXT("First constraint reference frame resolves to circle center"), A->GetComponentTransform().TransformPosition(
			Constraint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1).GetLocation()).Equals(Center, 0.001));
		TestTrue(TEXT("Second constraint reference frame resolves to the same center"), B->GetComponentTransform().TransformPosition(
			Constraint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2).GetLocation()).Equals(Center, 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyDriveProfileTest,
	"JGD2026.Assembly.PhysicsExecutor.DriveProfile", ExecutorFlags)

bool FJGDAssemblyDriveProfileTest::RunTest(const FString& Parameters)
{
	UJGDAssemblyPhysicsDriveProfile* Profile = NewObject<UJGDAssemblyPhysicsDriveProfile>();
	TestTrue(TEXT("The drive DataAsset defaults are valid"), Profile->HasValidParameters());
	Profile->CoreRotationTorque = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Non-finite core torque is rejected"), Profile->HasValidParameters());
	Profile->CoreRotationTorque = 50000.0f;
	Profile->MuscleContractionRatio = 1.0f;
	TestFalse(TEXT("Out-of-range muscle contraction is rejected"), Profile->HasValidParameters());
	Profile->MuscleContractionRatio = 0.35f;
	Profile->MuscleTransitionTime = -0.1f;
	TestFalse(TEXT("Negative transition time is rejected"), Profile->HasValidParameters());
	Profile->MuscleTransitionTime = 0.18f;
	Profile->MuscleContractionStrength = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Non-finite contraction strength is rejected"), Profile->HasValidParameters());
	Profile->MuscleContractionStrength = 0.5f;
	TestFalse(TEXT("Contraction strength below one is rejected"), Profile->HasValidParameters());
	Profile->MuscleContractionStrength = 4.0f;
	TestFalse(TEXT("Excessive contraction strength is rejected"), Profile->HasValidParameters());
	Profile->MuscleContractionStrength = 1.25f;
	Profile->MuscleMaxForce = -1.0f;
	TestFalse(TEXT("Negative muscle force is rejected"), Profile->HasValidParameters());

	const UJGDPhysicsSystemSettings* Settings = GetDefault<UJGDPhysicsSystemSettings>();
	TestFalse(TEXT("Project settings locate the default drive DataAsset"), Settings->DefaultAssemblyDriveProfile.IsNull());
	const UJGDAssemblyPhysicsDriveProfile* DefaultProfile = Settings->DefaultAssemblyDriveProfile.LoadSynchronous();
	TestNotNull(TEXT("The default drive DataAsset loads"), DefaultProfile);
	if (DefaultProfile)
	{
		TestTrue(TEXT("The default drive DataAsset contains valid parameters"), DefaultProfile->HasValidParameters());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyMuscleRelaxationTest,
	"JGD2026.Assembly.PhysicsExecutor.MuscleRelaxationPush", ExecutorFlags)

bool FJGDAssemblyMuscleRelaxationTest::RunTest(const FString& Parameters)
{
	FAssemblyPhysicsExecutorWorld T;
	if (!TestTrue(TEXT("Muscle physics fixture initializes"), T.bReady))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	auto InstallBoneAt = [&T](FVector2D Position, FGuid& OutId)
	{
		return T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason)
			&& T.Interaction->UpdateBoneDragAtLocalPosition(Position, T.Reason)
			&& T.Interaction->CommitBoneDrag(OutId, T.Reason);
	};
	FGuid BoneA, BoneB, MuscleId, SameBodyMuscleId;
	FVector2D CoreBonePosition;
	if (!TestTrue(TEXT("Resolve a core connection at the configured bone scale"), T.GetCoreBonePlacement(CoreBonePosition)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	if (!TestTrue(TEXT("Install a core-connected bone"), InstallBoneAt(CoreBonePosition, BoneA))
		|| !TestTrue(TEXT("Install a remote bone"), InstallBoneAt(FVector2D(500.0f, 180.0f), BoneB)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	FAssemblyMuscleEndpoint EndpointA, EndpointB, SameBodyEndpoint;
	EndpointA.BoneId = BoneA;
	EndpointA.BoneLocalPoint = FVector2D(20.25f, 1.5f);
	EndpointB.BoneId = BoneB;
	EndpointB.BoneLocalPoint = FVector2D(-20.5f, -2.5f);
	SameBodyEndpoint.BoneId = BoneA;
	SameBodyEndpoint.BoneLocalPoint = FVector2D(-20.0f, -1.5f);
	if (!TestTrue(TEXT("Bind W to the cross-bone muscle"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::W, T.Reason))
		|| !TestTrue(TEXT("Install a cross-bone muscle"), T.Model->TryInstallMuscle(TEXT("Muscle"), EndpointA, EndpointB, MuscleId, T.Reason))
		|| !TestTrue(TEXT("Bind S to a same-body muscle"), T.Model->SelectMuscleKey(EAssemblyMuscleKey::S, T.Reason))
		|| !TestTrue(TEXT("Install a same-body muscle"), T.Model->TryInstallMuscle(TEXT("Muscle"), SameBodyEndpoint, EndpointA, SameBodyMuscleId, T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	UPrimitiveComponent* BodyA = Cast<UPrimitiveComponent>(T.Model->GetBonePoseSource(BoneA));
	UPrimitiveComponent* BodyB = Cast<UPrimitiveComponent>(T.Model->GetBonePoseSource(BoneB));
	if (!TestNotNull(TEXT("First bone has a physics body"), BodyA)
		|| !TestNotNull(TEXT("Second bone has a physics body"), BodyB)) { return false; }
	const FVector InitialA = BodyA->GetComponentTransform().TransformPosition(FVector(EndpointA.BoneLocalPoint.X, 0.0f, EndpointA.BoneLocalPoint.Y));
	const FVector InitialB = BodyB->GetComponentTransform().TransformPosition(FVector(EndpointB.BoneLocalPoint.X, 0.0f, EndpointB.BoneLocalPoint.Y));
	const FVector InitialDirection = (InitialB - InitialA).GetSafeNormal();
	const float InitialLength = FVector::Distance(InitialA, InitialB);
	if (!TestTrue(TEXT("Session starts physics with both muscles installed"), T.Session->StartPlaying(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Natural length is captured before the first key press"),
		FMath::IsNearlyEqual(FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleRestLength(T.Executor, MuscleId), InitialLength, 0.01f));
	TestEqual(TEXT("A same-body muscle has no physical rest drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleRestCount(T.Executor), 1);
	TestTrue(TEXT("Passive support exists before any input"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasMuscleDrive(T.Executor, MuscleId)
		&& !FJGDAssemblyPhysicsExecutorTestAccessor::IsMuscleContracting(T.Executor, MuscleId));
	TestTrue(TEXT("An idle muscle pulls an overextended structure back"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength * 1.1f) < 0);
	TestTrue(TEXT("An idle muscle pushes a compressed structure back"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength * 0.9f) > 0);
	TestTrue(TEXT("Idle damping opposes separating velocity at the authored length"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength, 20) < 0);
	TestTrue(TEXT("Idle damping opposes closing velocity at the authored length"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength, -20) > 0);
	BodyB->SetWorldLocation(BodyB->GetComponentLocation() - InitialDirection * (InitialLength * 0.2f),
		false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Press creates a contracting drive"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("Cross-bone drive contracts"), FJGDAssemblyPhysicsExecutorTestAccessor::IsMuscleContracting(T.Executor, MuscleId));
	UJGDAssemblyPhysicsDriveProfile* StrengthProfile = DuplicateObject<UJGDAssemblyPhysicsDriveProfile>(T.Executor->DriveProfile.Get(), T.Executor);
	T.Executor->DriveProfile = StrengthProfile;
	StrengthProfile->MuscleContractionStrength = 1.0f;
	const float BasePull = FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength + 0.1f);
	const float BaseStepPull = FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength + 0.1f, 0, 1.0f / 60.0f);
	StrengthProfile->MuscleContractionStrength = 1.25f;
	TestTrue(TEXT("Active spring strength rises by 25 percent before stabilization"),
		FMath::IsNearlyEqual(FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength + 0.1f), BasePull * 1.25f, 0.1f));
	TestTrue(TEXT("Stabilized per-frame contraction also becomes stronger"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength + 0.1f, 0, 1.0f / 60.0f) < BaseStepPull);
	TestTrue(TEXT("Active force limit rises by 25 percent"),
		FMath::IsNearlyEqual(FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength * 1000), -StrengthProfile->MuscleMaxForce * 1.25f, 0.1f));
	TestTrue(TEXT("Moving a bone before the first press does not redefine natural length"),
		FMath::IsNearlyEqual(FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleRestLength(T.Executor, MuscleId), InitialLength, 0.01f));
	TestTrue(TEXT("A same-body press is accepted by the model"), T.Model->SetMuscleInput(EAssemblyMuscleKey::S, true));
	TestFalse(TEXT("A same-body muscle cannot drive one rigid body"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasMuscleDrive(T.Executor, SameBodyMuscleId));

	BodyA->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyB->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyA->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyB->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyA->SetEnableGravity(false);
	BodyB->SetEnableGravity(false);
	BodyA->SetPhysicsLinearVelocity(FVector::ZeroVector);
	BodyB->SetPhysicsLinearVelocity(FVector::ZeroVector);
	auto Anchor = [](const UPrimitiveComponent* Body, const FAssemblyMuscleEndpoint& Endpoint)
	{
		return Body->GetComponentTransform().TransformPosition(FVector(Endpoint.BoneLocalPoint.X, 0.0f, Endpoint.BoneLocalPoint.Y));
	};
	auto MoveSecondAnchorTo = [&](FVector Desired)
	{
		const FVector Offset = Desired - Anchor(BodyB, EndpointB);
		BodyB->SetWorldLocation(BodyB->GetComponentLocation() + Offset, false, nullptr, ETeleportType::TeleportPhysics);
		BodyA->SetPhysicsLinearVelocity(FVector::ZeroVector);
		BodyB->SetPhysicsLinearVelocity(FVector::ZeroVector);
	};
	MoveSecondAnchorTo(Anchor(BodyA, EndpointA) + InitialDirection * (InitialLength * 0.85f));
	const float ShortLength = FVector::Distance(Anchor(BodyA, EndpointA), Anchor(BodyB, EndpointB));
	TestTrue(TEXT("Test bones are shorter than the original muscle length"), ShortLength < InitialLength * 0.95f);
	TestTrue(TEXT("Release requests physical relaxation"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, false));
	const float PassiveForce = FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength + 0.1f);
	StrengthProfile->MuscleContractionStrength = 1.0f;
	TestEqual(TEXT("Released support ignores the active strength multiplier"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, InitialLength + 0.1f), PassiveForce);
	StrengthProfile->MuscleContractionStrength = 1.25f;
	for (int32 Step = 0; Step < 100; ++Step) FJGDAssemblyPhysicsExecutorTestAccessor::ApplyMuscleDrives(T.Executor);
	TestTrue(TEXT("Release retains a drive until the original length is reached"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasMuscleDrive(T.Executor, MuscleId)
		&& !FJGDAssemblyPhysicsExecutorTestAccessor::IsMuscleContracting(T.Executor, MuscleId));
	TestTrue(TEXT("Relaxation commands an outward force on the second bone"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, ShortLength) > 0.0f);
	FJGDAssemblyPhysicsExecutorTestAccessor::ApplyMuscleDrives(T.Executor);
	TestTrue(TEXT("Relaxation stays active below the original length"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasMuscleDrive(T.Executor, MuscleId));
	TestTrue(TEXT("Press during relaxation switches back to contraction"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	for (int32 Step = 0; Step < 100; ++Step) FJGDAssemblyPhysicsExecutorTestAccessor::ApplyMuscleDrives(T.Executor);
	TestTrue(TEXT("Re-press reuses the same original length"),
		FJGDAssemblyPhysicsExecutorTestAccessor::IsMuscleContracting(T.Executor, MuscleId)
		&& FMath::IsNearlyEqual(FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleRestLength(T.Executor, MuscleId), InitialLength, 0.01f));
	TestTrue(TEXT("Contraction commands an inward force on the second bone"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, ShortLength) < 0.0f);
	MoveSecondAnchorTo(Anchor(BodyA, EndpointA));
	TestTrue(TEXT("A second release resumes relaxation"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, false));
	FJGDAssemblyPhysicsExecutorTestAccessor::ApplyMuscleDrives(T.Executor);
	TestTrue(TEXT("A coincident endpoint pair retains its last valid push direction"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleDirection(T.Executor, MuscleId).Equals(InitialDirection, 0.01f));
	TestTrue(TEXT("A fully shortened muscle still commands outward force"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, 0.0f) > 0.0f);
	MoveSecondAnchorTo(Anchor(BodyA, EndpointA) + InitialDirection * InitialLength);
	FJGDAssemblyPhysicsExecutorTestAccessor::ApplyMuscleDrives(T.Executor);
	TestTrue(TEXT("Drive keeps supporting the authored length after release"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasMuscleDrive(T.Executor, MuscleId));

	FAssemblyPhysicsMuscleDriveMessage StaleRequest;
	StaleRequest.Context = T.Bridge->GetPhysicsContext();
	--StaleRequest.Context.RoundGeneration;
	StaleRequest.Phase = EAssemblyPhase::Playing;
	StaleRequest.bContracting = true;
	T.Model->GetMuscle(MuscleId, StaleRequest.Muscle);
	StaleRequest.EndpointBoneA.Body = BodyA;
	StaleRequest.EndpointBoneB.Body = BodyB;
	UJGDMessageSubsystem::Get(T.Model)->BroadcastMessage(
		AssemblyMessageTags::MuscleDriveRequested, FInstancedStruct::Make(StaleRequest));
	TestFalse(TEXT("An old-round press cannot change passive support into contraction"),
		FJGDAssemblyPhysicsExecutorTestAccessor::IsMuscleContracting(T.Executor, MuscleId));
	MoveSecondAnchorTo(Anchor(BodyA, EndpointA));
	TestTrue(TEXT("A current press can create another drive"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, true));
	TestTrue(TEXT("A first press at coincident endpoints uses the original direction"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleDirection(T.Executor, MuscleId).Equals(InitialDirection, 0.01f));
	TestTrue(TEXT("Release after a coincident press commands outward force"), T.Model->SetMuscleInput(EAssemblyMuscleKey::W, false)
		&& FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleForceOnB(T.Executor, MuscleId, 0.0f) > 0.0f);
	BodyB->SetSimulatePhysics(false);
	FJGDAssemblyPhysicsExecutorTestAccessor::ApplyMuscleDrives(T.Executor);
	TestFalse(TEXT("Invalid physical bodies discard their drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasMuscleDrive(T.Executor, MuscleId));
	if (!TestTrue(TEXT("Restart clears physical state"), T.Session->RestartSession(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TestEqual(TEXT("Restart clears original muscle lengths"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetMuscleRestCount(T.Executor), 0);
	TestFalse(TEXT("Restart leaves no muscle drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasMuscleDrive(T.Executor, MuscleId));
	TestEqual(TEXT("Playing restart preserves authored bones"), T.Model->GetBones().Num(), 2);
	TestEqual(TEXT("Playing restart preserves authored muscles"), T.Model->GetMuscles().Num(), 2);
	TestTrue(TEXT("Begin another shelf preview before clearing assembly"),
		T.Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), T.Reason));
	TestTrue(TEXT("Assembly restart clears the authored build"), T.Session->RestartSession(T.Reason));
	TestEqual(TEXT("Fresh assembly has no installed rigid parts"), T.Model->GetBones().Num(), 0);
	TestEqual(TEXT("Fresh assembly has no installed muscles"), T.Model->GetMuscles().Num(), 0);
	TestEqual(TEXT("Fresh assembly has no connections"), T.Model->GetJoints().Num(), 0);
	TestFalse(TEXT("Fresh assembly cancels the drag preview"), T.Interaction->IsBoneDragActive());
	for (const FAssemblyBoneDefinition& Definition : T.Model->GetBoneDefinitions())
		TestEqual(TEXT("Assembly restart refunds rigid stock"), T.Model->GetRemainingQuantity(Definition.TypeId), Definition.InitialQuantity);
	for (const FAssemblyMuscleDefinition& Definition : T.Model->GetMuscleDefinitions())
		TestEqual(TEXT("Assembly restart refunds muscle stock"), T.Model->GetRemainingQuantity(Definition.TypeId), Definition.InitialQuantity);
	TestTrue(TEXT("Repeated empty assembly restart succeeds"), T.Session->RestartSession(T.Reason));
	TestFalse(TEXT("Fresh assembly core stays non-simulating"), T.Core->IsSimulatingPhysics());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyExecutorCoreRotationTest,
	"JGD2026.Assembly.PhysicsExecutor.CoreRotationMessageFilteringAndDrive",
	ExecutorFlags)

bool FJGDAssemblyExecutorCoreRotationTest::RunTest(const FString& Parameters)
{
	FAssemblyPhysicsExecutorWorld T;
	if (!TestTrue(TEXT("Native Session/Bridge/Executor fixture initializes without a map"), T.bReady))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(T.Model);
	if (!TestNotNull(TEXT("Message bus is available"), Bus)) { return false; }

	TestTrue(TEXT("Assembly core press message is broadcast"), T.BroadcastRotation(true, EAssemblyPhase::Assembly, FVector::YAxisVector));
	T.Executor->ApplyCoreRotationDrive(0.025f);
	T.Step();
	TestFalse(TEXT("Assembly ignores a new rotation press"), T.Core->IsSimulatingPhysics());
	TestTrue(TEXT("Assembly has no angular velocity"), T.Core->GetPhysicsAngularVelocityInDegrees().IsNearlyZero());
	TestFalse(TEXT("Assembly has no cached core drive"), FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	if (!TestTrue(TEXT("Session commits Playing through PR lifecycle"), T.Session->StartPlaying(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Playing simulates the core"), T.Core->IsSimulatingPhysics());

	T.Core->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	TestTrue(TEXT("Current Playing core rotation press is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector(0.0f, -4.0f, 0.0f)));
	TestTrue(TEXT("A current Playing press is accepted into the independent drive input"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	TestTrue(TEXT("Drive caches the supplied normalized world axis"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetCoreDriveAxis(T.Executor).Equals(-FVector::YAxisVector));
	TestTrue(TEXT("Non-Playing true message is ignored without overwriting a valid drive"),
		T.BroadcastRotation(true, EAssemblyPhase::Assembly, FVector::YAxisVector));
	TestTrue(TEXT("Rejected non-Playing press preserves only the prior accepted state"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetCoreDriveAxis(T.Executor).Equals(-FVector::YAxisVector));
	T.Executor->ApplyCoreRotationDrive(0.025f);
	T.Step();
	TestTrue(TEXT("Torque application keeps the current drive active"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	TestTrue(TEXT("Legal stop is broadcast in the current context"),
		T.BroadcastRotation(false, EAssemblyPhase::Assembly, FVector::ZeroVector));
	TestFalse(TEXT("Stop immediately clears the cached drive and axis"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	TestTrue(TEXT("Stop clears the cached axis"),
		FJGDAssemblyPhysicsExecutorTestAccessor::GetCoreDriveAxis(T.Executor).IsNearlyZero());

	FAssemblyPhysicsContext ForeignSource = T.Bridge->GetPhysicsContext();
	ForeignSource.SourceId = FGuid::NewGuid();
	TestTrue(TEXT("Foreign SourceId message is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::YAxisVector, ForeignSource));
	T.Executor->ApplyCoreRotationDrive(0.025f);
	T.Step();
	TestFalse(TEXT("Foreign SourceId cannot drive the current core"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	FAssemblyPhysicsContext ForeignWorld = T.Bridge->GetPhysicsContext();
	ForeignWorld.World = nullptr;
	TestTrue(TEXT("Foreign World message is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::YAxisVector, ForeignWorld));
	TestFalse(TEXT("Foreign World cannot drive the current core"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	FAssemblyPhysicsContext ForeignAssembly = T.Bridge->GetPhysicsContext();
	ForeignAssembly.Assembly = nullptr;
	TestTrue(TEXT("Foreign Assembly message is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::YAxisVector, ForeignAssembly));
	TestFalse(TEXT("Foreign Assembly cannot drive the current core"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	FAssemblyPhysicsContext OldRound = T.Bridge->GetPhysicsContext();
	--OldRound.RoundGeneration;
	TestTrue(TEXT("Old round message is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::YAxisVector, OldRound));
	TestFalse(TEXT("Old round cannot drive the current core"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	FAssemblyPhysicsContext OldRevision = T.Bridge->GetPhysicsContext();
	--OldRevision.Revision;
	TestTrue(TEXT("Stale revision message is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::YAxisVector, OldRevision));
	T.Executor->ApplyCoreRotationDrive(0.025f);
	T.Step();
	TestFalse(TEXT("Stale revision cannot create a drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	TestTrue(TEXT("Zero-axis Playing request is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::ZeroVector));
	TestFalse(TEXT("Zero axis is rejected without retaining a drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	TestTrue(TEXT("Non-finite-axis Playing request is broadcast"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing,
			FVector(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f)));
	TestFalse(TEXT("Non-finite axis is rejected without retaining a drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	if (!TestTrue(TEXT("Return the initial Playing transaction to Assembly before repeated rounds"),
		T.Session->RestartSession(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	for (int32 Round = 0; Round < 3; ++Round)
	{
		if (!TestTrue(TEXT("Each restart round enters Playing"), T.Session->StartPlaying(T.Reason)))
		{
			AddError(T.Reason.ToString());
			return false;
		}
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::YAxisVector);
		T.Executor->ApplyCoreRotationDrive(0.025f);
		TestTrue(TEXT("Each round accepts the current held drive"),
			FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
		if (!TestTrue(TEXT("Restart stops physics before clearing the round"), T.Session->RestartSession(T.Reason)))
		{
			AddError(T.Reason.ToString());
			return false;
		}
		TestFalse(TEXT("Restart returns the core to non-simulating Assembly"), T.Core->IsSimulatingPhysics());
		TestFalse(TEXT("Restart disables gravity while editing"), T.Core->IsGravityEnabled());
		TestTrue(TEXT("Restart clears angular velocity"), T.Core->GetPhysicsAngularVelocityInDegrees().IsNearlyZero());
		TestFalse(TEXT("Restart clears any held core drive"), FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	}

	T.Session->ShutdownSession();
	TestFalse(TEXT("Reusable Session shutdown leaves the core stopped"), T.Core->IsSimulatingPhysics());
	TestFalse(TEXT("Reusable Session shutdown leaves Assembly gravity disabled"), T.Core->IsGravityEnabled());
	TestTrue(TEXT("Reusable Session shutdown preserves the single provider binding"),
		T.Bridge->GetPhysicsLifecycleProvider() == T.Executor);
	if (!TestTrue(TEXT("The same Session can initialize again after a physical shutdown"),
		T.Session->InitializeSession(T.Controller, T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	if (!TestTrue(TEXT("Reinitialized Session can start Playing through the same Executor"),
		T.Session->StartPlaying(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TestTrue(TEXT("Reinitialized Session reactivates the core"), T.Core->IsSimulatingPhysics());
	T.Session->ShutdownSession();
	TestFalse(TEXT("Final Session shutdown removes simulation and held force"), T.Core->IsSimulatingPhysics());
	TestFalse(TEXT("Final Session shutdown leaves no core drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDAssemblyPhysicsRealPlayerBootstrapTest,
	"JGD2026.Assembly.PhysicsExecutor.RealPlayerBootstrap", ExecutorFlags)

bool FJGDAssemblyPhysicsRealPlayerBootstrapTest::RunTest(const FString& Parameters)
{
	FAssemblyPhysicsExecutorWorld T(true);
	if (!TestTrue(TEXT("BP_Player establishes a real Session, Bridge and physics executor"), T.bReady))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TArray<UJGDAssemblyPhysicsExecutorComponent*> Executors;
	T.Character->GetComponents(Executors);
	TestEqual(TEXT("The actual player owns exactly one executor"), Executors.Num(), 1);
	TestTrue(TEXT("The player requires a physical lifecycle provider"), T.Bridge->bRequirePhysicsLifecycleProvider);
	TestTrue(TEXT("The sole executor is the active provider"), T.Bridge->GetPhysicsLifecycleProvider() == T.Executor);
	TestNotNull(TEXT("Runtime-created player executor resolves a drive DataAsset"), T.Executor->DriveProfile.Get());
	TestTrue(TEXT("Runtime player uses the configured default drive DataAsset"),
		T.Executor->DriveProfile.Get() == GetDefault<UJGDPhysicsSystemSettings>()->DefaultAssemblyDriveProfile.LoadSynchronous());
	UPrimitiveComponent* CoreBody = T.Interaction->GetCoreBody();
	if (!TestNotNull(TEXT("The real player supplies a core body"), CoreBody))
	{
		return false;
	}
	if (!TestTrue(TEXT("The actual player enters Playing through its executor"), T.Session->StartPlaying(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TestTrue(TEXT("The actual player core simulates in Playing"), CoreBody->IsSimulatingPhysics());
	TestTrue(TEXT("The actual player receives a core rotation request"),
		T.BroadcastRotation(true, EAssemblyPhase::Playing, FVector::YAxisVector));
	TestTrue(TEXT("The actual player's executor accepts the held drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	TestTrue(TEXT("The actual player receives a release request"),
		T.BroadcastRotation(false, EAssemblyPhase::Playing, FVector::ZeroVector));
	TestFalse(TEXT("Release clears the actual player's drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	if (!TestTrue(TEXT("Restart returns the actual player to Assembly"), T.Session->RestartSession(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TestFalse(TEXT("The actual player core stops simulating in Assembly"), CoreBody->IsSimulatingPhysics());
	TestFalse(TEXT("Restart leaves no held drive on the actual player"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	T.Session->ShutdownSession();
	if (!TestTrue(TEXT("The actual player Session reinitializes"), T.Session->InitializeSession(T.Controller, T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	Executors.Reset();
	T.Character->GetComponents(Executors);
	TestEqual(TEXT("Reinitialization never creates a second executor"), Executors.Num(), 1);
	TestTrue(TEXT("Reinitialization keeps the executor bound"), T.Bridge->GetPhysicsLifecycleProvider() == T.Executor);
	if (!TestTrue(TEXT("The reinitialized real player starts Playing through the same executor"),
		T.Session->StartPlaying(T.Reason)))
	{
		AddError(T.Reason.ToString());
		return false;
	}
	TestTrue(TEXT("The reinitialized real player's core simulates"), CoreBody->IsSimulatingPhysics());
	T.Session->ShutdownSession();
	TestFalse(TEXT("Shutdown stops the real player's core"), CoreBody->IsSimulatingPhysics());
	TestFalse(TEXT("Shutdown clears the real player's held drive"),
		FJGDAssemblyPhysicsExecutorTestAccessor::HasCoreDrive(T.Executor));
	return true;
}

#endif
