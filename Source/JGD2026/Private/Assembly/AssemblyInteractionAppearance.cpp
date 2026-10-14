#include "Assembly/AssemblyInteractionComponent.h"

#include "Assembly/AssemblyAppearanceData.h"
#include "Assembly/AssemblyComponent.h"
#include "AssemblyRenderLayers.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"

namespace
{
	bool GetUsableSpriteBounds(UPaperSprite* Sprite, FBoxSphereBounds& OutBounds)
	{
		if (!IsValid(Sprite)) { return false; }
		OutBounds = Sprite->GetRenderBounds();
		return !OutBounds.Origin.ContainsNaN() && !OutBounds.BoxExtent.ContainsNaN()
			&& OutBounds.BoxExtent.X > UE_DOUBLE_SMALL_NUMBER && OutBounds.BoxExtent.Z > UE_DOUBLE_SMALL_NUMBER;
	}

	bool IsUsableDiameter(float Diameter)
	{
		return FMath::IsFinite(Diameter) && Diameter > UE_SMALL_NUMBER;
	}

	bool IsBreakable(EAssemblyJointKind Kind)
	{
		return Kind == EAssemblyJointKind::BreakableHinge || Kind == EAssemblyJointKind::BreakableFixed;
	}

	void DestroySpriteView(UPaperSpriteComponent* Component)
	{
		if (!IsValid(Component)) { return; }
		if (AActor* Owner = Component->GetOwner()) { Owner->RemoveInstanceComponent(Component); }
		Component->DestroyComponent();
	}

	UPaperSpriteComponent* CreateSpriteView(AActor* Owner, USceneComponent* Parent, bool bAbsolute)
	{
		if (!IsValid(Owner) || Owner->IsActorBeingDestroyed()) { return nullptr; }
		UPaperSpriteComponent* Component = NewObject<UPaperSpriteComponent>(Owner, NAME_None, RF_Transient);
		Owner->AddInstanceComponent(Component);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		if (Parent) { Component->SetupAttachment(Parent); }
		Component->SetAbsolute(bAbsolute, bAbsolute, bAbsolute);
		Component->RegisterComponent();
		return Component;
	}

	void ApplySpriteStyle(UPaperSpriteComponent* Component, UPaperSprite* Sprite,
		const UAssemblyAppearanceData* Appearance, bool bMarker = false)
	{
		if (!IsValid(Component) || !IsValid(Sprite) || !IsValid(Appearance)) { return; }
		Component->SetSprite(Sprite);
		Component->SetMaterial(0, Sprite->GetDefaultMaterial());
		Component->SetSpriteColor(Appearance->Tint);
		Component->SetTranslucentSortPriority(Appearance->TranslucencySortPriority + (bMarker ? 1 : 0));
	}

	bool MakeSpriteTransform(UPaperSprite* Sprite, float Diameter, const FQuat& Rotation,
		const FVector& Center, FTransform& OutTransform)
	{
		FBoxSphereBounds Bounds;
		if (!GetUsableSpriteBounds(Sprite, Bounds) || !IsUsableDiameter(Diameter)
			|| Center.ContainsNaN() || Rotation.ContainsNaN()) { return false; }
		const double Scale = Diameter / (2.0 * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Z));
		const FVector VisualScale(Scale, 1.0, Scale);
		// Source regions retain their original 64-pixel blocks; compensate their render-bounds center without editing the image.
		const FVector PivotOffset = Rotation.RotateVector(Bounds.Origin * VisualScale);
		OutTransform = FTransform(Rotation, Center - PivotOffset, VisualScale);
		return !OutTransform.ContainsNaN();
	}

	void SetSpritePose(UPaperSpriteComponent* Component, float Diameter,
		const FQuat& Rotation, const FVector& Center)
	{
		if (!IsValid(Component)) { return; }
		FTransform Transform;
		const bool bValid = MakeSpriteTransform(Component->GetSprite(), Diameter, Rotation, Center, Transform);
		Component->SetVisibility(bValid, false);
		if (bValid) { Component->SetWorldTransform(Transform); }
	}
}

void UAssemblyInteractionComponent::ResolveAppearanceData()
{
	LoadedAppearanceData = AppearanceData.IsNull() ? nullptr : AppearanceData.LoadSynchronous();
}

void UAssemblyInteractionComponent::RefreshAppearance()
{
	if (!BoundAssembly.IsValid()) { return; }
	ResolveAppearanceData();
	SynchronizeAppearanceViews();
	OnViewsChanged.Broadcast();
}

UPaperSprite* UAssemblyInteractionComponent::GetCoreArtSprite() const
{
	return IsValid(CoreArtView) && CoreArtView->IsVisible() ? CoreArtView->GetSprite() : nullptr;
}

