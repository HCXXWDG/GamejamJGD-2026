// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/JGDWindTestCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Physics/JGD2DPhysicsParticipantComponent.h"
#include "Physics/JGDPhysicsMessages.h"
#include "Physics/JGDPhysicsProfiles.h"
#include "Physics/JGDPhysicsWorldSubsystem.h"
#include "UObject/ConstructorHelpers.h"

AJGDWindTestCharacter::AJGDWindTestCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bConstrainToPlane = true;
	Movement->SetPlaneConstraintNormal(FVector::YAxisVector);
	Movement->bSnapToPlaneAtStart = true;
	Movement->bOrientRotationToMovement = false;

	PhysicsParticipant = CreateDefaultSubobject<UJGD2DPhysicsParticipantComponent>(TEXT("PhysicsParticipant"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 700.0f;
	CameraBoom->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bUsePawnControlRotation = false;

	SideViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("SideViewCamera"));
	SideViewCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	SideViewCamera->bUsePawnControlRotation = false;

	TestVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestVisual"));
	TestVisual->SetupAttachment(GetCapsuleComponent());
	TestVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestVisual->SetGenerateOverlapEvents(false);
	TestVisual->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.75f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		TestVisual->SetStaticMesh(CubeMesh.Object);
	}
}

void AJGDWindTestCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitialTransform = GetActorTransform();

	if (UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this))
	{
		MessageSubsystem = Bus;
		FJGDMessageReceived Delegate;
		Delegate.BindDynamic(this, &ThisClass::HandlePhysicsProfileChanged);
		ProfileChangedListener = Bus->RegisterListener(JGDPhysicsMessageTags::CharacterProfileChanged(), Delegate);
	}
	// Super::BeginPlay already starts the Participant, so its first notification may precede ours.
	if (const UJGDPhysicsWorldSubsystem* Physics = GetWorld()->GetSubsystem<UJGDPhysicsWorldSubsystem>())
	{
		DisplayedPhysicsProfile = Physics->GetResolvedCharacterProfile(this);
	}
}

void AJGDWindTestCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UJGDMessageSubsystem* Bus = MessageSubsystem.Get())
	{
		Bus->UnregisterListener(ProfileChangedListener);
	}
	ProfileChangedListener.Invalidate();
	MessageSubsystem.Reset();
	DisplayedPhysicsProfile = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AJGDWindTestCharacter::HandlePhysicsProfileChanged(FGameplayTag Channel, const FInstancedStruct& Message)
{
	const auto* Notification = Message.GetPtr<FJGDCharacterPhysicsProfileChangedMessage>();
	if (ProfileChangedListener.IsValid() && Channel == JGDPhysicsMessageTags::CharacterProfileChanged()
		&& Notification && Notification->World == GetWorld() && Notification->Character == this)
	{
		DisplayedPhysicsProfile = Notification->NewProfile;
	}
}

void AJGDWindTestCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float HorizontalInput = static_cast<float>(bMoveRight) - static_cast<float>(bMoveLeft);
	if (!FMath::IsNearlyZero(HorizontalInput))
	{
		AddMovementInput(FVector::XAxisVector, HorizontalInput);
	}

	if (bShowDebugInfo)
	{
		DrawDebugInfo();
	}
}

void AJGDWindTestCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindKey(EKeys::A, IE_Pressed, this, &AJGDWindTestCharacter::StartMoveLeft);
	PlayerInputComponent->BindKey(EKeys::A, IE_Released, this, &AJGDWindTestCharacter::StopMoveLeft);
	PlayerInputComponent->BindKey(EKeys::Left, IE_Pressed, this, &AJGDWindTestCharacter::StartMoveLeft);
	PlayerInputComponent->BindKey(EKeys::Left, IE_Released, this, &AJGDWindTestCharacter::StopMoveLeft);

	PlayerInputComponent->BindKey(EKeys::D, IE_Pressed, this, &AJGDWindTestCharacter::StartMoveRight);
	PlayerInputComponent->BindKey(EKeys::D, IE_Released, this, &AJGDWindTestCharacter::StopMoveRight);
	PlayerInputComponent->BindKey(EKeys::Right, IE_Pressed, this, &AJGDWindTestCharacter::StartMoveRight);
	PlayerInputComponent->BindKey(EKeys::Right, IE_Released, this, &AJGDWindTestCharacter::StopMoveRight);

	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AJGDWindTestCharacter::StartJump);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &AJGDWindTestCharacter::StopJump);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AJGDWindTestCharacter::ResetTestCharacter);
}

void AJGDWindTestCharacter::StartMoveLeft()
{
	bMoveLeft = true;
}

void AJGDWindTestCharacter::StopMoveLeft()
{
	bMoveLeft = false;
}

void AJGDWindTestCharacter::StartMoveRight()
{
	bMoveRight = true;
}

void AJGDWindTestCharacter::StopMoveRight()
{
	bMoveRight = false;
}

void AJGDWindTestCharacter::StartJump()
{
	Jump();
}

void AJGDWindTestCharacter::StopJump()
{
	StopJumping();
}

void AJGDWindTestCharacter::ResetTestCharacter()
{
	bMoveLeft = false;
	bMoveRight = false;
	StopJumping();
	GetCharacterMovement()->StopMovementImmediately();
	SetActorTransform(InitialTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

void AJGDWindTestCharacter::DrawDebugInfo() const
{
	if (!GEngine)
	{
		return;
	}

	const FVector Location = GetActorLocation();
	const FVector Velocity = GetVelocity();
	const FString Message = FString::Printf(
		TEXT("Wind Test Player\nLocation X/Z: %.1f / %.1f\nVelocity X/Z: %.1f / %.1f\nProfile: %s\nA/D: Move   Space: Jump   R: Reset"),
		Location.X,
		Location.Z,
		Velocity.X,
		Velocity.Z,
		*GetNameSafe(DisplayedPhysicsProfile.Get()));

	GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::Cyan, Message);
}
