// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Physics/JGDPhysicsProfiles.h"
#include "Subsystems/WorldSubsystem.h"
#include "JGDPhysicsWorldSubsystem.generated.h"

class ACharacter;
class AJGDPhysicsProfileVolume;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FJGDCharacterPhysicsProfileChanged,
	ACharacter*, Character,
	UJGDCharacterPhysicsProfile*, PreviousProfile,
	UJGDCharacterPhysicsProfile*, NewProfile);

struct FJGDCharacterPhysicsRegistration
{
	TWeakObjectPtr<UJGDCharacterPhysicsProfile> BaseProfile;
	TArray<TWeakObjectPtr<AJGDPhysicsProfileVolume>> ActiveVolumes;
	TWeakObjectPtr<UJGDCharacterPhysicsProfile> AppliedProfile;
	bool bHasAppliedProfile = false;
};

/** Coordinates world defaults and event-driven character profile changes. */
UCLASS()
class JGD2026_API UJGDPhysicsWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	UFUNCTION(BlueprintCallable, Category = "JGD|Physics", meta = (DefaultToSelf = "Character"))
	void RegisterCharacter(ACharacter* Character, UJGDCharacterPhysicsProfile* BaseProfile = nullptr);

	UFUNCTION(BlueprintCallable, Category = "JGD|Physics", meta = (DefaultToSelf = "Character"))
	void UnregisterCharacter(ACharacter* Character);

	UFUNCTION(BlueprintCallable, Category = "JGD|Physics", meta = (DefaultToSelf = "Character"))
	void SetCharacterBaseProfile(ACharacter* Character, UJGDCharacterPhysicsProfile* BaseProfile);

	UFUNCTION(BlueprintCallable, Category = "JGD|Physics", meta = (DefaultToSelf = "Character"))
	void RefreshCharacterProfile(ACharacter* Character);

	UFUNCTION(BlueprintPure, Category = "JGD|Physics", meta = (DefaultToSelf = "Character"))
	UJGDCharacterPhysicsProfile* GetResolvedCharacterProfile(ACharacter* Character) const;

	UFUNCTION(BlueprintCallable, Category = "JGD|Physics")
	void ApplyWorldProfile(UJGDPhysicsWorldProfile* WorldProfile);

	void NotifyCharacterEnteredVolume(ACharacter* Character, AJGDPhysicsProfileVolume* Volume);
	void NotifyCharacterLeftVolume(ACharacter* Character, AJGDPhysicsProfileVolume* Volume);
	void NotifyVolumeRemoved(AJGDPhysicsProfileVolume* Volume);

	UPROPERTY(BlueprintAssignable, Category = "JGD|Physics")
	FJGDCharacterPhysicsProfileChanged OnCharacterPhysicsProfileChanged;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	UJGDCharacterPhysicsProfile* ResolveProfile(const FJGDCharacterPhysicsRegistration& Registration) const;
	void ApplyResolvedProfile(ACharacter& Character, FJGDCharacterPhysicsRegistration& Registration);
	void RefreshAllCharacters();

	UPROPERTY(Transient)
	TObjectPtr<UJGDPhysicsWorldProfile> CurrentWorldProfile;

	TMap<TWeakObjectPtr<ACharacter>, FJGDCharacterPhysicsRegistration> RegisteredCharacters;

	bool bHasCapturedOriginalWorldGravity = false;
	bool bOriginalGlobalGravitySet = false;
	float OriginalGlobalGravityZ = -980.0f;
};