UPaperSprite* UAssemblyInteractionComponent::GetJointArtSprite(FGuid InstanceId) const
{
	const TObjectPtr<UPaperSpriteComponent>* View = JointArtViews.Find(InstanceId);
	if (View && IsValid(View->Get()) && View->Get()->IsVisible()) return View->Get()->GetSprite();
	if (BoundAssembly.IsValid())
	{
		for (const FAssemblyJointInstance& Joint : BoundAssembly->GetJoints())
		{
			if (Joint.InstanceId != InstanceId) continue;
			for (FGuid PartId : { Joint.BoneA, Joint.BoneB })
			{
				FAssemblyBoneInstance Part;
				if (BoundAssembly->IsJointPart(PartId) && BoundAssembly->GetBone(PartId, Part))
					return GetJointPartArtSprite(Part.TypeId);
			}
		}
	}
	return nullptr;
}

bool UAssemblyInteractionComponent::HasJointArt(FGuid InstanceId) const
{
	return IsValid(GetJointArtSprite(InstanceId));
}

bool UAssemblyInteractionComponent::GetJointWorldAnchors(const FAssemblyJointInstance& Joint,
	FVector& OutA, FVector& OutB) const
{
	OutA = FVector::ZeroVector;
	OutB = FVector::ZeroVector;
	if (!BoundAssembly.IsValid()) { return false; }
	FTransform PoseB;
	if (!BoundAssembly->GetBoneWorldTransform(Joint.BoneB, PoseB)) { return false; }
	OutB = PoseB.TransformPosition(FVector(Joint.LocalAnchorB.X, 0.0, Joint.LocalAnchorB.Y));
	if (Joint.BoneA.IsValid())
	{
		FTransform PoseA;
		if (!BoundAssembly->GetBoneWorldTransform(Joint.BoneA, PoseA)) { return false; }
		OutA = PoseA.TransformPosition(FVector(Joint.LocalAnchorA.X, 0.0, Joint.LocalAnchorA.Y));
	}
	else if (UPrimitiveComponent* Core = GetCoreBody(); IsValid(Core))
	{
		OutA = Core->GetComponentTransform().TransformPosition(FVector(Joint.LocalAnchorA.X, 0.0, Joint.LocalAnchorA.Y));
	}
	else
	{
		const FVector2D Anchor = BoundAssembly->GetFreeSettings().CoreLocalPosition + Joint.LocalAnchorA;
		OutA = BoundAssembly->GetGridFrameTransform().TransformPosition(FVector(Anchor.X, 0.0, Anchor.Y));
	}
	return !OutA.ContainsNaN() && !OutB.ContainsNaN();
}

void UAssemblyInteractionComponent::SynchronizeAppearanceViews()
{
	if (!BoundAssembly.IsValid() || !IsValid(LoadedAppearanceData)
		|| !FMath::IsFinite(LoadedAppearanceData->PlaneDepthOffset))
	{
		DestroyAppearanceViews();
		return;
	}
	UAssemblyAppearanceData* Appearance = LoadedAppearanceData;
	FBoxSphereBounds Bounds;
	UPrimitiveComponent* Core = GetCoreBody();
	if (IsValid(Core) && GetUsableSpriteBounds(Appearance->CoreSprite, Bounds))
	{
		if (!IsValid(CoreArtView)) { CoreArtView = CreateSpriteView(GetOwner(), Core, false); }
		if (IsValid(CoreArtView))
		{
			if (CoreArtView->GetAttachParent() != Core)
			{
				CoreArtView->AttachToComponent(Core, FAttachmentTransformRules::KeepRelativeTransform);
			}
			ApplySpriteStyle(CoreArtView, Appearance->CoreSprite, Appearance);
		}
	}
	else
	{
		DestroySpriteView(CoreArtView);
		CoreArtView = nullptr;
	}

	TSet<FGuid> JointIds;
	TSet<FGuid> MarkerIds;
	for (const FAssemblyJointInstance& Joint : BoundAssembly->GetJoints())
	{
		const TObjectPtr<UPaperSprite>* Sprite = Appearance->JointSprites.Find(Joint.Kind);
		if (!Joint.InstanceId.IsValid()) continue;
		const bool bHasIndependentPart = BoundAssembly->IsJointPart(Joint.BoneA) || BoundAssembly->IsJointPart(Joint.BoneB);
		if (!bHasIndependentPart && Sprite && GetUsableSpriteBounds(Sprite->Get(), Bounds)
			&& IsUsableDiameter(Appearance->JointDiameter))
		{
			// Independent joint parts already have their complete artwork; do not stack a legacy cap over it.
			JointIds.Add(Joint.InstanceId);
			TObjectPtr<UPaperSpriteComponent>& View = JointArtViews.FindOrAdd(Joint.InstanceId);
			if (!IsValid(View)) { View = CreateSpriteView(GetOwner(), GetOwner()->GetRootComponent(), true); }
			ApplySpriteStyle(View, Sprite->Get(), Appearance);
		}
		if (IsBreakable(Joint.Kind) && GetUsableSpriteBounds(Appearance->BreakableJointMarker, Bounds)
			&& IsUsableDiameter(Appearance->BreakableMarkerDiameter))
		{
			MarkerIds.Add(Joint.InstanceId);
			TObjectPtr<UPaperSpriteComponent>& Marker = BreakableMarkerViews.FindOrAdd(Joint.InstanceId);
			if (!IsValid(Marker)) { Marker = CreateSpriteView(GetOwner(), GetOwner()->GetRootComponent(), true); }
			ApplySpriteStyle(Marker, Appearance->BreakableJointMarker, Appearance, true);
		}
	}
	for (auto It = JointArtViews.CreateIterator(); It; ++It)
	{
		if (!JointIds.Contains(It.Key())) { DestroySpriteView(It.Value()); It.RemoveCurrent(); }
	}
	for (auto It = BreakableMarkerViews.CreateIterator(); It; ++It)
	{
		if (!MarkerIds.Contains(It.Key())) { DestroySpriteView(It.Value()); It.RemoveCurrent(); }
	}
	UpdateAppearanceTransforms();
}

