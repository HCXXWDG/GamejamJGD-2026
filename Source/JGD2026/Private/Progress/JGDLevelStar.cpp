#include "Progress/JGDLevelStar.h"

#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "UObject/ConstructorHelpers.h"

AJGDLevelStar::AJGDLevelStar()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StarOrigin")));
	Artwork = CreateDefaultSubobject<UPaperSpriteComponent>(TEXT("StarArtwork"));
	Artwork->SetupAttachment(RootComponent);
	Artwork->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Artwork->SetGenerateOverlapEvents(false);
	Artwork->SetCastShadow(false);
	Artwork->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UPaperSprite> Sprite(TEXT("/Game/Res/Art/Sprites/SP_LevelStar.SP_LevelStar"));
	Artwork->SetSprite(Sprite.Object);
}

void AJGDLevelStar::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (UPaperSprite* Sprite = Artwork->GetSprite())
	{
		const FBoxSphereBounds Bounds = Sprite->GetRenderBounds();
		const double Size = 2 * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Z);
		const double Scale = Size > UE_SMALL_NUMBER ? DisplaySize / Size : 1;
		Artwork->SetRelativeScale3D(FVector(Scale, 1, Scale));
		Artwork->SetRelativeLocation(FVector(0, 12, 0) - Bounds.Origin * FVector(Scale, 1, Scale));
	}
}

void AJGDLevelStar::SetCollected(bool bValue)
{
	bCollected = bValue;
	SetActorHiddenInGame(bValue);
	SetActorTickEnabled(!bValue);
	if (!bValue) { BobTime = 0; OnConstruction(GetActorTransform()); }
}

void AJGDLevelStar::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	BobTime += DeltaTime;
	FVector Location = Artwork->GetRelativeLocation();
	Location.Z += (FMath::Sin(BobTime * 2.4f) - FMath::Sin((BobTime - DeltaTime) * 2.4f)) * 4;
	Artwork->SetRelativeLocation(Location);
}
