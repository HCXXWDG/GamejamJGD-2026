// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MessageBus/JGDMessageBusTypes.h"
#include "JGDWindTestCharacter.generated.h"

class UCameraComponent;
class UJGD2DPhysicsParticipantComponent;
class UJGDCharacterPhysicsProfile;
class UJGDMessageSubsystem;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * Self-contained side-view character used to verify local physics profiles and wind forces.
 * It intentionally binds keys directly so the test does not require Input Action assets.
 */
UCLASS(Blueprintable)
class JGD2026_API AJGDWindTestCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AJGDWindTestCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Bootstrapped once at BeginPlay, then updated only by this character's bus notifications. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "JGD|Test")
	TObjectPtr<UJGDCharacterPhysicsProfile> DisplayedPhysicsProfile;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JGD|Test")
	TObjectPtr<UJGD2DPhysicsParticipantComponent> PhysicsParticipant;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JGD|Test")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JGD|Test")
	TObjectPtr<UCameraComponent> SideViewCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JGD|Test")
	TObjectPtr<UStaticMeshComponent> TestVisual;

	/** Prints position, velocity and the currently resolved physics profile while playing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JGD|Test")
	bool bShowDebugInfo = true;

private:
	void StartMoveLeft();
	void StopMoveLeft();
	void StartMoveRight();
	void StopMoveRight();
	void StartJump();
	void StopJump();
	void ResetTestCharacter();
	void DrawDebugInfo() const;

	UFUNCTION()
	void HandlePhysicsProfileChanged(FGameplayTag Channel, const FInstancedStruct& Message);

	TWeakObjectPtr<UJGDMessageSubsystem> MessageSubsystem;
	FJGDMessageListenerHandle ProfileChangedListener;

	bool bMoveLeft = false;
	bool bMoveRight = false;
	FTransform InitialTransform;
};
