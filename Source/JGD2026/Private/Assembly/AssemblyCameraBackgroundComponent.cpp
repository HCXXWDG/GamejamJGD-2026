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
		// These planes are decoration. Following the camera must not move colliders
		// through the physical assembly or generate gameplay overlaps.
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Mesh->Mobility != EComponentMobility::Movable) Mesh->SetMobility(EComponentMobility::Movable);
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
	for (const FBackgroundLayer& Layer : Layers)
	{
		UStaticMeshComponent* Mesh = Layer.Mesh.Get();
		if (!Mesh || Layer.WorldSize.X < UE_SMALL_NUMBER || Layer.WorldSize.Z < UE_SMALL_NUMBER) continue;
		const double Scale = FMath::Max3(1.0, Width * 1.04 / Layer.WorldSize.X, Height * 1.04 / Layer.WorldSize.Z);
		const FVector WorldScale = Layer.OriginalTransform.GetScale3D() * Scale;
		const FQuat Rotation = Layer.OriginalTransform.GetRotation();
		const FVector Center(View.Location.X, Layer.Depth, View.Location.Z);
		// SM_Plane has a corner pivot; its origin is not the center of the artwork.
		const FVector Origin = Center - Rotation.RotateVector(Layer.LocalCenter * WorldScale);
		Mesh->SetWorldTransform(FTransform(Rotation, Origin, WorldScale), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (ParallaxParameters)
		UKismetMaterialLibrary::SetScalarParameterValue(this, ParallaxParameters, TEXT("CameraXPos"), View.Location.X);
}

void UAssemblyCameraBackgroundComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const FBackgroundLayer& Layer : Layers)
		if (UStaticMeshComponent* Mesh = Layer.Mesh.Get())
		{
			Mesh->SetWorldTransform(Layer.OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
			if (Mesh->Mobility != Layer.OriginalMobility) Mesh->SetMobility(Layer.OriginalMobility);
			Mesh->SetCollisionEnabled(Layer.OriginalCollision);
			Mesh->SetGenerateOverlapEvents(Layer.bOriginalGenerateOverlap);
		}
	Layers.Reset();
	Super::EndPlay(Reason);
}
