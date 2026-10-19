#include "UI/JGDLoadingScreenSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "MoviePlayer.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SThrobber.h"

DEFINE_LOG_CATEGORY_STATIC(LogJGDLoading, Log, All);
namespace { constexpr double MinimumDisplaySeconds = 1.0; }

bool UJGDLoadingScreenSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer() && !IsRunningCommandlet();
}

void UJGDLoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Poster = PosterAsset.LoadSynchronous();
	if (Poster)
	{
		PosterBrush.SetResourceObject(Poster);
		PosterBrush.ImageSize = FVector2D(1920, 1080);
		PosterBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	PreLoadHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::BeginMapLoad);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::EndMapLoad);
	if (IsMoviePlayerEnabled())
		MovieFinishedHandle = GetMoviePlayer()->OnMoviePlaybackFinished().AddUObject(this, &ThisClass::MovieFinished);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickLoading));
}

void UJGDLoadingScreenSubsystem::BeginMapLoad(const FWorldContext& Context, const FString& MapName)
{
	if (Context.OwningGameInstance != GetGameInstance() || !FSlateApplication::IsInitialized()) return;
	RemovePreview();
	StartedAt = FPlatformTime::Seconds();
	bLoading = true;
	bMapReady = false;
	bUsingMovie = false;
	if (Context.WorldType != EWorldType::PIE && IsMoviePlayerEnabled() && GetMoviePlayer()->IsInitialized())
	{
		FLoadingScreenAttributes Attributes;
		Attributes.MoviePaths.Add(TEXT("JGD_Loading"));
		Attributes.MinimumLoadingScreenDisplayTime = MinimumDisplaySeconds;
		Attributes.bAutoCompleteWhenLoadingCompletes = true;
		Attributes.bMoviesAreSkippable = false;
		Attributes.PlaybackType = MT_Looped;
		GetMoviePlayer()->SetupLoadingScreen(Attributes);
		// The engine's following PreLoadMap callback starts playback and installs its
		// PostLoadMap wait. Starting it here would bypass that matched lifecycle.
		bUsingMovie = true;
	}
	else ShowPreview();
	UE_LOG(LogJGDLoading, Log, TEXT("Begin map loading: %s (%s)"), *MapName, bUsingMovie ? TEXT("movie") : TEXT("PIE preview"));
}

void UJGDLoadingScreenSubsystem::EndMapLoad(UWorld* World)
{
	if (!bLoading || (World && World->GetGameInstance() != GetGameInstance())) return;
	bMapReady = true;
	if (!World)
	{
		if (bUsingMovie && IsMoviePlayerEnabled()) GetMoviePlayer()->StopMovie();
		bUsingMovie = false;
		RemovePreview();
		bLoading = false;
		return;
	}
	if (bUsingMovie && !GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		// Keep a usable presentation if the platform cannot initialize its decoder.
		bUsingMovie = false;
		ShowPreview();
	}
}

void UJGDLoadingScreenSubsystem::MovieFinished()
{
	if (!bUsingMovie) return;
	bUsingMovie = false;
	bLoading = false;
	++CompletedTransitions;
	UE_LOG(LogJGDLoading, Log, TEXT("Loading movie completed"));
}

void UJGDLoadingScreenSubsystem::ShowPreview()
{
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	if (!Viewport || Preview) return;
	PreviewViewport = Viewport;
	Preview = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black).Padding(0)
	.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
	.OnMouseButtonUp_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
			[SNew(SImage).Image(&PosterBrush)]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(32, 32, 48, 30)
		[SNew(SThrobber).NumPieces(3)]
	];
	Viewport->AddViewportWidgetContent(Preview.ToSharedRef(), MAX_int32);
}

void UJGDLoadingScreenSubsystem::RemovePreview()
{
	if (Preview && PreviewViewport.IsValid()) PreviewViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
	Preview.Reset();
	PreviewViewport.Reset();
}

bool UJGDLoadingScreenSubsystem::TickLoading(float DeltaTime)
{
	if (bLoading && bMapReady && !bUsingMovie && FPlatformTime::Seconds() - StartedAt >= MinimumDisplaySeconds)
	{
		RemovePreview();
		bLoading = false;
		++CompletedTransitions;
		UE_LOG(LogJGDLoading, Log, TEXT("Loading preview completed"));
	}
	return true;
}

void UJGDLoadingScreenSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	if (IsMoviePlayerEnabled())
	{
		GetMoviePlayer()->OnMoviePlaybackFinished().Remove(MovieFinishedHandle);
		if (bUsingMovie) GetMoviePlayer()->StopMovie();
	}
	RemovePreview();
	Poster = nullptr;
	bLoading = false;
	Super::Deinitialize();
}
