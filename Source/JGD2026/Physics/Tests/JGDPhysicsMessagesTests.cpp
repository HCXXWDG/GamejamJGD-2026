// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Character/JGDWindTestCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Physics/JGD2DPhysicsParticipantComponent.h"
#include "Physics/JGDPhysicsMessages.h"
#include "Physics/JGDPhysicsProfileVolume.h"
#include "Physics/JGDPhysicsWorldSubsystem.h"
#include "Physics/JGDPhysicsSystemSettings.h"
#include "Physics/Tests/JGDPhysicsMessageTestReceiver.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace JGDPhysicsMessageTests
{
	struct FTestWorld
	{
		TStrongObjectPtr<UGameInstance> GameInstance{NewObject<UGameInstance>(GEngine)};
		UWorld* World;
		UJGDPhysicsWorldSubsystem* Physics;
		UJGDMessageSubsystem* Bus;
		TSoftObjectPtr<UJGDPhysicsWorldProfile> OriginalDefaultWorldProfile;

		FTestWorld()
		{
			UJGDPhysicsSystemSettings* Settings = GetMutableDefault<UJGDPhysicsSystemSettings>();
			OriginalDefaultWorldProfile = Settings->DefaultWorldProfile;
			// Keep transient tests independent of an optional project content asset.
			Settings->DefaultWorldProfile.Reset();
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
			Physics = World->GetSubsystem<UJGDPhysicsWorldSubsystem>();
			Bus = GameInstance->GetSubsystem<UJGDMessageSubsystem>();
		}

		void BeginPlay()
		{
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			GEngine->DestroyWorldContext(World);
			GetMutableDefault<UJGDPhysicsSystemSettings>()->DefaultWorldProfile = OriginalDefaultWorldProfile;
		}

		ACharacter* SpawnCharacter()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform::Identity, Params);
		}

		void ApplyWorld(UJGDPhysicsWorldProfile* Profile, UWorld* Target = nullptr)
		{
			FJGDApplyWorldPhysicsProfileRequest Request;
			Request.TargetWorld = Target ? Target : World;
			Request.WorldProfile = Profile;
			Bus->BroadcastMessage(JGDPhysicsMessageTags::ApplyWorldProfile(), FInstancedStruct::Make(Request));
		}

		void SetBase(ACharacter* Character, UJGDCharacterPhysicsProfile* Profile)
		{
			FJGDSetCharacterBasePhysicsProfileRequest Request;
			Request.Character = Character;
			Request.BaseProfile = Profile;
			Bus->BroadcastMessage(JGDPhysicsMessageTags::SetCharacterBaseProfile(), FInstancedStruct::Make(Request));
		}
	};

	FJGDMessageListenerHandle Listen(UJGDMessageSubsystem* Bus, FGameplayTag Tag, UJGDPhysicsMessageTestReceiver* Receiver)
	{
		FJGDMessageReceived Delegate;
		Delegate.BindDynamic(Receiver, &UJGDPhysicsMessageTestReceiver::Receive);
		return Bus->RegisterListener(Tag, Delegate);
	}

	UJGDCharacterPhysicsProfile* MakeCharacterProfile(UObject* Outer, float Speed)
	{
		auto* Profile = NewObject<UJGDCharacterPhysicsProfile>(Outer);
		Profile->Settings.MaxWalkSpeed = Speed;
		Profile->Settings.bSnapToPlaneWhenApplied = false;
		return Profile;
	}
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDPhysicsMessageRequestsTest, "JGD2026.Physics.Messages.RequestsAndFallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDPhysicsMessageRequestsTest::RunTest(const FString& Parameters)
{
	JGDPhysicsMessageTests::FTestWorld Test;
	AWorldSettings* Settings = Test.World->GetWorldSettings();
	Settings->bGlobalGravitySet = true;
	Settings->GlobalGravityZ = -735.0f;
	Settings->bWorldGravitySet = false;
	Test.BeginPlay();
	TestTrue(TEXT("Real gameplay world began play"), Test.World->HasBegunPlay());

	auto* WorldProfile = NewObject<UJGDPhysicsWorldProfile>(Test.World);
	WorldProfile->bOverrideWorldGravity = true;
	WorldProfile->WorldGravityZ = -200.0f;
	WorldProfile->DefaultCharacterProfile = JGDPhysicsMessageTests::MakeCharacterProfile(WorldProfile, 321.0f);
	Test.ApplyWorld(WorldProfile);
	TestEqual(TEXT("World message applies gravity"), Settings->GetGravityZ(), -200.0f);

	ACharacter* Character = Test.SpawnCharacter();
	auto* Participant = NewObject<UJGD2DPhysicsParticipantComponent>(Character);
	Character->AddInstanceComponent(Participant);
	Participant->RegisterComponent();
	TestEqual(TEXT("Participant registers using world default"), Character->GetCharacterMovement()->MaxWalkSpeed, 321.0f);
	auto* Base = JGDPhysicsMessageTests::MakeCharacterProfile(Test.World, 456.0f);
	Test.SetBase(Character, Base);
	TestEqual(TEXT("Request synchronizes Participant value"), Participant->GetBaseProfile(), Base);
	TestEqual(TEXT("Base applies"), Character->GetCharacterMovement()->MaxWalkSpeed, 456.0f);

	auto* Volume = Test.World->SpawnActor<AJGDPhysicsProfileVolume>();
	Volume->Priority = 10;
	Volume->CharacterProfile = JGDPhysicsMessageTests::MakeCharacterProfile(Volume, 789.0f);
	auto* LowerVolume = Test.World->SpawnActor<AJGDPhysicsProfileVolume>();
	LowerVolume->Priority = 1;
	LowerVolume->CharacterProfile = JGDPhysicsMessageTests::MakeCharacterProfile(LowerVolume, 654.0f);
	Test.Physics->NotifyCharacterEnteredVolume(Character, Volume);
	Test.Physics->NotifyCharacterEnteredVolume(Character, LowerVolume);
	TestEqual(TEXT("Highest priority volume wins"), Character->GetCharacterMovement()->MaxWalkSpeed, 789.0f);
	Test.SetBase(Character, nullptr);
	TestNull(TEXT("None clears Participant base"), Participant->GetBaseProfile());
	TestEqual(TEXT("Volume still overrides a cleared base"), Character->GetCharacterMovement()->MaxWalkSpeed, 789.0f);
	Test.Physics->NotifyCharacterLeftVolume(Character, Volume);
	TestEqual(TEXT("Lower priority volume is restored"), Character->GetCharacterMovement()->MaxWalkSpeed, 654.0f);
	Test.Physics->NotifyVolumeRemoved(LowerVolume);
	TestEqual(TEXT("Leaving all volumes restores world default"), Character->GetCharacterMovement()->MaxWalkSpeed, 321.0f);
	Test.ApplyWorld(nullptr);
	TestEqual(TEXT("None restores captured gravity"), Settings->GetGravityZ(), -735.0f);
	TestEqual(TEXT("None world uses built-in character defaults"), Character->GetCharacterMovement()->MaxWalkSpeed,
		FJGDCharacterPhysicsSettings().MaxWalkSpeed);
	TestNull(TEXT("Resolved profile is None for built-in defaults"), Test.Physics->GetResolvedCharacterProfile(Character));

	ACharacter* WithoutParticipant = Test.SpawnCharacter();
	Test.SetBase(WithoutParticipant, Base);
	TestEqual(TEXT("Request auto-registers characters without a component"), WithoutParticipant->GetCharacterMovement()->MaxWalkSpeed, 456.0f);
	Test.SetBase(WithoutParticipant, nullptr);
	TestEqual(TEXT("Clearing a component-free base falls back"), WithoutParticipant->GetCharacterMovement()->MaxWalkSpeed,
		FJGDCharacterPhysicsSettings().MaxWalkSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDRigidBodyRegistrationAndVolumeRestoreTest,
	"JGD2026.Physics.RigidBodyRegistration.VolumeRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDRigidBodyRegistrationAndVolumeRestoreTest::RunTest(const FString& Parameters)
{
	JGDPhysicsMessageTests::FTestWorld Test;
	Test.BeginPlay();
	auto MakeRigidProfile = [this](UObject* Outer, bool bGravity, float Linear, float Angular)
	{
		UJGDPhysicsRigidBodyProfile* Profile = NewObject<UJGDPhysicsRigidBodyProfile>(Outer);
		Profile->bEnableGravity = bGravity;
		Profile->LinearDamping = Linear;
		Profile->AngularDamping = Angular;
		return Profile;
	};
	UJGDPhysicsWorldProfile* WorldProfile = NewObject<UJGDPhysicsWorldProfile>(Test.World);
	WorldProfile->DefaultRigidBodyProfile = MakeRigidProfile(WorldProfile, true, 0.5f, 0.25f);
	Test.ApplyWorld(WorldProfile);

	AActor* Owner = Test.World->SpawnActor<AActor>();
	TestNotNull(TEXT("Rigid-body owner spawned"), Owner);
	if (!Owner)
	{
		return false;
	}
	UBoxComponent* Body = NewObject<UBoxComponent>(Owner);
	Owner->SetRootComponent(Body);
	Owner->AddInstanceComponent(Body);
	Body->SetMobility(EComponentMobility::Movable);
	Body->RegisterComponent();
	Body->SetEnableGravity(false);
	Body->SetLinearDamping(4.0f);
	Body->SetAngularDamping(3.0f);

	const FGuid RegistrationId = FGuid::NewGuid();
	FText Reason;
	TestTrue(TEXT("Body registers with a unique owner token"),
		Test.Physics->RegisterRigidBody(Body, RegistrationId, nullptr, Reason));
	TestTrue(TEXT("World rigid-body default applies gravity"), Body->IsGravityEnabled());
	TestEqual(TEXT("World profile linear damping applies"), Body->GetLinearDamping(), 0.5f);
	TestEqual(TEXT("World profile angular damping applies"), Body->GetAngularDamping(), 0.25f);

	AJGDPhysicsProfileVolume* Volume = Test.World->SpawnActor<AJGDPhysicsProfileVolume>();
	Volume->Priority = 25;
	Volume->RigidBodyProfile = MakeRigidProfile(Volume, false, 7.0f, 6.0f);
	Test.Physics->NotifyRigidBodiesEnteredVolume(Owner, Volume);
	TestFalse(TEXT("Selected volume overrides gravity"), Body->IsGravityEnabled());
	TestEqual(TEXT("Selected volume overrides linear damping"), Body->GetLinearDamping(), 7.0f);

	Test.Physics->NotifyRigidBodiesLeftVolume(Owner, Volume);
	TestTrue(TEXT("Leaving volume returns to world gravity"), Body->IsGravityEnabled());
	TestEqual(TEXT("Leaving volume returns to world damping"), Body->GetLinearDamping(), 0.5f);

	Test.Physics->NotifyRigidBodiesEnteredVolume(Owner, Volume);
	Test.Physics->UnregisterRigidBody(Body, FGuid::NewGuid());
	TestTrue(TEXT("Wrong owner token cannot unregister another owner"), Test.Physics->IsRigidBodyRegistered(Body));
	Test.Physics->UnregisterRigidBody(Body, RegistrationId);
	TestFalse(TEXT("Matching owner token unregisters the body"), Test.Physics->IsRigidBodyRegistered(Body));
	TestFalse(TEXT("Unregister restores original gravity"), Body->IsGravityEnabled());
	TestEqual(TEXT("Unregister restores original linear damping"), Body->GetLinearDamping(), 4.0f);
	TestEqual(TEXT("Unregister restores original angular damping"), Body->GetAngularDamping(), 3.0f);

	const FGuid ReRegistrationId = FGuid::NewGuid();
	TestTrue(TEXT("Re-registration succeeds while owner remains in volume"),
		Test.Physics->RegisterRigidBody(Body, ReRegistrationId, nullptr, Reason));
	TestTrue(TEXT("Re-registration resolves the world profile before volume notification"), Body->IsGravityEnabled());
	Test.Physics->NotifyRigidBodiesEnteredVolume(Owner, Volume);
	TestFalse(TEXT("Re-entering registered region applies volume gravity"), Body->IsGravityEnabled());
	TestEqual(TEXT("Re-entering registered region applies volume damping"), Body->GetLinearDamping(), 7.0f);
	Test.Physics->UnregisterRigidBodiesOwnedBy(ReRegistrationId);

	const FGuid MessageRegistrationId = FGuid::NewGuid();
	FJGDRegisterRigidBodyPhysicsRequest RegisterRequest;
	RegisterRequest.TargetWorld = Test.World;
	RegisterRequest.Body = Body;
	RegisterRequest.RegistrationId = MessageRegistrationId;
	Test.Bus->BroadcastMessage(JGDPhysicsMessageTags::RegisterRigidBody(), FInstancedStruct::Make(RegisterRequest));
	TestTrue(TEXT("Generic bus registers a primitive rigid body"), Test.Physics->IsRigidBodyRegistered(Body));
	UJGDPhysicsRigidBodyProfile* MessageProfile = MakeRigidProfile(Test.World, false, 9.0f, 8.0f);
	FJGDSetRigidBodyBasePhysicsProfileRequest SetBaseRequest;
	SetBaseRequest.TargetWorld = Test.World;
	SetBaseRequest.Body = Body;
	SetBaseRequest.BaseProfile = MessageProfile;
	Test.Bus->BroadcastMessage(JGDPhysicsMessageTags::SetRigidBodyBaseProfile(), FInstancedStruct::Make(SetBaseRequest));
	TestFalse(TEXT("Generic bus applies a body base profile"), Body->IsGravityEnabled());
	TestEqual(TEXT("Generic bus applies body linear damping"), Body->GetLinearDamping(), 9.0f);
	FJGDUnregisterRigidBodyPhysicsRequest UnregisterRequest;
	UnregisterRequest.TargetWorld = Test.World;
	UnregisterRequest.Body = Body;
	UnregisterRequest.RegistrationId = MessageRegistrationId;
	Test.Bus->BroadcastMessage(JGDPhysicsMessageTags::UnregisterRigidBody(), FInstancedStruct::Make(UnregisterRequest));
	TestFalse(TEXT("Generic bus unregisters a primitive rigid body"), Test.Physics->IsRigidBodyRegistered(Body));
	TestFalse(TEXT("Generic bus unregister restores original gravity"), Body->IsGravityEnabled());
	TestEqual(TEXT("Generic bus unregister restores original damping"), Body->GetLinearDamping(), 4.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDPhysicsMessageNotificationsTest, "JGD2026.Physics.Messages.Notifications",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDPhysicsMessageNotificationsTest::RunTest(const FString& Parameters)
{
	JGDPhysicsMessageTests::FTestWorld Test;
	TStrongObjectPtr<UJGDPhysicsMessageTestReceiver> Receiver(NewObject<UJGDPhysicsMessageTestReceiver>());
	int32 WorldCount = 0, CharacterCount = 0, LegacyCount = 0, ParentCount = 0;
	ACharacter* Character = nullptr;
	Receiver->OnMessage = [&](FGameplayTag Channel, const FInstancedStruct& Message)
	{
		if (const auto* WorldMessage = Message.GetPtr<FJGDWorldPhysicsProfileChangedMessage>())
		{
			++WorldCount;
			TestEqual(TEXT("World notification identifies world"), WorldMessage->World.Get(), Test.World);
			if (Character && WorldMessage->NewProfile && WorldMessage->NewProfile->DefaultCharacterProfile)
			{
				TestEqual(TEXT("World notification follows character refresh"), Character->GetCharacterMovement()->MaxWalkSpeed,
					WorldMessage->NewProfile->DefaultCharacterProfile->Settings.MaxWalkSpeed);
				TestEqual(TEXT("Gravity is already applied"), Test.World->GetGravityZ(), WorldMessage->NewProfile->WorldGravityZ);
			}
		}
		else if (const auto* Changed = Message.GetPtr<FJGDCharacterPhysicsProfileChangedMessage>())
		{
			++CharacterCount;
			TestEqual(TEXT("Character identity"), Changed->Character.Get(), Character);
			const float ExpectedSpeed = Changed->NewProfile ? Changed->NewProfile->Settings.MaxWalkSpeed : FJGDCharacterPhysicsSettings().MaxWalkSpeed;
			TestEqual(TEXT("Notification follows movement apply"), Character->GetCharacterMovement()->MaxWalkSpeed, ExpectedSpeed);
		}
	};
	Receiver->OnLegacy = [&](ACharacter*, UJGDCharacterPhysicsProfile*, UJGDCharacterPhysicsProfile*) { ++LegacyCount; };
	JGDPhysicsMessageTests::Listen(Test.Bus, JGDPhysicsMessageTags::WorldProfileChanged(), Receiver.Get());
	JGDPhysicsMessageTests::Listen(Test.Bus, JGDPhysicsMessageTags::CharacterProfileChanged(), Receiver.Get());
	Test.Physics->OnCharacterPhysicsProfileChanged.AddDynamic(Receiver.Get(), &UJGDPhysicsMessageTestReceiver::ReceiveLegacy);
	TStrongObjectPtr<UJGDPhysicsMessageTestReceiver> ParentReceiver(NewObject<UJGDPhysicsMessageTestReceiver>());
	ParentReceiver->OnMessage = [&](FGameplayTag, const FInstancedStruct&) { ++ParentCount; };
	JGDPhysicsMessageTests::Listen(Test.Bus, FGameplayTag::RequestGameplayTag(TEXT("Event.Physics")), ParentReceiver.Get());
	Test.BeginPlay();
	TestEqual(TEXT("Default world profile notified once"), WorldCount, 1);
	Test.ApplyWorld(nullptr);
	const int32 WorldCountAfterClear = WorldCount;
	Test.ApplyWorld(nullptr);
	TestEqual(TEXT("Repeated None world is deduplicated"), WorldCount, WorldCountAfterClear);
	Character = Test.SpawnCharacter();
	Test.Physics->RegisterCharacter(Character);
	TestEqual(TEXT("First character apply notifies even when both profiles are None"), CharacterCount, 1);
	Test.Physics->RefreshCharacterProfile(Character);
	Test.Physics->RegisterCharacter(Character);
	TestEqual(TEXT("Repeated character refresh is deduplicated"), CharacterCount, 1);

	auto* WorldProfile = NewObject<UJGDPhysicsWorldProfile>(Test.World);
	WorldProfile->bOverrideWorldGravity = true;
	WorldProfile->WorldGravityZ = -333.0f;
	WorldProfile->DefaultCharacterProfile = JGDPhysicsMessageTests::MakeCharacterProfile(WorldProfile, 432.0f);
	Test.ApplyWorld(WorldProfile);
	TestEqual(TEXT("Changed character profile notified"), CharacterCount, 2);
	const int32 BeforeRepeat = WorldCount;
	WorldProfile->DefaultCharacterProfile->Settings.MaxWalkSpeed = 543.0f;
	Test.ApplyWorld(WorldProfile);
	TestEqual(TEXT("Same world reference has no duplicate event"), WorldCount, BeforeRepeat);
	TestEqual(TEXT("Same character reference has no duplicate event"), CharacterCount, 2);
	TestEqual(TEXT("Repeated apply still refreshes values"), Character->GetCharacterMovement()->MaxWalkSpeed, 543.0f);
	TestEqual(TEXT("Legacy delegate kept in sync"), LegacyCount, CharacterCount);
	TestEqual(TEXT("Parent tags do not receive child messages"), ParentCount, 0);
	Test.Bus->UnregisterAllListeners(Receiver.Get());
	Test.Bus->UnregisterAllListeners(ParentReceiver.Get());
	Test.Physics->OnCharacterPhysicsProfileChanged.RemoveAll(Receiver.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDPhysicsMessageLifecycleTest, "JGD2026.Physics.Messages.ValidationAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDPhysicsMessageLifecycleTest::RunTest(const FString& Parameters)
{
	JGDPhysicsMessageTests::FTestWorld Test;
	auto* Profile = NewObject<UJGDPhysicsWorldProfile>(Test.World);
	Profile->bOverrideWorldGravity = true;
	Profile->WorldGravityZ = -123.0f;
	TestFalse(TEXT("No request subscription before BeginPlay"), Test.Bus->HasListeners(JGDPhysicsMessageTags::ApplyWorldProfile()));
	Test.ApplyWorld(Profile);
	TStrongObjectPtr<UJGDPhysicsMessageTestReceiver> EndPlayReceiver(NewObject<UJGDPhysicsMessageTestReceiver>());
	bool bEndPlayOnRequest = false;
	EndPlayReceiver->OnMessage = [&](FGameplayTag, const FInstancedStruct&)
	{
		if (bEndPlayOnRequest)
		{
			TestTrue(TEXT("World EndPlay ran inside an earlier request listener"), Test.World->EndPlay(EEndPlayReason::Quit));
		}
	};
	// Registered first so the bus snapshot still contains the bridge after this callback ends the world.
	JGDPhysicsMessageTests::Listen(Test.Bus, JGDPhysicsMessageTags::ApplyWorldProfile(), EndPlayReceiver.Get());
	Test.BeginPlay();
	TestTrue(TEXT("World request listener installed"), Test.Bus->HasListeners(JGDPhysicsMessageTags::ApplyWorldProfile()));
	TestTrue(TEXT("Pre-BeginPlay request was not replayed"), Test.World->GetGravityZ() != -123.0f);
	Test.ApplyWorld(nullptr);
	const float Gravity = Test.World->GetGravityZ();

	UWorld* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false);
	OtherWorld->SetGameInstance(Test.GameInstance.Get());
	Test.ApplyWorld(Profile, OtherWorld);
	TestEqual(TEXT("Cross-world request silently ignored"), Test.World->GetGravityZ(), Gravity);
	ACharacter* OtherCharacter = OtherWorld->SpawnActor<ACharacter>();
	auto* Base = JGDPhysicsMessageTests::MakeCharacterProfile(Test.World, 123.0f);
	const float OtherSpeed = OtherCharacter->GetCharacterMovement()->MaxWalkSpeed;
	Test.SetBase(OtherCharacter, Base);
	TestEqual(TEXT("Foreign character not registered here"), OtherCharacter->GetCharacterMovement()->MaxWalkSpeed, OtherSpeed);
	// Two worlds sharing the same GameInstance must still route to exactly one recipient.
	auto* OtherPhysics = OtherWorld->GetSubsystem<UJGDPhysicsWorldSubsystem>();
	OtherPhysics->OnWorldBeginPlay(*OtherWorld);
	Test.ApplyWorld(Profile, OtherWorld);
	TestEqual(TEXT("Matching second world accepts request"), OtherWorld->GetGravityZ(), -123.0f);
	TestEqual(TEXT("First world remains isolated"), Test.World->GetGravityZ(), Gravity);
	Test.SetBase(OtherCharacter, Base);
	TestEqual(TEXT("Character routed to second world"), OtherCharacter->GetCharacterMovement()->MaxWalkSpeed, 123.0f);
	OtherPhysics->OnWorldEndPlay(*OtherWorld);
	OtherWorld->DestroyWorld(false);

	AddExpectedMessage(TEXT("incorrect payload type"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	Test.Bus->BroadcastMessage(JGDPhysicsMessageTags::ApplyWorldProfile(), FInstancedStruct());
	Test.Bus->BroadcastMessage(JGDPhysicsMessageTags::SetCharacterBaseProfile(), FInstancedStruct::Make<FJGDApplyWorldPhysicsProfileRequest>());
	AddExpectedMessage(TEXT("invalid TargetWorld"), ELogVerbosity::Warning);
	Test.Bus->BroadcastMessage(JGDPhysicsMessageTags::ApplyWorldProfile(), FInstancedStruct::Make<FJGDApplyWorldPhysicsProfileRequest>());
	AddExpectedMessage(TEXT("invalid Character"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	Test.SetBase(nullptr, Base);
	ACharacter* DestroyedCharacter = Test.SpawnCharacter();
	DestroyedCharacter->Destroy();
	Test.SetBase(DestroyedCharacter, Base);
	auto* InvalidProfile = NewObject<UJGDPhysicsWorldProfile>();
	InvalidProfile->MarkAsGarbage();
	AddExpectedMessage(TEXT("invalid WorldProfile"), ELogVerbosity::Warning);
	Test.ApplyWorld(InvalidProfile);
	auto* InvalidBase = JGDPhysicsMessageTests::MakeCharacterProfile(Test.World, 987.0f);
	InvalidBase->MarkAsGarbage();
	AddExpectedMessage(TEXT("invalid BaseProfile"), ELogVerbosity::Warning);
	Test.SetBase(Test.SpawnCharacter(), InvalidBase);
	TestEqual(TEXT("Invalid requests did not change gravity"), Test.World->GetGravityZ(), Gravity);

	bEndPlayOnRequest = true;
	Test.ApplyWorld(Profile);
	Test.Bus->UnregisterAllListeners(EndPlayReceiver.Get());
	TestEqual(TEXT("Already copied request delegate is inert after EndPlay"), Test.World->GetGravityZ(), Gravity);
	TestFalse(TEXT("World request listener removed"), Test.Bus->HasListeners(JGDPhysicsMessageTags::ApplyWorldProfile()));
	TestFalse(TEXT("Character request listener removed"), Test.Bus->HasListeners(JGDPhysicsMessageTags::SetCharacterBaseProfile()));
	Test.ApplyWorld(Profile);
	TestEqual(TEXT("No requests processed after EndPlay"), Test.World->GetGravityZ(), Gravity);
	// Destructor exercises Deinitialize after EndPlay: repeated cleanup must be safe.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDPhysicsMessageReentrancyTest, "JGD2026.Physics.Messages.ReentrantListeners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDPhysicsMessageReentrancyTest::RunTest(const FString& Parameters)
{
	JGDPhysicsMessageTests::FTestWorld Test;
	Test.BeginPlay();
	Test.ApplyWorld(nullptr);
	ACharacter* First = Test.SpawnCharacter();
	ACharacter* Second = Test.SpawnCharacter();
	Test.Physics->RegisterCharacter(First);
	Test.Physics->RegisterCharacter(Second);
	auto* Volume = Test.World->SpawnActor<AJGDPhysicsProfileVolume>();
	Volume->CharacterProfile = JGDPhysicsMessageTests::MakeCharacterProfile(Volume, 789.0f);
	Test.Physics->NotifyCharacterEnteredVolume(First, Volume);
	Test.Physics->NotifyCharacterEnteredVolume(Second, Volume);
	TStrongObjectPtr<UJGDPhysicsMessageTestReceiver> Receiver(NewObject<UJGDPhysicsMessageTestReceiver>());
	int32 CallbackCount = 0;
	Receiver->OnMessage = [&](FGameplayTag, const FInstancedStruct& Message)
	{
		if (const auto* Changed = Message.GetPtr<FJGDCharacterPhysicsProfileChangedMessage>(); Changed && Changed->Character == First)
		{
			++CallbackCount;
			Test.Physics->UnregisterCharacter(First);
			Test.Physics->UnregisterCharacter(Second);
			Second->Destroy();
			// Force map growth during NotifyVolumeRemoved's iteration.
			for (int32 Index = 0; Index < 32; ++Index)
			{
				Test.Physics->RegisterCharacter(Test.SpawnCharacter());
			}
		}
	};
	JGDPhysicsMessageTests::Listen(Test.Bus, JGDPhysicsMessageTags::CharacterProfileChanged(), Receiver.Get());
	Test.Physics->NotifyVolumeRemoved(Volume);
	TestEqual(TEXT("Volume removal survives unregister/destroy/rehash callbacks"), CallbackCount, 1);
	First->GetCharacterMovement()->MaxWalkSpeed = 17.0f;
	Test.Physics->RefreshCharacterProfile(First);
	TestEqual(TEXT("Callback unregister took effect"), First->GetCharacterMovement()->MaxWalkSpeed, 17.0f);
	Test.Bus->UnregisterAllListeners(Receiver.Get());

	Test.Physics->RegisterCharacter(First);
	CallbackCount = 0;
	Receiver->OnLegacy = [&](ACharacter* Character, UJGDCharacterPhysicsProfile*, UJGDCharacterPhysicsProfile*)
	{
		++CallbackCount;
		Test.Physics->UnregisterCharacter(Character);
	};
	Test.Physics->OnCharacterPhysicsProfileChanged.AddDynamic(Receiver.Get(), &UJGDPhysicsMessageTestReceiver::ReceiveLegacy);
	auto* WorldProfile = NewObject<UJGDPhysicsWorldProfile>(Test.World);
	WorldProfile->DefaultCharacterProfile = JGDPhysicsMessageTests::MakeCharacterProfile(WorldProfile, 321.0f);
	Test.ApplyWorld(WorldProfile);
	TestEqual(TEXT("Bulk refresh supports legacy listeners unregistering every character"), CallbackCount, 33);
	Test.Physics->OnCharacterPhysicsProfileChanged.RemoveAll(Receiver.Get());

	// Nested applies must not deliver a stale outer event after the newer transition.
	Test.Physics->RegisterCharacter(First);
	auto* BaseA = JGDPhysicsMessageTests::MakeCharacterProfile(Test.World, 111.0f);
	auto* BaseB = JGDPhysicsMessageTests::MakeCharacterProfile(Test.World, 222.0f);
	TArray<UJGDCharacterPhysicsProfile*> Observed;
	Receiver->OnLegacy = [&](ACharacter* Character, UJGDCharacterPhysicsProfile*, UJGDCharacterPhysicsProfile* NewProfile)
	{
		if (NewProfile == BaseA)
		{
			Test.SetBase(Character, BaseB);
		}
	};
	Receiver->OnMessage = [&](FGameplayTag, const FInstancedStruct& Message)
	{
		if (const auto* Changed = Message.GetPtr<FJGDCharacterPhysicsProfileChangedMessage>())
		{
			Observed.Add(Changed->NewProfile);
		}
	};
	Test.Physics->OnCharacterPhysicsProfileChanged.AddDynamic(Receiver.Get(), &UJGDPhysicsMessageTestReceiver::ReceiveLegacy);
	JGDPhysicsMessageTests::Listen(Test.Bus, JGDPhysicsMessageTags::CharacterProfileChanged(), Receiver.Get());
	Test.SetBase(First, BaseA);
	TestEqual(TEXT("Both nested transitions delivered"), Observed.Num(), 2);
	if (Observed.Num() == 2)
	{
		TestEqual(TEXT("First transition retains order"), Observed[0], BaseA);
		TestEqual(TEXT("Final displayed profile is newest"), Observed[1], BaseB);
	}
	TestEqual(TEXT("Final movement is newest"), First->GetCharacterMovement()->MaxWalkSpeed, 222.0f);
	Test.Bus->UnregisterAllListeners(Receiver.Get());
	Test.Physics->OnCharacterPhysicsProfileChanged.RemoveAll(Receiver.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDPhysicsMessageDisplayTest, "JGD2026.Physics.Messages.TestCharacterDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDPhysicsMessageDisplayTest::RunTest(const FString& Parameters)
{
	JGDPhysicsMessageTests::FTestWorld Test;
	Test.BeginPlay();
	auto* WorldProfile = NewObject<UJGDPhysicsWorldProfile>(Test.World);
	WorldProfile->DefaultCharacterProfile = JGDPhysicsMessageTests::MakeCharacterProfile(WorldProfile, 321.0f);
	Test.ApplyWorld(WorldProfile);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Character = Test.World->SpawnActor<AJGDWindTestCharacter>(AJGDWindTestCharacter::StaticClass(), FTransform::Identity, Params);
	const FObjectPropertyBase* Display = FindFProperty<FObjectPropertyBase>(AJGDWindTestCharacter::StaticClass(), TEXT("DisplayedPhysicsProfile"));
	if (!TestNotNull(TEXT("Test character exposes display cache"), Display))
	{
		return false;
	}
	TestEqual(TEXT("Initial display bootstraps missed component registration event"),
		Display->GetObjectPropertyValue_InContainer(Character), static_cast<UObject*>(WorldProfile->DefaultCharacterProfile));
	auto* Base = JGDPhysicsMessageTests::MakeCharacterProfile(Test.World, 456.0f);
	Test.SetBase(Character, Base);
	TestEqual(TEXT("Own notification updates display"), Display->GetObjectPropertyValue_InContainer(Character), static_cast<UObject*>(Base));
	Test.SetBase(Test.SpawnCharacter(), nullptr);
	TestEqual(TEXT("Other characters cannot overwrite display"), Display->GetObjectPropertyValue_InContainer(Character), static_cast<UObject*>(Base));
	Character->Destroy();
	TestFalse(TEXT("Test character unsubscribes on EndPlay"), Test.Bus->HasListeners(JGDPhysicsMessageTags::CharacterProfileChanged()));
	return true;
}

#endif
