// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/JGDPhysicsWorldSubsystem.h"

#include "JGD2026.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Physics/JGD2DPhysicsParticipantComponent.h"
#include "Physics/JGDPhysicsMessages.h"
#include "Physics/JGDPhysicsProfileVolume.h"
#include "Physics/JGDPhysicsProfiles.h"
#include "Physics/JGDPhysicsSystemSettings.h"

void UJGDPhysicsWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UJGDPhysicsWorldSubsystem::Deinitialize()
{
	UnsubscribeFromRequests();
	ResetRuntimeState();
	Super::Deinitialize();
}

void UJGDPhysicsWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldEnded = false;
	SubscribeToRequests();

	const UJGDPhysicsSystemSettings* Settings = GetDefault<UJGDPhysicsSystemSettings>();
	UJGDPhysicsWorldProfile* Profile = Settings ? Settings->DefaultWorldProfile.LoadSynchronous() : nullptr;
	ApplyWorldProfile(Profile);

	if (!Profile)
	{
		UE_LOG(LogJGDPhysics, Warning,
			TEXT("No default world physics profile is configured. Built-in character defaults will be used."));
	}
}

void UJGDPhysicsWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	UnsubscribeFromRequests();
	ResetRuntimeState();
	Super::OnWorldEndPlay(InWorld);
}

void UJGDPhysicsWorldSubsystem::ResetRuntimeState()
{
	bWorldEnded = true;
	RegisteredCharacters.Empty();
	CurrentWorldProfile = nullptr;
	bHasCapturedOriginalWorldGravity = false;
	bHasAppliedWorldProfile = false;
	PendingProfileNotifications.Empty();
}

void UJGDPhysicsWorldSubsystem::SubscribeToRequests()
{
	UnsubscribeFromRequests();
	UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this);
	if (!Bus)
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Physics message bridge has no GameInstance message subsystem."));
		return;
	}

	MessageSubsystem = Bus;
	FJGDMessageReceived Delegate;
	Delegate.BindDynamic(this, &ThisClass::HandleApplyWorldProfileRequest);
	ApplyWorldProfileListener = Bus->RegisterListener(JGDPhysicsMessageTags::ApplyWorldProfile(), Delegate);
	Delegate.BindDynamic(this, &ThisClass::HandleSetCharacterBaseProfileRequest);
	SetCharacterBaseProfileListener = Bus->RegisterListener(JGDPhysicsMessageTags::SetCharacterBaseProfile(), Delegate);
	bAcceptMessageRequests = true;
}

void UJGDPhysicsWorldSubsystem::UnsubscribeFromRequests()
{
	// A bus broadcast snapshots its delegates; an already copied callback must also be inert.
	bAcceptMessageRequests = false;
	if (UJGDMessageSubsystem* Bus = MessageSubsystem.Get())
	{
		Bus->UnregisterListener(ApplyWorldProfileListener);
		Bus->UnregisterListener(SetCharacterBaseProfileListener);
	}
	ApplyWorldProfileListener.Invalidate();
	SetCharacterBaseProfileListener.Invalidate();
	MessageSubsystem.Reset();
}

void UJGDPhysicsWorldSubsystem::HandleApplyWorldProfileRequest(FGameplayTag Channel, const FInstancedStruct& Message)
{
	if (!bAcceptMessageRequests)
	{
		return;
	}
	const auto* Request = Message.GetPtr<FJGDApplyWorldPhysicsProfileRequest>();
	if (!Request)
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Physics request %s has an incorrect payload type."), *Channel.ToString());
		return;
	}
	if (!IsValid(Request->TargetWorld))
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Physics world request has an invalid TargetWorld."));
		return;
	}
	if (Request->TargetWorld != GetWorld())
	{
		return;
	}
	if (Request->WorldProfile && !IsValid(Request->WorldProfile))
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Physics world request has an invalid WorldProfile."));
		return;
	}
	ApplyWorldProfile(Request->WorldProfile);
}

