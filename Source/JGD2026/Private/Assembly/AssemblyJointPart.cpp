#include "Assembly/AssemblyJointPart.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Components/BoxComponent.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AssemblyJointPart"

AAssemblyJointPart::AAssemblyJointPart()
{
	const TCHAR* Names[] = { TEXT("Round"), TEXT("Bar"), TEXT("Corner"), TEXT("Tee"), TEXT("Cross"), TEXT("Fork"), TEXT("FixedRound") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Game/Res/Art/Sprites/SP_Joint_%s.SP_Joint_%s"), Names[Index], Names[Index]);
		ConstructorHelpers::FObjectFinder<UPaperSprite> Sprite(*Path);
		if (Sprite.Succeeded()) ShapeSprites.Add(static_cast<EAssemblyJointPartShape>(Index), Sprite.Object);
	}
	BoneSprite->SetSprite(GetSpriteForShape(PreviewShape));
	// Category depth places joint artwork behind bones; physical bodies stay on the gameplay plane.
	VisualDepthOffset = 0.0f;
}

UPaperSprite* AAssemblyJointPart::GetSpriteForShape(EAssemblyJointPartShape Shape) const
{
	const TObjectPtr<UPaperSprite>* Sprite = ShapeSprites.Find(Shape);
	return Sprite && IsValid(Sprite->Get()) ? Sprite->Get() : nullptr;
}

FAssemblyBoneDefinition AAssemblyJointPart::MakeJointPartDefinition(EAssemblyJointPartShape Shape, float PixelScale)
{
	return AssemblyRigidPartMetrics::MakeJointPartDefinition(Shape, PixelScale);
}

void AAssemblyJointPart::OnConstruction(const FTransform& Transform)
{
	if (!IsBoneInitialized())
	{
		FText Reason;
		ApplyRigidPartDefinition(MakeJointPartDefinition(PreviewShape, ArtUnitsPerPixel), Reason);
	}
	Super::OnConstruction(Transform);
}

bool AAssemblyJointPart::ApplyRigidPartDefinition(const FAssemblyBoneDefinition& Definition, FText& OutReason)
{
	if (Definition.PartKind != EAssemblyRigidPartKind::JointPart)
	{
		OutReason = LOCTEXT("WrongDefinition", "关节件 Actor 只能展示独立关节件定义。");
		return false;
	}
	UPaperSprite* Sprite = GetSpriteForShape(Definition.JointShape);
	if (!Sprite)
	{
		OutReason = LOCTEXT("MissingArt", "缺少此关节形状的 Sprite，请检查 Shape Sprites 配置。");
		return false;
	}
	if (GetBoneBody()->IsSimulatingPhysics()
		|| (IsBoneInitialized() && GetAssemblyComponent()->GetPhase() != EAssemblyPhase::Assembly))
	{
		OutReason = LOCTEXT("PhysicalOwnership", "物理模拟或操控期间不能修改关节件几何。");
		return false;
	}
	FNormalBonePhysicalStats Stats;
	if (!AssemblyRigidPartMetrics::Evaluate(Definition, Stats, OutReason)) return false;
	if (!FMath::IsFinite(Sprite->GetPixelsPerUnrealUnit()) || Sprite->GetPixelsPerUnrealUnit() <= UE_SMALL_NUMBER)
	{
		OutReason = LOCTEXT("InvalidPixelScale", "关节 Sprite 的 Pixels Per Unreal Unit 必须是有效正数。");
		return false;
	}
	UPaperSprite* PreviousSprite = BoneSprite->GetSprite();
	UMaterialInterface* PreviousMaterial = BoneSprite->GetMaterial(0);
	BoneSprite->SetSprite(Sprite);
	BoneSprite->SetMaterial(0, Sprite->GetDefaultMaterial());
	if (!Super::ApplyRigidPartDefinition(Definition, OutReason))
	{
		BoneSprite->SetSprite(PreviousSprite);
		BoneSprite->SetMaterial(0, PreviousMaterial);
		return false;
	}
	return true;
}

EAssemblyJointPartShape AAssemblyJointPart::GetJointShape() const
{
	return GetRigidPartDefinition().JointShape;
}

TArray<FVector2D> AAssemblyJointPart::GetConnectionLocalPositions() const
{
	return AssemblyRigidPartMetrics::GetConnectionPoints(GetRigidPartDefinition());
}

TArray<FVector> AAssemblyJointPart::GetConnectionWorldPositions() const
{
	TArray<FVector> Result;
	const FTransform Pose = BoneBody->GetComponentTransform();
	if (Pose.ContainsNaN()) return Result;
	for (const FVector2D Point : GetConnectionLocalPositions())
	{
		Result.Add(Pose.TransformPosition(FVector(Point.X, 0.0, Point.Y)));
	}
	return Result;
}

FText AAssemblyJointPart::GetJointPartDisplayName() const
{
	return GetRigidPartDefinition().DisplayName;
}

#undef LOCTEXT_NAMESPACE
