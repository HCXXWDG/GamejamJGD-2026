#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblySessionComponent.h"
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

		FGuid InstallBoneWithJoint()
		{
			FGuid BoneId;
			if (!Interaction->BeginBoneDrag(TEXT("Bone"), FGuid(), Reason)
				|| !Interaction->UpdateBoneDragAtLocalPosition(FVector2D(96.0, 0.0), Reason)
				|| !Interaction->CommitBoneDrag(BoneId, Reason))
			{
				return FGuid();
			}
			const TArray<FAssemblyJointInstance> Joints = Model->GetJoints();
			return Joints.Num() == 1 ? Joints[0].InstanceId : FGuid();
		}
	};
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
