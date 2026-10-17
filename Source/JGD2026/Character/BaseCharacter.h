// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BaseCharacter.generated.h"

class UAssemblySessionComponent;

UCLASS()
class JGD2026_API ABaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABaseCharacter();

protected:
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	UFUNCTION()
	void HandleAssemblySessionChanged();

private:
	TWeakObjectPtr<UAssemblySessionComponent> PhysicsBootstrapSession;
	bool bBindingAssemblyPhysicsExecutor = false;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

};
