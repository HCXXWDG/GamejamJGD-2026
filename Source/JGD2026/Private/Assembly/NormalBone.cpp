#include "Assembly/NormalBone.h"
#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "AssemblyRules.h"
#include "AssemblyRenderLayers.h"
#include "Components/BoxComponent.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "NormalBone"

namespace
{
	bool IsUsableTransform(const FTransform& Transform)
	{
		const FVector Scale = Transform.GetScale3D();
		return !Transform.ContainsNaN() && !FMath::IsNearlyZero(Scale.X)
			&& !FMath::IsNearlyZero(Scale.Y) && !FMath::IsNearlyZero(Scale.Z);
	}

	bool FitsFootprintBounds(const TArray<FIntPoint>& Shape, float CellSize, const FNormalBonePhysicalStats& Stats)
	{
		if (Shape.IsEmpty())
		{
			return false;
		}
		FIntPoint Min = Shape[0], Max = Shape[0];
		for (const FIntPoint Cell : Shape)
		{
			Min.X = FMath::Min(Min.X, Cell.X);
			Min.Y = FMath::Min(Min.Y, Cell.Y);
			Max.X = FMath::Max(Max.X, Cell.X);
			Max.Y = FMath::Max(Max.Y, Cell.Y);
		}
		return Stats.Length <= (static_cast<int64>(Max.X) - Min.X + 1) * CellSize + UE_KINDA_SMALL_NUMBER
			&& Stats.Thickness <= (static_cast<int64>(Max.Y) - Min.Y + 1) * CellSize + UE_KINDA_SMALL_NUMBER;
	}
}

ANormalBone::ANormalBone()
{
	PrimaryActorTick.bCanEverTick = false;
	BoneBody = CreateDefaultSubobject<UBoxComponent>(TEXT("BoneBody"));
	SetRootComponent(BoneBody);
	BoneBody->InitBoxExtent(FVector(64.0, 4.0, 6.4));
	BoneBody->SetMobility(EComponentMobility::Movable);
	BoneBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoneBody->SetCollisionObjectType(ECC_PhysicsBody);
	BoneBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoneBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BoneBody->SetGenerateOverlapEvents(false);
	BoneBody->SetSimulatePhysics(false);
	BoneBody->SetCastShadow(false);

	BoneSprite = CreateDefaultSubobject<UPaperSpriteComponent>(TEXT("BoneSprite"));
	BoneSprite->SetupAttachment(BoneBody);
	BoneSprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoneSprite->SetGenerateOverlapEvents(false);
	BoneSprite->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UPaperSprite> Placeholder(
		TEXT("/Game/Character/Assembly/SP_BonePlaceholder.SP_BonePlaceholder"));
	if (Placeholder.Succeeded())
	{
		BoneSprite->SetSprite(Placeholder.Object);
	}
}

UAssemblyComponent* ANormalBone::GetAssemblyComponent() const
{
	return BoundAssembly.Get();
}

FNormalBoneParameters ANormalBone::GetBoneParameters() const
{
	return IsBoneInitialized() ? InstalledParameters : PreviewParameters;
}

FNormalBonePhysicalStats ANormalBone::GetPhysicalStats() const
{
	FNormalBonePhysicalStats Stats;
	FText Reason;
	AssemblyRigidPartMetrics::Evaluate(GetRigidPartDefinition(), Stats, Reason);
	return Stats;
}

FAssemblyBoneDefinition ANormalBone::GetRigidPartDefinition() const
{
	FAssemblyBoneDefinition Definition = bHasAppliedDefinition ? AppliedDefinition : FAssemblyBoneDefinition();
	Definition.BoneParameters = GetBoneParameters();
	return Definition;
}

