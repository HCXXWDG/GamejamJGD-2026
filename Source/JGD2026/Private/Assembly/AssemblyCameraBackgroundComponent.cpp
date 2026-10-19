#include "Assembly/AssemblyCameraBackgroundComponent.h"
#include "Assembly/AssemblyPlayerExperienceComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "SceneView.h"
#include "UObject/ConstructorHelpers.h"

UAssemblyCameraBackgroundComponent::UAssemblyCameraBackgroundComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	// CameraManager updates between PostPhysics and PostUpdateWork. Blueprint Tick
	// writes the legacy camera X earlier; our parameter must describe the rendered view.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> Parameters(
		TEXT("/Game/Res/Art/Material/MPC_Bacground.MPC_Bacground"));
	ParallaxParameters = Parameters.Object;
}

void UAssemblyCameraBackgroundComponent::BeginPlay()
{
	Super::BeginPlay();
	AddTickPrerequisiteActor(GetOwner());
	if (UAssemblyPlayerExperienceComponent* Experience = GetOwner()->FindComponentByClass<UAssemblyPlayerExperienceComponent>())
		AddTickPrerequisiteComponent(Experience);
	CacheBackgroundLayers();
}

void UAssemblyCameraBackgroundComponent::CacheBackgroundLayers()
{
	for (UStaticMeshComponent* Mesh : TInlineComponentArray<UStaticMeshComponent*>(GetOwner()))
	{
		if (!Mesh || !Mesh->GetStaticMesh()) continue;
		bool bBackground = false;
		for (UMaterialInterface* Material : Mesh->GetMaterials())
		{
			const UMaterial* Base = Material ? Material->GetBaseMaterial() : nullptr;
			bBackground |= Base && Base->GetFName() == TEXT("M_Bacground");
		}
		if (!bBackground) continue;
		FVector Min, Max;
		Mesh->GetLocalBounds(Min, Max);
		FBackgroundLayer& Layer = Layers.AddDefaulted_GetRef();
		Layer.Mesh = Mesh;
		Layer.OriginalTransform = Mesh->GetComponentTransform();
		Layer.LocalCenter = (Min + Max) * .5;
		const FBoxSphereBounds Bounds = Mesh->CalcBounds(Layer.OriginalTransform);
		Layer.WorldSize = Bounds.BoxExtent * 2;
		Layer.Depth = Bounds.Origin.Y;
		Layer.OriginalMobility = Mesh->Mobility;
		Layer.OriginalCollision = Mesh->GetCollisionEnabled();
		Layer.bOriginalGenerateOverlap = Mesh->GetGenerateOverlapEvents();
		float Ratio = 0.2f;
		if (UMaterialInterface* Material = Mesh->GetMaterial(0))
			Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Ratio")), Ratio);
		Layer.Parallax = FMath::Clamp(Ratio, 0.0f, 1.0f);
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			Layer.OriginalMaterials.Add(Mesh->GetMaterial(Slot));
			if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(Slot))
				Material->SetScalarParameterValue(TEXT("Ratio"), 0.0f);
		}
		// These planes are decoration. Following the camera must not move colliders
		// through the physical assembly or generate gameplay overlaps.
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Mesh->Mobility != EComponentMobility::Movable) Mesh->SetMobility(EComponentMobility::Movable);
		// Repeating geometry replaces camera-driven UV scrolling. Three columns cover
		// the viewport and wrap outside it without dragging the artwork back on screen.
		for (int32 Side : { -1, 1 })
		{
			UStaticMeshComponent* Tile = NewObject<UStaticMeshComponent>(GetOwner());
			GetOwner()->AddInstanceComponent(Tile);
			Tile->SetStaticMesh(Mesh->GetStaticMesh());
			Tile->SetMobility(EComponentMobility::Movable);
			Tile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Tile->SetGenerateOverlapEvents(false);
			Tile->SetCastShadow(false);
			Tile->TranslucencySortPriority = Mesh->TranslucencySortPriority;
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) Tile->SetMaterial(Slot, Mesh->GetMaterial(Slot));
			Tile->RegisterComponent();
			FTransform Initial = Layer.OriginalTransform;
			Initial.AddToTranslation(FVector(Side * Layer.WorldSize.X, 0, 0));
			Tile->SetWorldTransform(Initial);
			Layer.Tiles.Add(Tile);
		}
	}
}

void UAssemblyCameraBackgroundComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(DeltaTime, TickType, Tick);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager) return;
	const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
	if (View.ProjectionMode != ECameraProjectionMode::Orthographic) return;
	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->ViewportClient->Viewport) return;
	FSceneViewProjectionData Projection;
	if (!LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, Projection)) return;
	const double X = FMath::Abs(Projection.ProjectionMatrix.M[0][0]);
	const double Z = FMath::Abs(Projection.ProjectionMatrix.M[1][1]);
	if (X < UE_SMALL_NUMBER || Z < UE_SMALL_NUMBER) return;
	// Use the final projection: OrthoWidth alone ignores MaintainYFOV and resizing.
	const double Width = 2.0 / X;
	const double Height = 2.0 / Z;
	if (Layers.IsEmpty()) CacheBackgroundLayers();
	if (!bHaveCameraOrigin) { CameraOrigin = View.Location; bHaveCameraOrigin = true; }
	const FVector Travel = View.Location - CameraOrigin;
	for (const FBackgroundLayer& Layer : Layers)
	{
		UStaticMeshComponent* Mesh = Layer.Mesh.Get();
		if (!Mesh || Layer.WorldSize.X < UE_SMALL_NUMBER || Layer.WorldSize.Z < UE_SMALL_NUMBER) continue;
		const double Scale = FMath::Max3(1.0, Width * 1.04 / Layer.WorldSize.X, Height * 1.35 / Layer.WorldSize.Z);
		const FVector WorldScale = Layer.OriginalTransform.GetScale3D() * Scale;
		const FQuat Rotation = Layer.OriginalTransform.GetRotation();
		const double TileWidth = Layer.WorldSize.X * Scale;
		const double VerticalMargin = (Layer.WorldSize.Z * Scale - Height) * 0.49;
		FVector Center = CameraOrigin + Travel * (1.0 - Layer.Parallax);
		Center.Y = Layer.Depth;
		Center.X += FMath::RoundToDouble((View.Location.X - Center.X) / TileWidth) * TileWidth;
		// Pan vertically with the world, but never repeat sky/ground vertically.
		Center.Z = FMath::Clamp(Center.Z, View.Location.Z - VerticalMargin, View.Location.Z + VerticalMargin);
		// SM_Plane has a corner pivot; its origin is not the center of the artwork.
		const FVector Origin = Center - Rotation.RotateVector(Layer.LocalCenter * WorldScale);
		Mesh->SetWorldTransform(FTransform(Rotation, Origin, WorldScale), false, nullptr, ETeleportType::TeleportPhysics);
		for (int32 Index = 0; Index < Layer.Tiles.Num(); ++Index)
			if (UStaticMeshComponent* Tile = Layer.Tiles[Index].Get())
				Tile->SetWorldTransform(FTransform(Rotation, Origin + FVector((Index == 0 ? -1.0 : 1.0) * TileWidth, 0, 0), WorldScale));
	}
	if (ParallaxParameters)
		UKismetMaterialLibrary::SetScalarParameterValue(this, ParallaxParameters, TEXT("CameraXPos"), 0.0f);
}

void UAssemblyCameraBackgroundComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const FBackgroundLayer& Layer : Layers)
	{
		for (const auto& Tile : Layer.Tiles) if (Tile.IsValid()) Tile->DestroyComponent();
		if (UStaticMeshComponent* Mesh = Layer.Mesh.Get())
		{
			for (int32 Slot = 0; Slot < Layer.OriginalMaterials.Num(); ++Slot)
				if (Layer.OriginalMaterials[Slot].IsValid()) Mesh->SetMaterial(Slot, Layer.OriginalMaterials[Slot].Get());
			Mesh->SetWorldTransform(Layer.OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
			if (Mesh->Mobility != Layer.OriginalMobility) Mesh->SetMobility(Layer.OriginalMobility);
			Mesh->SetCollisionEnabled(Layer.OriginalCollision);
			Mesh->SetGenerateOverlapEvents(Layer.bOriginalGenerateOverlap);
		}
	}
	Layers.Reset();
	Super::EndPlay(Reason);
}
