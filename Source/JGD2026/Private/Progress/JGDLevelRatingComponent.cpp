#include "Progress/JGDLevelRatingComponent.h"

#include "Assembly/AssemblyComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Progress/JGDLevelStar.h"

void UJGDLevelRatingComponent::InitializeRun(UAssemblyComponent* InModel)
{
	if (Model.Get() == InModel || !IsValid(InModel)) return;
	if (Model.IsValid())
	{
		Model->OnAssemblyReset.RemoveDynamic(this, &ThisClass::ResetRun);
		Model->OnPhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhase);
	}
	Model = InModel;
	Model->OnAssemblyReset.AddUniqueDynamic(this, &ThisClass::ResetRun);
	Model->OnPhaseChanged.AddUniqueDynamic(this, &ThisClass::HandlePhase);
	Stars.Reset();
	for (TActorIterator<AJGDLevelStar> It(GetWorld()); It; ++It) Stars.Add(*It);
	ResetRun();
}

void UJGDLevelRatingComponent::ResetRun()
{
	CollectedMask = 0;
	bComplete = false;
	bHavePrevious = false;
	Result = FJGDLevelRatingResult();
	for (const auto& Star : Stars) if (Star.IsValid()) Star->SetCollected(false);
}

void UJGDLevelRatingComponent::HandlePhase(EAssemblyPhase Phase)
{
	if (Phase == EAssemblyPhase::Assembly) ResetRun();
}

int32 UJGDLevelRatingComponent::GetCollectedStars() const
{
	return ((CollectedMask & 1) != 0) + ((CollectedMask & 2) != 0);
}

void UJGDLevelRatingComponent::UpdateCollection(UPrimitiveComponent* Core)
{
	if (bComplete || !Model.IsValid() || Model->GetPhase() != EAssemblyPhase::Playing || !IsValid(Core))
	{
		bHavePrevious = false;
		return;
	}
	FVector Position = Core->GetComponentLocation(); Position.Y = 0;
	const FVector Start = bHavePrevious ? PreviousCore : Position;
	for (const auto& Entry : Stars)
	{
		AJGDLevelStar* Star = Entry.Get();
		if (!Star || Star->StarIndex < 0 || Star->StarIndex > 1 || (CollectedMask & (1 << Star->StarIndex))) continue;
		FVector Center = Star->GetActorLocation(); Center.Y = 0;
		const double Radius = FMath::Max(0.f, Star->PickupRadius) + Model->GetFreeSettings().CoreRadius;
		if (FMath::PointDistToSegmentSquared(Center, Start, Position) <= FMath::Square(Radius))
		{
			CollectedMask |= (1 << Star->StarIndex);
			Star->SetCollected(true);
		}
	}
	PreviousCore = Position;
	bHavePrevious = true;
}

FJGDLevelRatingResult UJGDLevelRatingComponent::CompleteRun()
{
	if (bComplete || !Model.IsValid() || Model->GetPhase() != EAssemblyPhase::Playing) return Result;
	bComplete = true;
	Result.EarnedStars = 1 + GetCollectedStars();
	if (UGameInstance* Instance = GetWorld()->GetGameInstance())
		if (UJGDLevelProgressSubsystem* Progress = Instance->GetSubsystem<UJGDLevelProgressSubsystem>())
			Result = Progress->RecordCompletion(FName(UGameplayStatics::GetCurrentLevelName(this, true)), GetCollectedStars());
	return Result;
}

void UJGDLevelRatingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Model.IsValid())
	{
		Model->OnAssemblyReset.RemoveDynamic(this, &ThisClass::ResetRun);
		Model->OnPhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhase);
	}
	Super::EndPlay(Reason);
}