bool ANormalBone::ApplyRigidPartDefinition(const FAssemblyBoneDefinition& Definition, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (IsBoneInitialized() && !bApplyingModelDefinition)
	{
		OutReason = LOCTEXT("ModelOwnsDefinition", "已安装组件的几何由组装模型管理；此接口仅用于未安装的预览。");
		return false;
	}
	if (BoneBody->IsSimulatingPhysics()
		|| (IsBoneInitialized() && BoundAssembly->GetPhase() != EAssemblyPhase::Assembly))
	{
		OutReason = LOCTEXT("CannotEditGeometry", "物理模拟或操控期间不能修改刚体几何。");
		return false;
	}
	FNormalBonePhysicalStats Stats;
	if (!AssemblyRigidPartMetrics::Evaluate(Definition, Stats, OutReason) || !HasValidGeometry(Stats))
	{
		if (OutReason.IsEmpty()) OutReason = LOCTEXT("InvalidPartArt", "组件 Sprite 或几何尺寸无效。");
		return false;
	}
	AppliedDefinition = Definition;
	bHasAppliedDefinition = true;
	PreviewParameters = Definition.BoneParameters;
	UpdateGeometry(Stats);
	return true;
}

bool ANormalBone::ApplyModelDefinition(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason)
{
	FAssemblyBoneInstance Instance;
	FAssemblyBoneDefinition Definition;
	if (!IsValid(Assembly) || !Assembly->GetBone(InstanceId, Instance)
		|| !Assembly->GetBoneDefinition(Instance.TypeId, Definition))
	{
		OutReason = LOCTEXT("MissingRigidDefinition", "找不到刚体实例或其类型定义。");
		return false;
	}
	TGuardValue<bool> ApplyGuard(bApplyingModelDefinition, true);
	return ApplyRigidPartDefinition(Definition, OutReason);
}

FText ANormalBone::GetBoneDisplayName() const
{
	return NormalBoneMetrics::Describe(GetBoneParameters());
}

FIntPoint ANormalBone::GetRequiredGridSize(float GridCellSize) const
{
	return NormalBoneMetrics::RequiredGridSize(GetPhysicalStats(), GridCellSize);
}

bool ANormalBone::HasValidGeometry(const FNormalBonePhysicalStats& Stats) const
{
	const UPaperSprite* Sprite = BoneSprite->GetSprite();
	if (!Stats.bValid || !Sprite || !FMath::IsFinite(BodyHalfThickness) || BodyHalfThickness <= 0.0f
		|| !FMath::IsFinite(VisualDepthOffset))
	{
		return false;
	}
	const FBoxSphereBounds Bounds = Sprite->GetRenderBounds();
	return !Bounds.Origin.ContainsNaN() && !Bounds.BoxExtent.ContainsNaN()
		&& Bounds.BoxExtent.X > UE_SMALL_NUMBER && Bounds.BoxExtent.Z > UE_SMALL_NUMBER;
}

bool ANormalBone::CalculateBoneStats(const FNormalBoneParameters& Parameters, FNormalBonePhysicalStats& OutStats, FText& OutReason)
{
	return NormalBoneMetrics::Evaluate(Parameters, OutStats, OutReason);
}

FText ANormalBone::DescribeBoneParameters(const FNormalBoneParameters& Parameters)
{
	return NormalBoneMetrics::Describe(Parameters);
}

TArray<FIntPoint> ANormalBone::MakeRectangularBoneFootprint(const FNormalBoneParameters& Parameters, float GridCellSize, FText& OutReason)
{
	FNormalBonePhysicalStats Stats;
	if (!NormalBoneMetrics::Evaluate(Parameters, Stats, OutReason))
	{
		return {};
	}
	const FIntPoint Size = NormalBoneMetrics::RequiredGridSize(Stats, GridCellSize);
	if (Size.X <= 0 || Size.Y <= 0 || Size.X > 64 || Size.Y > 64)
	{
		OutReason = LOCTEXT("InvalidGridSize", "格长无效，或一般骨头无法放进支持的最大 64×64 网格。");
		return {};
	}
	TArray<FIntPoint> Cells;
	Cells.Reserve(Size.X * Size.Y);
	for (int32 Row = 0; Row < Size.Y; ++Row)
	{
		for (int32 Column = 0; Column < Size.X; ++Column)
		{
			Cells.Emplace(Column, Row);
		}
	}
	return Cells;
}