void UAssemblyInteractionComponent::UpdateAppearanceTransforms()
{
	if (!BoundAssembly.IsValid() || !IsValid(LoadedAppearanceData)) { return; }
	const UAssemblyAppearanceData* Appearance = LoadedAppearanceData;
	UPrimitiveComponent* Core = GetCoreBody();
	if (IsValid(CoreArtView))
	{
		FTransform Relative;
		const FVector Center(0.0, Appearance->PlaneDepthOffset, 0.0);
		const bool bValid = IsValid(Core) && FMath::IsFinite(Appearance->PlaneDepthOffset)
			&& MakeSpriteTransform(CoreArtView->GetSprite(), BoundAssembly->GetFreeSettings().CoreRadius * 2.0f,
				FQuat::Identity, Center, Relative);
		CoreArtView->SetVisibility(bValid, false);
		if (bValid)
		{
			if (CoreArtView->GetAttachParent() != Core)
			{
				CoreArtView->AttachToComponent(Core, FAttachmentTransformRules::KeepRelativeTransform);
			}
			CoreArtView->SetRelativeTransform(Relative);
		}
	}
	// Only our own white-model mesh is hidden. Existing physical bodies and their rendering are never changed.
	if (IsValid(CreatedCoreVisual)) { CreatedCoreVisual->SetVisibility(GetCoreArtSprite() == nullptr, false); }
	for (const FAssemblyJointInstance& Joint : BoundAssembly->GetJoints())
	{
		TObjectPtr<UPaperSpriteComponent>* View = JointArtViews.Find(Joint.InstanceId);
		TObjectPtr<UPaperSpriteComponent>* Marker = BreakableMarkerViews.Find(Joint.InstanceId);
		if (!View && !Marker) { continue; }
		FVector A, B;
		FTransform BonePose;
		if (!GetJointWorldAnchors(Joint, A, B) || !BoundAssembly->GetBoneWorldTransform(Joint.BoneB, BonePose))
		{
			if (View && IsValid(View->Get())) { View->Get()->SetVisibility(false, false); }
			if (Marker && IsValid(Marker->Get())) { Marker->Get()->SetVisibility(false, false); }
			continue;
		}
		const FQuat Rotation = BonePose.GetRotation();
		const FVector JointCenter = (A + B) * 0.5;
		const int32 Rank = AssemblyRenderLayers::GetRank(BoundAssembly->GetJoints(), Joint.InstanceId);
		const double InstanceDepth = Rank * AssemblyRenderLayers::DepthStep;
		if (View)
		{
			// Masked Sprite materials use the depth buffer, so separate overlapping visual layers.
			SetSpritePose(View->Get(), Appearance->JointDiameter, Rotation,
				JointCenter + Rotation.RotateVector(FVector(0.0, Appearance->PlaneDepthOffset + 0.1f + InstanceDepth, 0.0)));
		}
		if (Marker)
		{
			const double Offset = Appearance->JointDiameter * 0.35;
			SetSpritePose(Marker->Get(), Appearance->BreakableMarkerDiameter, Rotation,
				JointCenter + Rotation.RotateVector(FVector(Offset, Appearance->PlaneDepthOffset + 0.2f
					+ BoundAssembly->GetJoints().Num() * AssemblyRenderLayers::DepthStep + InstanceDepth, Offset)));
		}
	}
}

void UAssemblyInteractionComponent::DestroyAppearanceViews()
{
	DestroySpriteView(CoreArtView);
	CoreArtView = nullptr;
	for (const auto& Item : JointArtViews) { DestroySpriteView(Item.Value); }
	JointArtViews.Reset();
	for (const auto& Item : BreakableMarkerViews) { DestroySpriteView(Item.Value); }
	BreakableMarkerViews.Reset();
	if (IsValid(CreatedCoreVisual)) { CreatedCoreVisual->SetVisibility(true, false); }
}