void UJGDPhysicsWorldSubsystem::HandleSetCharacterBaseProfileRequest(FGameplayTag Channel, const FInstancedStruct& Message)
{
	if (!bAcceptMessageRequests)
	{
		return;
	}
	const auto* Request = Message.GetPtr<FJGDSetCharacterBasePhysicsProfileRequest>();
	if (!Request)
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Physics request %s has an incorrect payload type."), *Channel.ToString());
		return;
	}
	ACharacter* Character = Request->Character;
	if (!IsValid(Character) || !IsValid(Character->GetWorld()) || !Character->GetCharacterMovement())
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Physics character request has an invalid Character or movement component."));
		return;
	}
	if (Character->GetWorld() != GetWorld())
	{
		return;
	}
	if (Request->BaseProfile && !IsValid(Request->BaseProfile))
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Physics character request has an invalid BaseProfile."));
		return;
	}
	if (UJGD2DPhysicsParticipantComponent* Participant = Character->FindComponentByClass<UJGD2DPhysicsParticipantComponent>())
	{
		Participant->SetBaseProfile(Request->BaseProfile);
	}
	else
	{
		SetCharacterBaseProfile(Character, Request->BaseProfile);
	}
}

bool UJGDPhysicsWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::GamePreview;
}

void UJGDPhysicsWorldSubsystem::RegisterCharacter(
	ACharacter* Character,
	UJGDCharacterPhysicsProfile* BaseProfile)
{
	if (bWorldEnded || !IsValid(Character) || Character->GetWorld() != GetWorld() || !Character->GetCharacterMovement())
	{
		UE_LOG(LogJGDPhysics, Warning, TEXT("Cannot register an invalid character or a character without movement."));
		return;
	}

	const bool bNewRegistration = !RegisteredCharacters.Contains(Character);
	FJGDCharacterPhysicsRegistration& Registration = RegisteredCharacters.FindOrAdd(Character);
	Registration.BaseProfile.Reset(BaseProfile);
	if (bNewRegistration)
	{
		RebuildActiveVolumes(*Character, Registration);
	}
	ApplyResolvedProfile(*Character, Registration);
}

void UJGDPhysicsWorldSubsystem::UnregisterCharacter(ACharacter* Character)
{
	if (Character)
	{
		RegisteredCharacters.Remove(Character);
	}
}

void UJGDPhysicsWorldSubsystem::SetCharacterBaseProfile(
	ACharacter* Character,
	UJGDCharacterPhysicsProfile* BaseProfile)
{
	if (bWorldEnded || !IsValid(Character) || Character->GetWorld() != GetWorld())
	{
		return;
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		Registration->BaseProfile.Reset(BaseProfile);
		ApplyResolvedProfile(*Character, *Registration);
		return;
	}

	RegisterCharacter(Character, BaseProfile);
}

void UJGDPhysicsWorldSubsystem::RefreshCharacterProfile(ACharacter* Character)
{
	if (!IsValid(Character))
	{
		return;
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		ApplyResolvedProfile(*Character, *Registration);
	}
}

UJGDCharacterPhysicsProfile* UJGDPhysicsWorldSubsystem::GetResolvedCharacterProfile(ACharacter* Character) const
{
	if (const FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		return ResolveProfile(*Registration);
	}

	return CurrentWorldProfile ? CurrentWorldProfile->DefaultCharacterProfile.Get() : nullptr;
}

void UJGDPhysicsWorldSubsystem::ApplyWorldProfile(UJGDPhysicsWorldProfile* WorldProfile)
{
	if (bWorldEnded)
	{
		return;
	}
	FJGDWorldPhysicsProfileChangedMessage Notification;
	Notification.World = GetWorld();
	Notification.PreviousProfile = CurrentWorldProfile;
	Notification.NewProfile = WorldProfile;
	const bool bProfileChanged = !bHasAppliedWorldProfile || CurrentWorldProfile != WorldProfile;
	CurrentWorldProfile = WorldProfile;
	bHasAppliedWorldProfile = true;
	++NotificationBatchDepth;

	if (UWorld* World = GetWorld())
	{
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			if (!bHasCapturedOriginalWorldGravity)
			{
				bOriginalGlobalGravitySet = WorldSettings->bGlobalGravitySet;
				OriginalGlobalGravityZ = WorldSettings->GlobalGravityZ;
				bHasCapturedOriginalWorldGravity = true;
			}

			const bool bOverrideGravity = WorldProfile && WorldProfile->bOverrideWorldGravity;
			if (bOverrideGravity)
			{
				WorldSettings->bGlobalGravitySet = true;
				WorldSettings->GlobalGravityZ = WorldProfile->WorldGravityZ;
			}
			else
			{
				WorldSettings->bGlobalGravitySet = bOriginalGlobalGravitySet;
				WorldSettings->GlobalGravityZ = OriginalGlobalGravityZ;
			}

			// Reset the cached value. The world updates the Chaos scene from this value each frame.
			WorldSettings->bWorldGravitySet = false;
			WorldSettings->GetGravityZ();
		}
	}

	RefreshAllCharacters();
	if (bProfileChanged)
	{
		PendingProfileNotifications.Add(FInstancedStruct::Make(Notification));
	}
	--NotificationBatchDepth;
	FlushProfileNotifications();
}

