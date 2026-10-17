// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseCharacter.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Physics/JGDAssemblyPhysicsExecutorComponent.h"

// Sets default values
ABaseCharacter::ABaseCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

void ABaseCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	TArray<UAssemblySessionComponent*> Sessions;
	GetComponents(Sessions);
	if (Sessions.Num() != 1)
	{
		return;
	}
	PhysicsBootstrapSession = Sessions[0];
	Sessions[0]->OnSessionChanged.AddUniqueDynamic(this, &ABaseCharacter::HandleAssemblySessionChanged);
	HandleAssemblySessionChanged();
}

void ABaseCharacter::HandleAssemblySessionChanged()
{
	UAssemblySessionComponent* Session = PhysicsBootstrapSession.Get();
	if (bBindingAssemblyPhysicsExecutor || !HasActorBegunPlay() || !IsValid(Session))
	{
		return;
	}
	UAssemblyComponent* Assembly = Session->GetAssemblyComponent();
	UAssemblyInteractionComponent* Interaction = FindComponentByClass<UAssemblyInteractionComponent>();
	UAssemblyPhysicsBridgeComponent* Bridge = IsValid(Interaction) ? Interaction->GetPhysicsBridge() : nullptr;
	if (!IsValid(Assembly) || !IsValid(Bridge) || !Bridge->IsBridgeInitialized()
		|| Bridge->GetPhysicsContext().Assembly != Assembly || !Session->CanOwnPhysicsLifecycle(Assembly))
	{
		return;
	}

	TGuardValue<bool> BindingGuard(bBindingAssemblyPhysicsExecutor, true);
	// A formal player must fail closed if its physical executor cannot be bound.
	Bridge->bRequirePhysicsLifecycleProvider = true;
	TArray<UJGDAssemblyPhysicsExecutorComponent*> Executors;
	GetComponents(Executors);
	if (Executors.Num() > 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Assembly player has %d physics executors; expected exactly one."), Executors.Num());
		return;
	}
	UJGDAssemblyPhysicsExecutorComponent* Executor = Executors.IsEmpty() ? nullptr : Executors[0];
	if (!Executor)
	{
		Executor = NewObject<UJGDAssemblyPhysicsExecutorComponent>(this);
		AddInstanceComponent(Executor);
		Executor->RegisterComponent();
	}
	if (!IsValid(Executor) || !Executor->IsRegistered())
	{
		UE_LOG(LogTemp, Error, TEXT("Assembly player could not register its physics executor."));
		return;
	}
	FText Reason;
	if (!Executor->InitializeExecutor(Bridge, Session, Reason))
	{
		UE_LOG(LogTemp, Error, TEXT("Assembly player physics executor binding failed: %s"), *Reason.ToString());
	}
}

void ABaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAssemblySessionComponent* Session = PhysicsBootstrapSession.Get())
	{
		Session->OnSessionChanged.RemoveDynamic(this, &ABaseCharacter::HandleAssemblySessionChanged);
	}
	PhysicsBootstrapSession.Reset();
	Super::EndPlay(EndPlayReason);
}

// Called when the game starts or when spawned
void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Blueprint initialization may finish before the actor reports BeginPlay.
	HandleAssemblySessionChanged();
}

// Called every frame
void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

