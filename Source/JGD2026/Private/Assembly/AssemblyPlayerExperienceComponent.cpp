#include "Assembly/AssemblyPlayerExperienceComponent.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyExperienceSettings.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Assembly/AssemblyInteractionComponent.h"
#include "Assembly/AssemblyWorkbenchComponent.h"
#include "Assembly/AssemblyHUDWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/Border.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "PaperTileMapComponent.h"

void UAssemblyScreenFadeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Black = WidgetTree->ConstructWidget<UBorder>();
	Black->SetBrushColor(FLinearColor(0,0,0,0));
	WidgetTree->RootWidget = Black;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UAssemblyScreenFadeWidget::SetFadeAlpha(float Alpha)
{
	if (Black) Black->SetBrushColor(FLinearColor(0,0,0,FMath::Clamp(Alpha,0.0f,1.0f)));
}

UAssemblyPlayerExperienceComponent::UAssemblyPlayerExperienceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAssemblyPlayerExperienceComponent::RestartWithFade()
{
	if (FadeState || !Session.IsValid()) return;
	Session->ClearHeldInput();
	if (UAssemblyWorkbenchComponent* Workbench = Session->GetWorkbench()) Workbench->CancelTool();
	if (!FadeWidget)
	{
		FadeWidget = CreateWidget<UAssemblyScreenFadeWidget>(Session->GetPlayerController());
		if (FadeWidget) FadeWidget->AddToViewport(10000);
	}
	FadeTime = 0;
	FadeState = 1;
}

void UAssemblyPlayerExperienceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(DeltaTime, TickType, Tick);
	if (!Session.IsValid()) Session = GetOwner()->FindComponentByClass<UAssemblySessionComponent>();
	UAssemblySessionComponent* Active = Session.Get();
	UAssemblyComponent* Model = Active ? Active->GetAssemblyComponent() : nullptr;
	UAssemblyWorkbenchComponent* Workbench = Active ? Active->GetWorkbench() : nullptr;
	UAssemblyInteractionComponent* Interaction = Workbench ? Workbench->GetInteractionComponent() : nullptr;
	UPrimitiveComponent* Core = Interaction ? Interaction->GetCoreBody() : nullptr;
	APlayerController* PC = Active ? Active->GetPlayerController() : nullptr;
	if (!Model || !Core || !PC || !PC->IsLocalController()) return;
	if (!bGoalsCaptured)
	{
		// Resolve after the pawn has begun play; the goal Blueprint itself references
		// BP_Player, so loading it inside a native CDO constructor creates a cycle.
		DestinationClass = LoadClass<AActor>(nullptr, TEXT("/Game/Module/Grid/Blueprints/Destination/BP_Destination.BP_Destination_C"));
		bGoalsCaptured = true;
		for (TActorIterator<AActor> It(GetWorld(), DestinationClass); It; ++It)
			for (UBoxComponent* Box : TInlineComponentArray<UBoxComponent*>(*It))
				if (Box && Box->GetFName() == TEXT("Box"))
				{
					Goals.Add(Box);
					GoalOverlapEnabled.Add(Box->GetGenerateOverlapEvents());
					// The legacy Blueprint immediately opens the next map. This session now
					// detects the real core and presents a paused completion screen first.
					Box->SetGenerateOverlapEvents(false);
				}
	}
	const UAssemblyExperienceSettings* Settings = GetDefault<UAssemblyExperienceSettings>();
	const bool bPlaying = Model->GetPhase() == EAssemblyPhase::Playing;
	if (bPlaying && !FadeState)
	{
		const FVector Position = Core->GetComponentLocation();
		const double Radius = Model->GetFreeSettings().CoreRadius;
		for (const TWeakObjectPtr<UBoxComponent>& Goal : Goals)
		{
			if (!Goal.IsValid()) continue;
			const FBox Bounds = Goal->Bounds.GetBox();
			const double DX = FMath::Max(FMath::Max(Bounds.Min.X - Position.X, Position.X - Bounds.Max.X), 0.0);
			const double DZ = FMath::Max(FMath::Max(Bounds.Min.Z - Position.Z, Position.Z - Bounds.Max.Z), 0.0);
			if (DX * DX + DZ * DZ <= Radius * Radius)
			{
				Active->ClearHeldInput();
				Core->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Core->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
				if (UAssemblyHUDWidget* HUD = Active->GetHUD()) HUD->CompleteLevel();
				return;
			}
		}
	}
	if (!bCameraReady)
	{
		PreviousView = PC->GetViewTarget();
		FollowCamera = GetWorld()->SpawnActor<ACameraActor>();
		if (!FollowCamera) return;
		FollowCamera->GetCameraComponent()->ProjectionMode = ECameraProjectionMode::Orthographic;
		FollowCamera->GetCameraComponent()->SetOrthoWidth(FMath::Max(640.0f, Settings->CameraOrthoWidth));
		FollowCamera->SetActorRotation(FRotator(0,-90,0));
		FollowCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		FollowCamera->SetActorLocation(Core->GetComponentLocation() + FVector(-120,1800,90));
		PC->SetViewTarget(FollowCamera);
		FallThreshold = Core->GetComponentLocation().Z - 1200;
		bool bFoundMap = false;
		float MapBottom = TNumericLimits<float>::Max();
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			for (UPaperTileMapComponent* Map : TInlineComponentArray<UPaperTileMapComponent*>(*It))
				if (Map && Map->TileMap)
				{
					MapBottom = FMath::Min(MapBottom, static_cast<float>(Map->Bounds.Origin.Z - Map->Bounds.BoxExtent.Z));
					bFoundMap = true;
				}
		if (bFoundMap) FallThreshold = MapBottom - Settings->FallDistanceBelowMap;
		bCameraReady = true;
	}
	if (PC->GetViewTarget() != FollowCamera) PC->SetViewTarget(FollowCamera);
	const FVector Target = Core->GetComponentLocation() + FVector(bPlaying ? 0 : -120, 1800, 90);
	const bool bSnap = bWasPlaying && !bPlaying;
	FollowCamera->SetActorLocation(bSnap ? Target : FMath::VInterpTo(FollowCamera->GetActorLocation(), Target, DeltaTime, Settings->CameraFollowSpeed));
	bWasPlaying = bPlaying;
	if (!FadeState && bPlaying && Core->GetComponentLocation().Z < FallThreshold) RestartWithFade();
	if (FadeState)
	{
		Active->ClearHeldInput();
		FadeTime += DeltaTime;
		const float Duration = FMath::Max(.1f, Settings->RestartFadeSeconds);
		if (FadeState == 1)
		{
			if (FadeWidget) FadeWidget->SetFadeAlpha(FadeTime / Duration);
			if (FadeTime >= Duration)
			{
				FText Reason;
				if (!Active->RestartSession(Reason)) Active->ShowMessage(Reason);
				FadeState = 2;
				FadeTime = 0;
			}
		}
		else
		{
			if (FadeWidget) FadeWidget->SetFadeAlpha(1.0f - FadeTime / Duration);
			if (FadeTime >= Duration)
			{
				FadeState = 0;
				if (FadeWidget) FadeWidget->SetFadeAlpha(0);
			}
		}
	}
}

void UAssemblyPlayerExperienceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	for (int32 Index = 0; Index < Goals.Num(); ++Index)
		if (Goals[Index].IsValid()) Goals[Index]->SetGenerateOverlapEvents(GoalOverlapEnabled[Index]);
	if (FadeWidget) FadeWidget->RemoveFromParent();
	if (Session.IsValid())
		if (APlayerController* PC = Session->GetPlayerController())
			if (PC->GetViewTarget() == FollowCamera && PreviousView.IsValid()) PC->SetViewTarget(PreviousView.Get());
	if (FollowCamera) FollowCamera->Destroy();
	Super::EndPlay(Reason);
}