void UJGDPhysicsWorldSubsystem::NotifyCharacterEnteredVolume(
	ACharacter* Character,
	AJGDPhysicsProfileVolume* Volume)
{
	if (bWorldEnded || !IsValid(Character) || !IsValid(Volume)
		|| Character->GetWorld() != GetWorld() || Volume->GetWorld() != GetWorld())
	{
		return;
	}

	if (!RegisteredCharacters.Contains(Character))
	{
		RegisterCharacter(Character);
	}
	// Initial registration may synchronously notify a listener that destroys the actor.
	if (!IsValid(Character) || !IsValid(Volume))
	{
		return;
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		Registration->ActiveVolumes.AddUnique(Volume);
		ApplyResolvedProfile(*Character, *Registration);
	}
}

void UJGDPhysicsWorldSubsystem::NotifyCharacterLeftVolume(
	ACharacter* Character,
	AJGDPhysicsProfileVolume* Volume)
{
	if (!IsValid(Character) || !Volume)
	{
		return;
	}

	if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(Character))
	{
		Registration->ActiveVolumes.RemoveAll(
			[Volume](const TWeakObjectPtr<AJGDPhysicsProfileVolume>& Item)
			{
				return !Item.IsValid() || Item.Get() == Volume;
			});
		ApplyResolvedProfile(*Character, *Registration);
	}
}

void UJGDPhysicsWorldSubsystem::NotifyVolumeRemoved(AJGDPhysicsProfileVolume* Volume)
{
	if (!Volume)
	{
		return;
	}

	TArray<TWeakObjectPtr<ACharacter>> Characters;
	RegisteredCharacters.GenerateKeyArray(Characters);
	for (const TWeakObjectPtr<ACharacter>& WeakCharacter : Characters)
	{
		ACharacter* Character = WeakCharacter.Get();
		if (!IsValid(Character))
		{
			RegisteredCharacters.Remove(WeakCharacter);
			continue;
		}

		FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(WeakCharacter);
		if (!Registration)
		{
			continue;
		}
		const int32 RemovedCount = Registration->ActiveVolumes.RemoveAll(
			[Volume](const TWeakObjectPtr<AJGDPhysicsProfileVolume>& Item)
			{
				return !Item.IsValid() || Item.Get() == Volume;
			});
		if (RemovedCount > 0)
		{
			ApplyResolvedProfile(*Character, *Registration);
		}
	}
}

UJGDCharacterPhysicsProfile* UJGDPhysicsWorldSubsystem::ResolveProfile(
	const FJGDCharacterPhysicsRegistration& Registration) const
{
	UJGDCharacterPhysicsProfile* ResolvedProfile = nullptr;
	int32 BestPriority = TNumericLimits<int32>::Lowest();

	// Later entries win ties, which makes overlapping equal-priority volumes deterministic.
	for (const TWeakObjectPtr<AJGDPhysicsProfileVolume>& WeakVolume : Registration.ActiveVolumes)
	{
		const AJGDPhysicsProfileVolume* Volume = WeakVolume.Get();
		if (Volume && Volume->CharacterProfile && Volume->Priority >= BestPriority)
		{
			BestPriority = Volume->Priority;
			ResolvedProfile = Volume->CharacterProfile;
		}
	}

	if (ResolvedProfile)
	{
		return ResolvedProfile;
	}

	if (UJGDCharacterPhysicsProfile* BaseProfile = Registration.BaseProfile.Get())
	{
		return BaseProfile;
	}

	return CurrentWorldProfile ? CurrentWorldProfile->DefaultCharacterProfile.Get() : nullptr;
}

