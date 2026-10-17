// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JGDPhysicsMessages.generated.h"

class ACharacter;
class UJGDCharacterPhysicsProfile;
class UJGDPhysicsWorldProfile;
class UJGDPhysicsRigidBodyProfile;
class UPrimitiveComponent;
class UWorld;

/** Exact-match channels registered in DefaultGameplayTags.ini. */
namespace JGDPhysicsMessageTags
{
	JGD2026_API FGameplayTag ApplyWorldProfile();
	JGD2026_API FGameplayTag SetCharacterBaseProfile();
	JGD2026_API FGameplayTag WorldProfileChanged();
	JGD2026_API FGameplayTag CharacterProfileChanged();
	JGD2026_API FGameplayTag RegisterRigidBody();
	JGD2026_API FGameplayTag UnregisterRigidBody();
	JGD2026_API FGameplayTag SetRigidBodyBaseProfile();
	JGD2026_API FGameplayTag RigidBodyProfileChanged();
}

USTRUCT(BlueprintType)
struct JGD2026_API FJGDApplyWorldPhysicsProfileRequest
{
	GENERATED_BODY()

	/** Required; requests never implicitly target every world in a GameInstance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UWorld> TargetWorld = nullptr;

	/** None restores the original world gravity and built-in character defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDPhysicsWorldProfile> WorldProfile = nullptr;
};

USTRUCT(BlueprintType)
struct JGD2026_API FJGDSetCharacterBasePhysicsProfileRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<ACharacter> Character = nullptr;

	/** None clears the base override; volumes and world defaults still apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDCharacterPhysicsProfile> BaseProfile = nullptr;
};

USTRUCT(BlueprintType)
struct JGD2026_API FJGDWorldPhysicsProfileChangedMessage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDPhysicsWorldProfile> PreviousProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDPhysicsWorldProfile> NewProfile = nullptr;
};

USTRUCT(BlueprintType)
struct JGD2026_API FJGDCharacterPhysicsProfileChangedMessage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<ACharacter> Character = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDCharacterPhysicsProfile> PreviousProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDCharacterPhysicsProfile> NewProfile = nullptr;
};

USTRUCT(BlueprintType)
struct JGD2026_API FJGDRegisterRigidBodyPhysicsRequest
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UWorld> TargetWorld = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UPrimitiveComponent> Body = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDPhysicsRigidBodyProfile> BaseProfile = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	FGuid RegistrationId;
};

USTRUCT(BlueprintType)
struct JGD2026_API FJGDUnregisterRigidBodyPhysicsRequest
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UWorld> TargetWorld = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UPrimitiveComponent> Body = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	FGuid RegistrationId;
};

USTRUCT(BlueprintType)
struct JGD2026_API FJGDSetRigidBodyBasePhysicsProfileRequest
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UWorld> TargetWorld = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UPrimitiveComponent> Body = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDPhysicsRigidBodyProfile> BaseProfile = nullptr;
};

USTRUCT(BlueprintType)
struct JGD2026_API FJGDRigidBodyPhysicsProfileChangedMessage
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UWorld> World = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UPrimitiveComponent> Body = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	FGuid RegistrationId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDPhysicsRigidBodyProfile> PreviousProfile = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JGD|Physics|Messages")
	TObjectPtr<UJGDPhysicsRigidBodyProfile> NewProfile = nullptr;
};

/** Lets Blueprints populate the explicit TargetWorld field without engine-specific casts. */
UCLASS()
class JGD2026_API UJGDPhysicsMessageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "JGD|Physics|Messages", meta = (WorldContext = "WorldContextObject"))
	static UWorld* GetPhysicsMessageWorld(const UObject* WorldContextObject);
};