void ANormalBone::UpdateGeometry(const FNormalBonePhysicalStats& Stats)
{
	const FBoxSphereBounds Bounds = BoneSprite->GetSprite()->GetRenderBounds();
	const FVector HalfSize(Stats.Length * 0.5, BodyHalfThickness, Stats.Thickness * 0.5);
	BoneBody->SetBoxExtent(HalfSize, false);
	// Sprite fitting does not scale the bone-local coordinate system used by muscles and physics.
	const double LengthScale = HalfSize.X / Bounds.BoxExtent.X;
	const double ArtworkThicknessFactor = (static_cast<uint8>(GetBoneParameters().Thickness) + 1) * 0.5;
	FVector VisualScale(LengthScale, 1.0, bUseArtworkProportions
		? LengthScale * ArtworkThicknessFactor : HalfSize.Z / Bounds.BoxExtent.Z);
	const FAssemblyBoneDefinition Definition = GetRigidPartDefinition();
	if (Definition.PartKind == EAssemblyRigidPartKind::JointPart)
	{
		// Use the authored canvas, not trimmed render bounds: all shapes share one pixel scale.
		const FVector2D Canvas = AssemblyRigidPartMetrics::GetJointArtworkPixelSize(Definition.JointShape);
		const double UniformScale = Stats.Length / Canvas.X * BoneSprite->GetSprite()->GetPixelsPerUnrealUnit();
		VisualScale = FVector(UniformScale, 1.0, UniformScale);
	}
	BoneSprite->SetRelativeScale3D(VisualScale);
	double Depth = VisualDepthOffset + AssemblyRenderLayers::PreviewOffset;
	if (IsBoneInitialized())
	{
		const int32 Rank = AssemblyRenderLayers::GetRank(BoundAssembly->GetBones(), BoneData.InstanceId);
		Depth = VisualDepthOffset - AssemblyRenderLayers::DepthStep * (Rank + 1.0);
	}
	BoneSprite->SetRelativeLocation(FVector(-Bounds.Origin.X * VisualScale.X, Depth, -Bounds.Origin.Z * VisualScale.Z));
	BoneSprite->SetRelativeRotation(FRotator::ZeroRotator);
}

void ANormalBone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const FNormalBonePhysicalStats Stats = GetPhysicalStats();
	if (!BoneBody->IsSimulatingPhysics() && HasValidGeometry(Stats))
	{
		UpdateGeometry(Stats);
	}
}

bool ANormalBone::CanApplyPose(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!IsValid(Assembly) || !Assembly->IsInitialized() || Assembly->GetWorld() != GetWorld())
	{
		OutReason = LOCTEXT("MissingAssembly", "请提供同一游戏世界中已初始化的 AssemblyComponent。");
		return false;
	}
	if (Assembly->GetPhase() != EAssemblyPhase::Assembly || BoneBody->IsSimulatingPhysics())
	{
		OutReason = LOCTEXT("PhysicalPose", "操控或物理模拟期间由物理系统决定骨头姿态，不能应用组装摆放。");
		return false;
	}
	if (USceneComponent* Source = Assembly->GetBonePoseSource(InstanceId); Source && Source != BoneBody)
	{
		OutReason = LOCTEXT("DifferentPoseSource", "此骨头实例已有其他姿态源，请避免重复生成或覆盖物理组件。");
		return false;
	}
	return true;
}

bool ANormalBone::ReadAssemblyData(UAssemblyComponent* Assembly, FGuid InstanceId, FAssemblyBoneInstance& OutBone,
	TArray<FIntPoint>& OutFootprint, FNormalBoneParameters& OutParameters, float& OutCellSize,
	FTransform& OutPose, FText& OutReason) const
{
	if (!Assembly->GetBone(InstanceId, OutBone))
	{
		OutReason = LOCTEXT("MissingBone", "找不到已安装的骨头实例，请先成功调用安装骨头接口。");
		return false;
	}
	const TArray<FAssemblyBoneDefinition> Definitions = Assembly->GetBoneDefinitions();
	const FAssemblyBoneDefinition* Definition = Definitions.FindByPredicate(
		[&OutBone](const FAssemblyBoneDefinition& Item) { return Item.TypeId == OutBone.TypeId; });
	if (!Definition)
	{
		OutReason = LOCTEXT("MissingDefinition", "骨头类型配置不存在。");
		return false;
	}
	OutFootprint = Definition->Footprint;
	OutParameters = Definition->BoneParameters;
	OutCellSize = Assembly->GetGridSettings().CellSize;
	OutPose = OutBone.LocalVisualTransform * Assembly->GetGridFrameTransform();
	FNormalBonePhysicalStats Stats;
	if (!AssemblyRigidPartMetrics::Evaluate(*Definition, Stats, OutReason))
	{
		return false;
	}
	const bool bFitsPlacement = Assembly->GetPlacementMode() == EAssemblyPlacementMode::Free
		|| FitsFootprintBounds(OutFootprint, OutCellSize, Stats);
	if (!IsUsableTransform(OutPose) || !HasValidGeometry(Stats) || !bFitsPlacement)
	{
		OutReason = LOCTEXT("InvalidGeometry", "骨头 Sprite、组装变换或尺寸无效；网格模式的实际尺寸必须放得进配置占格。");
		return false;
	}
	return true;
}