void UJGDPhysicsWorldSubsystem::RebuildActiveVolumes(
	ACharacter& Character,
	FJGDCharacterPhysicsRegistration& Registration)
{
	Registration.ActiveVolumes.Reset();

	TArray<AActor*> OverlappingActors;
	Character.GetOverlappingActors(OverlappingActors, AJGDPhysicsProfileVolume::StaticClass());
	for (AActor* Actor : OverlappingActors)
	{
		AJGDPhysicsProfileVolume* Volume = Cast<AJGDPhysicsProfileVolume>(Actor);
		if (IsValid(Volume) && Volume->GetWorld() == GetWorld())
		{
			Registration.ActiveVolumes.AddUnique(Volume);
		}
	}

	// Overlap caches can lag a PhysicsVolume update by a frame. Keep the engine-selected
	// volume as a fallback and place it last so it wins an otherwise ambiguous priority tie.
	if (AJGDPhysicsProfileVolume* CurrentVolume = Cast<AJGDPhysicsProfileVolume>(Character.GetPhysicsVolume());
		IsValid(CurrentVolume) && CurrentVolume->GetWorld() == GetWorld())
	{
		Registration.ActiveVolumes.Remove(CurrentVolume);
		Registration.ActiveVolumes.Add(CurrentVolume);
	}
}

void UJGDPhysicsWorldSubsystem::ApplyResolvedProfile(
	ACharacter& Character,
	FJGDCharacterPhysicsRegistration& Registration)
{
	UCharacterMovementComponent* MovementComponent = Character.GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	UJGDCharacterPhysicsProfile* PreviousProfile = Registration.AppliedProfile.Get();
	UJGDCharacterPhysicsProfile* NewProfile = ResolveProfile(Registration);

	if (NewProfile)
	{
		NewProfile->Settings.ApplyTo(*MovementComponent);
	}
	else
	{
		FJGDCharacterPhysicsSettings().ApplyTo(*MovementComponent);
	}

	const bool bProfileChanged = !Registration.bHasAppliedProfile || PreviousProfile != NewProfile;
	Registration.AppliedProfile.Reset(NewProfile);
	Registration.bHasAppliedProfile = true;

	if (bProfileChanged)
	{
		FJGDCharacterPhysicsProfileChangedMessage Notification;
		Notification.World = GetWorld();
		Notification.Character = &Character;
		Notification.PreviousProfile = PreviousProfile;
		Notification.NewProfile = NewProfile;
		PendingProfileNotifications.Add(FInstancedStruct::Make(Notification));
		FlushProfileNotifications();
		// Do not touch Registration after dispatch: a listener may remove it or rehash the map.
	}
}

void UJGDPhysicsWorldSubsystem::RefreshAllCharacters()
{
	TArray<TWeakObjectPtr<ACharacter>> Characters;
	RegisteredCharacters.GenerateKeyArray(Characters);
	for (const TWeakObjectPtr<ACharacter>& WeakCharacter : Characters)
	{
		ACharacter* Character = WeakCharacter.Get();
		if (!IsValid(Character))
		{
			RegisteredCharacters.Remove(WeakCharacter);
			continue;
		}

		if (FJGDCharacterPhysicsRegistration* Registration = RegisteredCharacters.Find(WeakCharacter))
		{
			ApplyResolvedProfile(*Character, *Registration);
		}
	}
}

void UJGDPhysicsWorldSubsystem::FlushProfileNotifications()
{
	if (bDispatchingProfileNotifications || NotificationBatchDepth > 0 || bWorldEnded)
	{
		return;
	}
	TGuardValue<bool> DispatchGuard(bDispatchingProfileNotifications, true);
	// Keep entries in the reflected array until dispatch ends so payload objects remain GC-visible.
	for (int32 Index = 0; Index < PendingProfileNotifications.Num() && !bWorldEnded; ++Index)
	{
		// Copy: callbacks may append notifications and reallocate the array.
		const FInstancedStruct Message = PendingProfileNotifications[Index];
		if (const auto* CharacterMessage = Message.GetPtr<FJGDCharacterPhysicsProfileChangedMessage>())
		{
			if (!IsValid(CharacterMessage->Character))
			{
				continue;
			}
			OnCharacterPhysicsProfileChanged.Broadcast(CharacterMessage->Character,
				CharacterMessage->PreviousProfile, CharacterMessage->NewProfile);
			if (UJGDMessageSubsystem* Bus = MessageSubsystem.Get(); Bus && !bWorldEnded && IsValid(CharacterMessage->Character))
			{
				Bus->BroadcastMessage(JGDPhysicsMessageTags::CharacterProfileChanged(), Message);
			}
		}
		else if (UJGDMessageSubsystem* Bus = MessageSubsystem.Get(); Bus && !bWorldEnded)
		{
			Bus->BroadcastMessage(JGDPhysicsMessageTags::WorldProfileChanged(), Message);
		}
	}
	PendingProfileNotifications.Reset();
}
