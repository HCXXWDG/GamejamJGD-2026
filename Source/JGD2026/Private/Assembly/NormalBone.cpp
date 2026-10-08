#include "Assembly/NormalBone.h"
#include "Assembly/AssemblyComponent.h"
#include "AssemblyRules.h"
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
	NormalBoneMetrics::Evaluate(GetBoneParameters(), Stats, Reason);
	return Stats;
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
	if (!Stats.bValid || !Sprite || !FMath::IsFinite(BodyHalfThickness) || BodyHalfThickness <= 0.0f)
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
	const FVector VisualScale(HalfSize.X / Bounds.BoxExtent.X, 1.0, HalfSize.Z / Bounds.BoxExtent.Z);
	BoneSprite->SetRelativeScale3D(VisualScale);
	BoneSprite->SetRelativeLocation(FVector(-Bounds.Origin.X * VisualScale.X, 0.0, -Bounds.Origin.Z * VisualScale.Z));
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
	if (!IsValid(Assembly) || !Assembly->IsInitialized())
	{
		OutReason = LOCTEXT("MissingAssembly", "请提供已初始化的 AssemblyComponent。");
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
		OutReason = LOCTEXT("MissingBone", "找不到已安装的骨头实例，请先成功调用 TryInstallBone。");
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
	if (!NormalBoneMetrics::Evaluate(OutParameters, Stats, OutReason))
	{
		return false;
	}
	if (!IsUsableTransform(OutPose) || !HasValidGeometry(Stats) || !FitsFootprintBounds(OutFootprint, OutCellSize, Stats))
	{
		OutReason = LOCTEXT("InvalidGeometry", "骨头 Sprite、网格变换或尺寸无效；实际长度和粗细必须放得进配置占格。");
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
	if (!CanApplyPose(Assembly, InstanceId, OutReason))
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
	if (!CanApplyPose(Assembly, BoneData.InstanceId, OutReason))
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
	return GetConnectionWorldPosition(GetEndLocalPosition(End), OutWorldPosition);
}

bool ANormalBone::TryMakeMuscleEndpoint(FVector2D LocalPoint, FAssemblyMuscleEndpoint& OutEndpoint) const
{
	OutEndpoint = FAssemblyMuscleEndpoint();
	FAssemblyBoneInstance Current;
	const FNormalBonePhysicalStats Stats = GetPhysicalStats();
	if (!IsBoneInitialized() || !BoundAssembly->GetBone(BoneData.InstanceId, Current) || !Stats.bValid
		|| !FMath::IsFinite(LocalPoint.X) || !FMath::IsFinite(LocalPoint.Y)
		|| FMath::Abs(LocalPoint.X) > Stats.Length * 0.5 + UE_KINDA_SMALL_NUMBER
		|| FMath::Abs(LocalPoint.Y) > Stats.Thickness * 0.5 + UE_KINDA_SMALL_NUMBER
		|| !AssemblyRules::IsPointOnBone(Footprint, CellSize, LocalPoint))
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