bool ANormalBone::InitializeBone(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason)
{
	if (IsBoneInitialized())
	{
		if (BoundAssembly.Get() == Assembly && BoneData.InstanceId == InstanceId)
		{
			return RefreshAssemblyPose(OutReason);
		}
		OutReason = LOCTEXT("AlreadyBound", "此 Actor 已绑定另一骨头实例；重新使用前请调用 ClearAssemblyBinding。");
		return false;
	}
	if (!CanApplyPose(Assembly, InstanceId, OutReason) || !ApplyModelDefinition(Assembly, InstanceId, OutReason))
	{
		return false;
	}
	FAssemblyBoneInstance NewBone;
	TArray<FIntPoint> NewFootprint;
	FNormalBoneParameters NewParameters;
	float NewCellSize;
	FTransform NewPose;
	if (!ReadAssemblyData(Assembly, InstanceId, NewBone, NewFootprint, NewParameters, NewCellSize, NewPose, OutReason)
		|| !Assembly->RegisterBonePoseSource(InstanceId, BoneBody, OutReason))
	{
		return false;
	}
	BoundAssembly = Assembly;
	BoneData = NewBone;
	Footprint = MoveTemp(NewFootprint);
	InstalledParameters = NewParameters;
	CellSize = NewCellSize;
	UpdateGeometry(GetPhysicalStats());
	SetActorTransform(NewPose, false, nullptr, ETeleportType::TeleportPhysics);
	Assembly->OnAssemblyChanged.AddUniqueDynamic(this, &ANormalBone::HandleAssemblyChanged);
	Assembly->OnAssemblyReset.AddUniqueDynamic(this, &ANormalBone::HandleAssemblyReset);
	return true;
}

bool ANormalBone::RefreshAssemblyPose(FText& OutReason)
{
	UAssemblyComponent* Assembly = BoundAssembly.Get();
	if (!CanApplyPose(Assembly, BoneData.InstanceId, OutReason) || !ApplyModelDefinition(Assembly, BoneData.InstanceId, OutReason))
	{
		return false;
	}
	FAssemblyBoneInstance NewBone;
	TArray<FIntPoint> NewFootprint;
	FNormalBoneParameters NewParameters;
	float NewCellSize;
	FTransform NewPose;
	if (!ReadAssemblyData(Assembly, BoneData.InstanceId, NewBone, NewFootprint, NewParameters, NewCellSize, NewPose, OutReason))
	{
		return false;
	}
	BoneData = NewBone;
	Footprint = MoveTemp(NewFootprint);
	InstalledParameters = NewParameters;
	CellSize = NewCellSize;
	UpdateGeometry(GetPhysicalStats());
	SetActorTransform(NewPose, false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

FVector2D ANormalBone::GetEndLocalPosition(EBoneEnd End) const
{
	return FVector2D((End == EBoneEnd::Start ? -0.5 : 0.5) * GetPhysicalStats().Length, 0.0);
}

bool ANormalBone::GetEndWorldPosition(EBoneEnd End, FVector& OutWorldPosition) const
{
	OutWorldPosition = FVector::ZeroVector;
	if (static_cast<uint8>(End) > static_cast<uint8>(EBoneEnd::End))
	{
		return false;
	}
	// An uninstalled preview still has meaningful physical end positions.
	if (!IsBoneInitialized())
	{
		const FNormalBonePhysicalStats Stats = GetPhysicalStats();
		const FTransform Pose = BoneBody->GetComponentTransform();
		if (!Stats.bValid || !IsUsableTransform(Pose))
		{
			return false;
		}
		const FVector2D Local = GetEndLocalPosition(End);
		OutWorldPosition = Pose.TransformPosition(FVector(Local.X, 0.0, Local.Y));
		return !OutWorldPosition.ContainsNaN();
	}
	return GetConnectionWorldPosition(GetEndLocalPosition(End), OutWorldPosition);
}

bool ANormalBone::TryMakeMuscleEndpoint(FVector2D LocalPoint, FAssemblyMuscleEndpoint& OutEndpoint) const
{
	OutEndpoint = FAssemblyMuscleEndpoint();
	FAssemblyBoneInstance Current;
	const FNormalBonePhysicalStats Stats = GetPhysicalStats();
	if (!IsBoneInitialized() || GetRigidPartDefinition().PartKind != EAssemblyRigidPartKind::Bone
		|| !BoundAssembly->GetBone(BoneData.InstanceId, Current) || !Stats.bValid
		|| !FMath::IsFinite(LocalPoint.X) || !FMath::IsFinite(LocalPoint.Y)
		|| FMath::Abs(LocalPoint.X) > Stats.Length * 0.5 + UE_KINDA_SMALL_NUMBER
		|| FMath::Abs(LocalPoint.Y) > Stats.Thickness * 0.5 + UE_KINDA_SMALL_NUMBER
		|| (BoundAssembly->GetPlacementMode() == EAssemblyPlacementMode::Grid
			&& !AssemblyRules::IsPointOnBone(Footprint, CellSize, LocalPoint)))
	{
		return false;
	}
	OutEndpoint.BoneId = BoneData.InstanceId;
	OutEndpoint.BoneLocalPoint = LocalPoint;
	return true;
}

bool ANormalBone::TryGetMuscleEndpoint(FVector WorldPoint, FAssemblyMuscleEndpoint& OutEndpoint) const
{
	OutEndpoint = FAssemblyMuscleEndpoint();
	FTransform Pose;
	if (!IsBoneInitialized() || WorldPoint.ContainsNaN()
		|| !BoundAssembly->GetBoneWorldTransform(BoneData.InstanceId, Pose))
	{
		return false;
	}
	const FVector Local = Pose.InverseTransformPosition(WorldPoint);
	return TryMakeMuscleEndpoint(FVector2D(Local.X, Local.Z), OutEndpoint);
}

bool ANormalBone::GetConnectionWorldPosition(FVector2D LocalPoint, FVector& OutWorldPosition) const
{
	OutWorldPosition = FVector::ZeroVector;
	FAssemblyMuscleEndpoint Endpoint;
	return TryMakeMuscleEndpoint(LocalPoint, Endpoint)
		&& BoundAssembly->GetEndpointWorldPosition(Endpoint, OutWorldPosition);
}

void ANormalBone::ClearAssemblyBinding()
{
	if (UAssemblyComponent* Assembly = BoundAssembly.Get())
	{
		Assembly->OnAssemblyChanged.RemoveDynamic(this, &ANormalBone::HandleAssemblyChanged);
		Assembly->OnAssemblyReset.RemoveDynamic(this, &ANormalBone::HandleAssemblyReset);
		Assembly->UnregisterBonePoseSourceIfMatches(BoneData.InstanceId, BoneBody);
	}
	BoundAssembly.Reset();
	BoneData = FAssemblyBoneInstance();
	Footprint.Reset();
}

void ANormalBone::HandleAssemblyChanged()
{
	FAssemblyBoneInstance Current;
	if (!BoundAssembly.IsValid() || !BoundAssembly->GetBone(BoneData.InstanceId, Current))
	{
		ClearAssemblyBinding();
		Destroy();
		return;
	}
	// Free assembly follows the registered body, including when its coordinate frame moves.
	// A key/constraint edit must not apply an unchanged cached pose in a new frame and teleport it.
	// Explicit RefreshAssemblyPose remains available when a caller intentionally reapplies the model.
	if (BoundAssembly->GetPlacementMode() == EAssemblyPlacementMode::Free
		&& Current.LocalVisualTransform.Equals(BoneData.LocalVisualTransform, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}
	FText Reason;
	RefreshAssemblyPose(Reason);
}

void ANormalBone::HandleAssemblyReset()
{
	ClearAssemblyBinding();
	Destroy();
}

void ANormalBone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAssemblyBinding();
	Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
