#include "Assembly/AssemblyComponent.h"
#include "AssemblyRules.h"
#include "AssemblyFreeRules.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "Assembly/AssemblySurfaceGeometry.h"
#include "AssemblyTutorial.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "AssemblyComponent"

UAssemblyComponent::UAssemblyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	FAssemblyBoneDefinition Bone;
	Bone.DisplayName = LOCTEXT("DefaultBone", "骨头");
	Bone.InitialQuantity = 6;
	DefaultBones.Add(Bone);
	for (uint8 Shape = 0; Shape <= static_cast<uint8>(EAssemblyJointPartShape::FixedRound); ++Shape)
	{
		DefaultJointParts.Add(AssemblyRigidPartMetrics::MakeJointPartDefinition(static_cast<EAssemblyJointPartShape>(Shape)));
	}
	FAssemblyMuscleDefinition Muscle;
	Muscle.DisplayName = LOCTEXT("DefaultMuscle", "肌肉");
	DefaultMuscles.Add(Muscle);
}

void UAssemblyComponent::BeginPlay()
{
	Super::BeginPlay();
	bEndingPlay = false;
	if (bAutoEnterAssembly && !bInitialized)
	{
		FText Reason;
		if (!EnterAssembly(Reason))
		{
			UE_LOG(LogTemp, Error, TEXT("Assembly initialization failed on %s: %s"), *GetPathName(), *Reason.ToString());
		}
	}
}

void UAssemblyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearInputState();
	{
		TGuardValue<bool> Guard(bInLifecycleCallback, true);
		const FSimpleDelegate Shutdown = BeforeEndPlayCallback;
		Shutdown.ExecuteIfBound();
	}
	BeforePlayingCallback.Unbind();
	BeforeResetCallback.Unbind();
	CommitPlayingCallback.Unbind();
	VerifyPlayingCallback.Unbind();
	FinalizePlayingCallback.Unbind();
	AbortPlayingCallback.Unbind();
	BeforeEndPlayCallback.Unbind();
	LifecycleOwner.Reset();
	bEndingPlay = true;
	InstalledBones.Reset();
	InstalledMuscles.Reset();
	InstalledJoints.Reset();
	BonePoseSources.Reset();
	RemainingQuantities.Reset();
	SelectedKey = EAssemblyMuscleKey::None;
	Phase = EAssemblyPhase::Assembly;
	{
		TGuardValue<bool> Guard(bNotifying, true);
		OnAssemblyReset.Broadcast();
	}
	bInitialized = false;
	Super::EndPlay(EndPlayReason);
}

bool UAssemblyComponent::CanMutate(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (bEndingPlay)
	{
		OutReason = LOCTEXT("EndedAssembly", "组装组件已结束生命周期。");
		return false;
	}
	if (bNotifying || bInLifecycleCallback)
	{
		OutReason = LOCTEXT("ReentrantEdit", "不能在组装通知或物理生命周期回调中再次修改规则数据；请在回调结束后发起操作。");
		return false;
	}
	return true;
}

bool UAssemblyComponent::RegisterLifecycleCallbacks(UObject* Owner, FAssemblyLifecycleCheck BeforePlaying,
	FAssemblyLifecycleCheck BeforeReset, FSimpleDelegate AbortPlaying, FSimpleDelegate BeforeEndPlay, FText& OutReason)
{
	if (!CanMutate(OutReason)) { return false; }
	if (!IsValid(Owner) || !BeforePlaying.IsBound() || !BeforeReset.IsBound())
	{
		OutReason = LOCTEXT("InvalidLifecycle", "生命周期保护需要有效拥有者和开始、重置检查。");
		return false;
	}
	if (LifecycleOwner.IsValid() && LifecycleOwner.Get() != Owner)
	{
		OutReason = LOCTEXT("DuplicateLifecycle", "组装模型已有生命周期保护，请使用同一个物理桥。");
		return false;
	}
	LifecycleOwner = Owner;
	BeforePlayingCallback = MoveTemp(BeforePlaying);
	BeforeResetCallback = MoveTemp(BeforeReset);
	AbortPlayingCallback = MoveTemp(AbortPlaying);
	BeforeEndPlayCallback = MoveTemp(BeforeEndPlay);
	OutReason = FText::GetEmpty();
	return true;
}

bool UAssemblyComponent::RegisterPlayingCommitCallbacks(UObject* Owner, FAssemblyLifecycleCheck Commit,
	FAssemblyLifecycleCheck Verify, FAssemblyLifecycleCheck Finalize, FText& OutReason)
{
	if (!CanMutate(OutReason)) { return false; }
	if (!IsValid(Owner) || LifecycleOwner.Get() != Owner || !Commit.IsBound() || !Verify.IsBound() || !Finalize.IsBound())
	{
		OutReason = LOCTEXT("InvalidPlayingCommit", "阶段提交检查必须属于当前生命周期拥有者，并提供提交、复核和最终确认。 ");
		return false;
	}
	CommitPlayingCallback = MoveTemp(Commit);
	VerifyPlayingCallback = MoveTemp(Verify);
	FinalizePlayingCallback = MoveTemp(Finalize);
	OutReason = FText::GetEmpty();
	return true;
}

void UAssemblyComponent::UnregisterLifecycleCallbacksIfMatches(const UObject* Owner)
{
	if (LifecycleOwner.Get() != Owner) { return; }
	LifecycleOwner.Reset();
	BeforePlayingCallback.Unbind();
	BeforeResetCallback.Unbind();
	CommitPlayingCallback.Unbind();
	VerifyPlayingCallback.Unbind();
	FinalizePlayingCallback.Unbind();
	AbortPlayingCallback.Unbind();
	BeforeEndPlayCallback.Unbind();
}

bool UAssemblyComponent::RunLifecycleCheck(bool bBeforePlaying, FText& OutReason)
{
	if (!bBeforePlaying) { ClearInputState(); }
	if (!IsValid(this) || bEndingPlay)
	{
		OutReason = LOCTEXT("LifecycleEnded", "组装生命周期已在回调中结束。");
		return false;
	}
	const FAssemblyLifecycleCheck Check = bBeforePlaying ? BeforePlayingCallback : BeforeResetCallback;
	if (!Check.IsBound()) { return true; }
	const TWeakObjectPtr<UObject> Owner = LifecycleOwner;
	TGuardValue<bool> Guard(bInLifecycleCallback, true);
	const bool bAccepted = Check.Execute(OutReason);
	if (!IsValid(this) || bEndingPlay || !Owner.IsValid() || LifecycleOwner != Owner)
	{
		OutReason = LOCTEXT("LifecycleChanged", "物理生命周期绑定在检查期间已结束或替换，请重新初始化。");
		return false;
	}
	if (!bAccepted && OutReason.IsEmpty())
	{
		OutReason = LOCTEXT("LifecycleRejected", "物理组件拒绝本次阶段切换。");
	}
	return bAccepted;
}

bool UAssemblyComponent::CanEdit(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!bInitialized)
	{
		OutReason = LOCTEXT("NotInitialized", "请先初始化组装组件。");
		return false;
	}
	if (Phase != EAssemblyPhase::Assembly)
	{
		OutReason = LOCTEXT("NotAssembling", "只能在组装阶段修改组件。");
		return false;
	}
	return true;
}

bool UAssemblyComponent::ConfigureAssembly(const FAssemblyGridSettings& Grid,
	const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason)
{
	if (!CanMutate(OutReason)) { return false; }
	if (Bones.ContainsByPredicate([](const FAssemblyBoneDefinition& D) { return D.PartKind != EAssemblyRigidPartKind::Bone; }))
	{
		OutReason = LOCTEXT("JointPartsRequireFree", "独立关节零件仅支持自由组装，不能加入旧网格模式。");
		return false;
	}
	if (!AssemblyRules::ValidateConfiguration(Grid, Bones, Muscles, OutReason))
	{
		return false;
	}
	if (!RunLifecycleCheck(false, OutReason)) { return false; }
	ClearInputState();
	RoundGrid = Grid;
	RoundPlacementMode = EAssemblyPlacementMode::Grid;
	RoundBoneDefinitions = Bones;
	RoundMuscleDefinitions = Muscles;
	bInitialized = true;
	ResetRound();
	return true;
}

bool UAssemblyComponent::ConfigureFreeAssembly(const FAssemblyFreeSettings& Settings,
	const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason)
{
	if (!CanMutate(OutReason) || !AssemblyFreeRules::ValidateConfiguration(Settings, Bones, Muscles, OutReason)) { return false; }
	if (!RunLifecycleCheck(false, OutReason)) { return false; }
	ClearInputState();
	RoundGrid = DefaultGrid;
	RoundFreeSettings = Settings;
	RoundPlacementMode = EAssemblyPlacementMode::Free;
	RoundBoneDefinitions = Bones;
	RoundMuscleDefinitions = Muscles;
	bInitialized = true;
	ResetRound();
	return true;
}

bool UAssemblyComponent::EnterAssembly(FText& OutReason)
{
	if (!CanMutate(OutReason))
	{
		return false;
	}
	if (!bInitialized)
	{
		if (DefaultPlacementMode == EAssemblyPlacementMode::Free)
		{
			TArray<FAssemblyBoneDefinition> Definitions = DefaultBones;
			Definitions.Append(DefaultJointParts);
			TArray<FAssemblyMuscleDefinition> Muscles = DefaultMuscles;
			AssemblyTutorial::ConfigureInventory(AssemblyTutorial::Stage(this), Definitions, Muscles);
			return ConfigureFreeAssembly(DefaultFreeSettings, Definitions, Muscles, OutReason);
		}
		return ConfigureAssembly(DefaultGrid, DefaultBones, DefaultMuscles, OutReason);
	}
	if (!RunLifecycleCheck(false, OutReason)) { return false; }
	ResetRound();
	return true;
}

bool UAssemblyComponent::RestartAssembly(FText& OutReason)
{
	return EnterAssembly(OutReason);
}

void UAssemblyComponent::CaptureBuildCheckpoint()
{
	if (!bInitialized || bEndingPlay) return;
	CheckpointBones = InstalledBones;
	CheckpointMuscles = InstalledMuscles;
	CheckpointJoints = InstalledJoints;
	CheckpointQuantities = RemainingQuantities;
	CheckpointKey = SelectedKey;
	for (FAssemblyMuscleInstance& Muscle : CheckpointMuscles) Muscle.bContracting = false;
	bHasBuildCheckpoint = true;
}

void UAssemblyComponent::ApplyInitialGameplayScale(float Scale)
{
	if (!bInitialized || Phase != EAssemblyPhase::Assembly || !InstalledBones.IsEmpty()
		|| !FMath::IsNearlyEqual(GameplaySizeScale, 1.0f)) return;
	GameplaySizeScale = FMath::IsFinite(Scale) ? FMath::Clamp(Scale, 0.3f, 1.5f) : 0.8f;
	RoundFreeSettings.CoreRadius *= GameplaySizeScale;
	RoundFreeSettings.CoreLocalPosition *= GameplaySizeScale;
	RoundFreeSettings.ConnectionDistance *= GameplaySizeScale;
	RoundFreeSettings.SnapReleaseDistance *= GameplaySizeScale;
	RoundGrid.CellSize *= GameplaySizeScale;
	RoundGrid.TopLeftLocal *= GameplaySizeScale;
	for (FAssemblyBoneDefinition& Definition : RoundBoneDefinitions)
	{
		Definition.BoneParameters.UnitLength *= GameplaySizeScale;
		Definition.JointPartSize *= GameplaySizeScale;
		for (FVector2D& Point : Definition.ConnectionPoints) Point *= GameplaySizeScale;
		for (FVector2D& Point : Definition.SurfaceOutline) Point *= GameplaySizeScale;
	}
}

bool UAssemblyComponent::ReturnToAssemblyKeepingBuild(FText& OutReason)
{
	if (!CanMutate(OutReason)) return false;
	if (!bInitialized) return EnterAssembly(OutReason);
	// Assembly edits are the newest design; a running round restores its start design.
	if (Phase == EAssemblyPhase::Assembly || !bHasBuildCheckpoint) CaptureBuildCheckpoint();
	if (!RunLifecycleCheck(false, OutReason)) return false;
	ClearInputState();
	InstalledBones = CheckpointBones;
	InstalledMuscles = CheckpointMuscles;
	InstalledJoints = CheckpointJoints;
	RemainingQuantities = CheckpointQuantities;
	SelectedKey = CheckpointKey;
	BonePoseSources.Reset();
	Phase = EAssemblyPhase::Assembly;
	{
		TGuardValue<bool> Guard(bNotifying, true);
		OnAssemblyReset.Broadcast();
	}
	NotifyPhase();
	NotifyChanged();
	OutReason = FText::GetEmpty();
	return true;
}

bool UAssemblyComponent::RecoverAssemblyAfterPhysicsFailure(FText& OutReason)
{
	if (!CanMutate(OutReason) || !bInitialized) { return false; }
	if (Phase == EAssemblyPhase::Assembly) { return true; }
	TGuardValue<bool> RecoveryGuard(bRecoveringPhysicsFailure, true);
	ClearInputState();
	if (!IsValid(this) || !bInitialized || bEndingPlay || (GetOwner() && GetOwner()->IsActorBeingDestroyed()))
	{
		OutReason = LOCTEXT("PhysicsRecoveryEnded", "物理恢复期间角色或组装生命周期已结束。");
		return false;
	}
	Phase = EAssemblyPhase::Assembly;
	NotifyPhase();
	if (!IsValid(this) || !bInitialized || bEndingPlay) { return false; }
	NotifyChanged();
	OutReason = FText::GetEmpty();
	return true;
}

void UAssemblyComponent::ResetRound()
{
	bHasBuildCheckpoint = false;
	CheckpointBones.Reset();
	CheckpointMuscles.Reset();
	CheckpointJoints.Reset();
	CheckpointQuantities.Reset();
	ClearInputState();
	InstalledBones.Reset();
	InstalledMuscles.Reset();
	InstalledJoints.Reset();
	BonePoseSources.Reset();
	RemainingQuantities.Reset();
	for (const FAssemblyBoneDefinition& Bone : RoundBoneDefinitions)
	{
		RemainingQuantities.Add(Bone.TypeId, Bone.InitialQuantity);
	}
	for (const FAssemblyMuscleDefinition& Muscle : RoundMuscleDefinitions)
	{
		RemainingQuantities.Add(Muscle.TypeId, Muscle.InitialQuantity);
	}
	SelectedKey = EAssemblyMuscleKey::None;
	Phase = EAssemblyPhase::Assembly;
	{
		TGuardValue<bool> Guard(bNotifying, true);
		OnAssemblyReset.Broadcast();
	}
	NotifyPhase();
	NotifyChanged();
}

bool UAssemblyComponent::TryStartPlaying(FText& OutReason)
{
	if (!CanMutate(OutReason))
	{
		return false;
	}
	TArray<FAssemblyBoneInstance> EffectiveBones = InstalledBones;
	TArray<FAssemblyJointInstance> EffectiveJoints = InstalledJoints;
	if (RoundPlacementMode == EAssemblyPlacementMode::Free && Phase == EAssemblyPhase::Assembly)
	{
		if (!BuildEffectiveFreeBoneSnapshot(EffectiveBones, OutReason)) { return false; }
		EffectiveJoints = AssemblyFreeRules::BuildJoints(RoundFreeSettings, RoundBoneDefinitions, EffectiveBones, InstalledJoints);
	}
	const FAssemblyValidationResult Validation = Phase == EAssemblyPhase::Playing
		? ValidateAssembly() : ValidateAssemblyState(EffectiveBones, EffectiveJoints);
	OutReason = Validation.Reason;
	if (!Validation.bValid)
	{
		return false;
	}
	if (Phase == EAssemblyPhase::Playing)
	{
		if (VerifyPlayingCallback.IsBound())
		{
			const FAssemblyLifecycleCheck Verify = VerifyPlayingCallback;
			const TWeakObjectPtr<UObject> Owner = LifecycleOwner;
			bool bAccepted;
			{
				TGuardValue<bool> Guard(bInLifecycleCallback, true);
				bAccepted = Verify.Execute(OutReason);
			}
			if (!bAccepted || !IsValid(this) || !bInitialized || bEndingPlay || !Owner.IsValid() || LifecycleOwner != Owner)
			{
				const FText Failure = OutReason.IsEmpty() ? LOCTEXT("ActivePlayingInvalid", "当前操控物理绑定或角色代理保护已失效。") : OutReason;
				const FSimpleDelegate Shutdown = BeforeEndPlayCallback;
				{
					TGuardValue<bool> Guard(bInLifecycleCallback, true);
					Shutdown.ExecuteIfBound();
				}
				FText RecoveryReason;
				RecoverAssemblyAfterPhysicsFailure(RecoveryReason);
				OutReason = Failure;
				return false;
			}
		}
		return true;
	}
	const TArray<FAssemblyBoneInstance> BeforeBones = InstalledBones;
	const TArray<FAssemblyJointInstance> BeforeJoints = InstalledJoints;
	const TMap<FGuid, TWeakObjectPtr<USceneComponent>> BeforePoseSources = BonePoseSources;
	const TWeakObjectPtr<USceneComponent> BeforeFrame = GridFrame;
	const TWeakObjectPtr<UObject> TransactionOwner = LifecycleOwner;
	const bool bHadLifecycleOwner = TransactionOwner.IsValid();
	const FAssemblyLifecycleCheck Commit = CommitPlayingCallback;
	const FAssemblyLifecycleCheck Verify = VerifyPlayingCallback;
	const FAssemblyLifecycleCheck Finalize = FinalizePlayingCallback;
	const FSimpleDelegate Abort = AbortPlayingCallback;
	bool bPhaseWasNotified = false;
	// Preparation consumes a consistent model snapshot. Failed preparation restores the editable data.
	InstalledBones = MoveTemp(EffectiveBones);
	InstalledJoints = MoveTemp(EffectiveJoints);
	auto RestoreEditableSnapshot = [&]()
	{
		if (!IsValid(this) || !bInitialized || bEndingPlay || (GetOwner() && GetOwner()->IsActorBeingDestroyed())) { return; }
		Phase = EAssemblyPhase::Assembly;
		InstalledBones = BeforeBones;
		InstalledJoints = BeforeJoints;
		BonePoseSources = BeforePoseSources;
		GridFrame = BeforeFrame;
	};
	auto RollBack = [&]()
	{
		// Phase is restored before Abort so providers observe stable editing while undoing activation.
		if (IsValid(this) && !bEndingPlay) { Phase = EAssemblyPhase::Assembly; }
		{
			TGuardValue<bool> Guard(bInLifecycleCallback, true);
			Abort.ExecuteIfBound();
		}
		RestoreEditableSnapshot();
		if (bPhaseWasNotified && IsValid(this) && bInitialized && !bEndingPlay
			&& (!GetOwner() || !GetOwner()->IsActorBeingDestroyed())) { NotifyPhase(); }
	};
	auto Confirm = [&](const FAssemblyLifecycleCheck& Check)
	{
		// Keep old native lifecycle clients compatible when they have no commit extension.
		TGuardValue<bool> Guard(bInLifecycleCallback, true);
		const bool bAccepted = !Check.IsBound() || Check.Execute(OutReason);
		if (!IsValid(this) || !bInitialized || bEndingPlay || Phase != EAssemblyPhase::Playing
			|| (bHadLifecycleOwner && (!TransactionOwner.IsValid() || LifecycleOwner != TransactionOwner))
			|| (GetOwner() && GetOwner()->IsActorBeingDestroyed()))
		{
			OutReason = LOCTEXT("PlayingCommitCanceled", "操控阶段提交期间组装、角色或物理绑定已结束或替换。");
			return false;
		}
		if (!bAccepted && OutReason.IsEmpty()) { OutReason = LOCTEXT("PlayingCommitRejected", "物理执行器拒绝最终操控阶段确认。"); }
		return bAccepted;
	};
	if (!RunLifecycleCheck(true, OutReason)) { RollBack(); return false; }
	// Preparation may register physical pose sources. Check those poses before committing the phase.
	const FAssemblyValidationResult PreparedValidation = ValidateAssembly();
	if (!PreparedValidation.bValid)
	{
		OutReason = PreparedValidation.Reason;
		RollBack();
		return false;
	}
	// This is provisional until activation and all phase listeners have been confirmed.
	Phase = EAssemblyPhase::Playing;
	if (!Confirm(Commit)) { RollBack(); return false; }
	bPhaseWasNotified = true;
	NotifyPhase();
	if (!Confirm(Verify) || !Confirm(Finalize)) { RollBack(); return false; }
	OutReason = FText::GetEmpty();
	return true;
}

FAssemblyGridSettings UAssemblyComponent::GetGridSettings() const
{
	return bInitialized ? RoundGrid : DefaultGrid;
}

TArray<FAssemblyBoneDefinition> UAssemblyComponent::GetBoneDefinitions() const
{
	if (bInitialized) { return RoundBoneDefinitions; }
	TArray<FAssemblyBoneDefinition> Definitions = DefaultBones;
	if (DefaultPlacementMode == EAssemblyPlacementMode::Free) { Definitions.Append(DefaultJointParts); }
	return Definitions;
}

bool UAssemblyComponent::GetBoneDefinition(FName TypeId, FAssemblyBoneDefinition& OutDefinition) const
{
	const TArray<FAssemblyBoneDefinition> Definitions = GetBoneDefinitions();
	const FAssemblyBoneDefinition* Found = Definitions.FindByPredicate([TypeId](const FAssemblyBoneDefinition& D) { return D.TypeId == TypeId; });
	OutDefinition = Found ? *Found : FAssemblyBoneDefinition();
	return Found != nullptr;
}

TArray<FAssemblyBoneDefinition> UAssemblyComponent::GetJointPartDefinitions() const
{
	TArray<FAssemblyBoneDefinition> Definitions = GetBoneDefinitions();
	Definitions.RemoveAll([](const FAssemblyBoneDefinition& D) { return D.PartKind != EAssemblyRigidPartKind::JointPart; });
	return Definitions;
}

TArray<FAssemblyBoneInstance> UAssemblyComponent::GetJointParts() const
{
	TArray<FAssemblyBoneInstance> Parts = InstalledBones;
	Parts.RemoveAll([this](const FAssemblyBoneInstance& B) { return !IsJointPart(B.InstanceId); });
	return Parts;
}

bool UAssemblyComponent::IsJointPart(FGuid Id) const
{
	const FAssemblyBoneInstance* B = FindBone(Id);
	const FAssemblyBoneDefinition* D = B ? FindBoneDefinition(B->TypeId) : nullptr;
	return D && D->PartKind == EAssemblyRigidPartKind::JointPart;
}

TArray<FAssemblyMuscleDefinition> UAssemblyComponent::GetMuscleDefinitions() const
{
	return bInitialized ? RoundMuscleDefinitions : DefaultMuscles;
}

int32 UAssemblyComponent::GetRemainingQuantity(FName TypeId) const
{
	const int32* Quantity = RemainingQuantities.Find(TypeId);
	return Quantity ? *Quantity : 0;
}

const FAssemblyBoneDefinition* UAssemblyComponent::FindBoneDefinition(FName TypeId) const
{
	return RoundBoneDefinitions.FindByPredicate([TypeId](const FAssemblyBoneDefinition& Definition) { return Definition.TypeId == TypeId; });
}

const FAssemblyMuscleDefinition* UAssemblyComponent::FindMuscleDefinition(FName TypeId) const
{
	return RoundMuscleDefinitions.FindByPredicate([TypeId](const FAssemblyMuscleDefinition& Definition) { return Definition.TypeId == TypeId; });
}

const FAssemblyBoneInstance* UAssemblyComponent::FindBone(FGuid Id) const
{
	return InstalledBones.FindByPredicate([Id](const FAssemblyBoneInstance& Bone) { return Bone.InstanceId == Id; });
}

const FAssemblyMuscleInstance* UAssemblyComponent::FindMuscle(FGuid Id) const
{
	return InstalledMuscles.FindByPredicate([Id](const FAssemblyMuscleInstance& Muscle) { return Muscle.InstanceId == Id; });
}

bool UAssemblyComponent::GetBone(FGuid InstanceId, FAssemblyBoneInstance& OutBone) const
{
	const FAssemblyBoneInstance* Bone = FindBone(InstanceId);
	OutBone = Bone ? *Bone : FAssemblyBoneInstance();
	return Bone != nullptr;
}

bool UAssemblyComponent::GetMuscle(FGuid InstanceId, FAssemblyMuscleInstance& OutMuscle) const
{
	const FAssemblyMuscleInstance* Muscle = FindMuscle(InstanceId);
	OutMuscle = Muscle ? *Muscle : FAssemblyMuscleInstance();
	return Muscle != nullptr;
}

bool UAssemblyComponent::SelectMuscleKey(EAssemblyMuscleKey Key, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	if (!AssemblyRules::IsValidKey(Key, true))
	{
		OutReason = LOCTEXT("InvalidKey", "请选择 W、A、S 或 D。");
		return false;
	}
	if (SelectedKey != Key)
	{
		SelectedKey = Key;
		NotifyChanged();
	}
	return true;
}

FAssemblyPlacementResult UAssemblyComponent::ValidateBonePlacement(FName TypeId, FIntPoint AnchorCell,
	int32 QuarterTurns, FGuid IgnoreId) const
{
	FAssemblyPlacementResult Result;
	if (GetPlacementMode() != EAssemblyPlacementMode::Grid)
	{
		Result.Reason = LOCTEXT("UseFreePlacement", "自由组装请使用 Validate/Try Install Free Bone，位置和角度不经过格坐标。");
		return Result;
	}
	if (!CanEdit(Result.Reason))
	{
		return Result;
	}
	const FAssemblyBoneDefinition* Definition = FindBoneDefinition(TypeId);
	if (!Definition)
	{
		Result.Reason = LOCTEXT("UnknownBone", "本关卡没有这种骨头。");
		return Result;
	}
	Result = AssemblyRules::MakePlacement(RoundGrid, Definition->Footprint, AnchorCell, QuarterTurns);
	if (!Result.Reason.IsEmpty())
	{
		return Result;
	}
	if (IgnoreId.IsValid())
	{
		const FAssemblyBoneInstance* Existing = FindBone(IgnoreId);
		if (!Existing || Existing->TypeId != TypeId)
		{
			Result.Reason = LOCTEXT("InvalidMove", "找不到要移动的同类型骨头。");
			return Result;
		}
	}
	else if (GetRemainingQuantity(TypeId) <= 0)
	{
		Result.Reason = LOCTEXT("NoStock", "该组件的剩余数量不足。");
		return Result;
	}
	Result.bValid = AssemblyRules::ValidateLayout(RoundGrid, InstalledBones, &Result.OccupiedCells, IgnoreId, Result.Reason);
	return Result;
}

bool UAssemblyComponent::TryInstallBone(FName TypeId, FIntPoint AnchorCell, int32 QuarterTurns,
	FGuid& OutInstanceId, FText& OutReason)
{
	OutInstanceId.Invalidate();
	if (!CanMutate(OutReason))
	{
		return false;
	}
	const FAssemblyPlacementResult Result = ValidateBonePlacement(TypeId, AnchorCell, QuarterTurns, FGuid());
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	FAssemblyBoneInstance Bone;
	Bone.InstanceId = FGuid::NewGuid();
	Bone.TypeId = TypeId;
	Bone.AnchorCell = Result.AnchorCell;
	Bone.QuarterTurns = Result.QuarterTurns;
	Bone.OccupiedCells = Result.OccupiedCells;
	Bone.LocalVisualTransform = Result.LocalVisualTransform;
	InstalledBones.Add(Bone);
	--RemainingQuantities.FindChecked(TypeId);
	OutInstanceId = Bone.InstanceId;
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TryMoveBone(FGuid InstanceId, FIntPoint AnchorCell, int32 QuarterTurns, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	FAssemblyBoneInstance* Bone = InstalledBones.FindByPredicate([InstanceId](const FAssemblyBoneInstance& B) { return B.InstanceId == InstanceId; });
	if (!Bone)
	{
		OutReason = LOCTEXT("MissingBone", "找不到这块骨头。");
		return false;
	}
	const FAssemblyPlacementResult Result = ValidateBonePlacement(Bone->TypeId, AnchorCell, QuarterTurns, InstanceId);
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	if (Bone->AnchorCell != Result.AnchorCell || Bone->QuarterTurns != Result.QuarterTurns)
	{
		Bone->AnchorCell = Result.AnchorCell;
		Bone->QuarterTurns = Result.QuarterTurns;
		Bone->OccupiedCells = Result.OccupiedCells;
		Bone->LocalVisualTransform = Result.LocalVisualTransform;
		NotifyChanged();
	}
	return true;
}

bool UAssemblyComponent::ValidateMuscleEndpoints(const FAssemblyMuscleEndpoint& A, const FAssemblyMuscleEndpoint& B,
	EAssemblyMuscleKey Key, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!AssemblyRules::IsValidKey(Key))
	{
		OutReason = LOCTEXT("SelectKeyFirst", "安装肌肉前请先选择 W、A、S 或 D。");
		return false;
	}
	for (const FAssemblyMuscleEndpoint* Endpoint : { &A, &B })
	{
		const FAssemblyBoneInstance* Bone = FindBone(Endpoint->BoneId);
		const FAssemblyBoneDefinition* Definition = Bone ? FindBoneDefinition(Bone->TypeId) : nullptr;
		if (!Definition || Definition->PartKind != EAssemblyRigidPartKind::Bone)
		{
			OutReason = LOCTEXT("MissingEndpointBone", "肌肉两端必须连接已安装的普通骨头；核心和独立关节零件不能作为连接点。");
			return false;
		}
		const bool bOnBone = IsPointOnBoneDefinition(*Definition, Endpoint->BoneLocalPoint);
		if (!bOnBone)
		{
			OutReason = LOCTEXT("PointOutsideBone", "肌肉连接点必须位于对应骨头的实际轮廓内。");
			return false;
		}
	}
	if (A.BoneId == B.BoneId && A.BoneLocalPoint.Equals(B.BoneLocalPoint, UE_KINDA_SMALL_NUMBER))
	{
		OutReason = LOCTEXT("SameEndpoint", "允许连接同一块骨头，但必须选择两个不同的位置。");
		return false;
	}
	return true;
}

FAssemblyValidationResult UAssemblyComponent::ValidateMusclePlacement(FName TypeId,
	const FAssemblyMuscleEndpoint& EndpointA, const FAssemblyMuscleEndpoint& EndpointB,
	EAssemblyMuscleKey Key, FGuid IgnoreId) const
{
	FAssemblyValidationResult Result;
	if (!CanEdit(Result.Reason))
	{
		return Result;
	}
	if (!FindMuscleDefinition(TypeId))
	{
		Result.Reason = LOCTEXT("UnknownMuscle", "本关卡没有这种肌肉。");
		return Result;
	}
	if (IgnoreId.IsValid())
	{
		const FAssemblyMuscleInstance* Existing = FindMuscle(IgnoreId);
		if (!Existing || Existing->TypeId != TypeId)
		{
			Result.Reason = LOCTEXT("InvalidMuscleMove", "找不到要调整的同类型肌肉。");
			return Result;
		}
	}
	else if (GetRemainingQuantity(TypeId) <= 0)
	{
		Result.Reason = LOCTEXT("NoStock", "该组件的剩余数量不足。");
		return Result;
	}
	Result.bValid = ValidateMuscleEndpoints(EndpointA, EndpointB, Key, Result.Reason);
	return Result;
}

bool UAssemblyComponent::TryInstallMuscle(FName TypeId, const FAssemblyMuscleEndpoint& EndpointA,
	const FAssemblyMuscleEndpoint& EndpointB, FGuid& OutInstanceId, FText& OutReason)
{
	OutInstanceId.Invalidate();
	if (!CanMutate(OutReason))
	{
		return false;
	}
	const FAssemblyValidationResult Result = ValidateMusclePlacement(TypeId, EndpointA, EndpointB, SelectedKey, FGuid());
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	FAssemblyMuscleInstance Muscle;
	Muscle.InstanceId = FGuid::NewGuid();
	Muscle.TypeId = TypeId;
	Muscle.EndpointA = EndpointA;
	Muscle.EndpointB = EndpointB;
	Muscle.BindingKey = SelectedKey;
	InstalledMuscles.Add(Muscle);
	--RemainingQuantities.FindChecked(TypeId);
	OutInstanceId = Muscle.InstanceId;
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TryMoveMuscle(FGuid InstanceId, const FAssemblyMuscleEndpoint& EndpointA,
	const FAssemblyMuscleEndpoint& EndpointB, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	FAssemblyMuscleInstance* Muscle = InstalledMuscles.FindByPredicate([InstanceId](const FAssemblyMuscleInstance& M) { return M.InstanceId == InstanceId; });
	if (!Muscle)
	{
		OutReason = LOCTEXT("MissingMuscle", "找不到这条肌肉。");
		return false;
	}
	const FAssemblyValidationResult Result = ValidateMusclePlacement(Muscle->TypeId, EndpointA, EndpointB, Muscle->BindingKey, InstanceId);
	OutReason = Result.Reason;
	if (!Result.bValid)
	{
		return false;
	}
	Muscle->EndpointA = EndpointA;
	Muscle->EndpointB = EndpointB;
	NotifyChanged();
	return true;
}

bool UAssemblyComponent::TrySetMuscleKey(FGuid InstanceId, EAssemblyMuscleKey Key, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	FAssemblyMuscleInstance* Muscle = InstalledMuscles.FindByPredicate([InstanceId](const FAssemblyMuscleInstance& M) { return M.InstanceId == InstanceId; });
	if (!Muscle)
	{
		OutReason = LOCTEXT("MissingMuscle", "找不到这条肌肉。");
		return false;
	}
	if (!ValidateMuscleEndpoints(Muscle->EndpointA, Muscle->EndpointB, Key, OutReason))
	{
		return false;
	}
	if (Muscle->BindingKey != Key)
	{
		Muscle->BindingKey = Key;
		NotifyChanged();
	}
	return true;
}

bool UAssemblyComponent::TryRemoveInstance(FGuid InstanceId, FText& OutReason)
{
	if (!CanMutate(OutReason) || !CanEdit(OutReason))
	{
		return false;
	}
	TArray<FAssemblyBoneInstance> EffectiveBones = InstalledBones;
	TArray<FAssemblyJointInstance> EffectiveJoints = InstalledJoints;
	if (RoundPlacementMode == EAssemblyPlacementMode::Free)
	{
		if (!BuildEffectiveFreeBoneSnapshot(EffectiveBones, OutReason)) { return false; }
		EffectiveJoints = AssemblyFreeRules::BuildJoints(RoundFreeSettings, RoundBoneDefinitions, EffectiveBones, InstalledJoints);
	}
	auto PreservesCurrentConnections = [&](const TArray<FAssemblyBoneInstance>& AfterBones,
		const TArray<FAssemblyMuscleInstance>& AfterMuscles, const TArray<FAssemblyJointInstance>& AfterJoints, FGuid Removed)
	{
		const TSet<FGuid> Before = GetFreeCoreReachable(EffectiveBones, InstalledMuscles, EffectiveJoints);
		const TSet<FGuid> After = GetFreeCoreReachable(AfterBones, AfterMuscles, AfterJoints);
		for (FGuid Id : Before) { if (Id != Removed && !After.Contains(Id)) { return false; } }
		return true;
	};
	const int32 MuscleIndex = InstalledMuscles.IndexOfByPredicate([InstanceId](const FAssemblyMuscleInstance& M) { return M.InstanceId == InstanceId; });
	if (MuscleIndex != INDEX_NONE)
	{
		if (RoundPlacementMode == EAssemblyPlacementMode::Free && RoundFreeSettings.bRejectDisconnectingRemoval)
		{
			TArray<FAssemblyMuscleInstance> Remaining = InstalledMuscles;
			Remaining.RemoveAt(MuscleIndex);
			if (!PreservesCurrentConnections(EffectiveBones, Remaining, EffectiveJoints, FGuid()))
			{
				OutReason = LOCTEXT("DisconnectingMuscle", "拆下这条肌肉会使其他骨头与核心断开。");
				return false;
			}
		}
		++RemainingQuantities.FindChecked(InstalledMuscles[MuscleIndex].TypeId);
		InstalledMuscles.RemoveAt(MuscleIndex);
		if (RoundPlacementMode == EAssemblyPlacementMode::Free)
		{
			InstalledBones = MoveTemp(EffectiveBones);
			InstalledJoints = MoveTemp(EffectiveJoints);
		}
		NotifyChanged();
		return true;
	}
	const int32 BoneIndex = InstalledBones.IndexOfByPredicate([InstanceId](const FAssemblyBoneInstance& B) { return B.InstanceId == InstanceId; });
	if (BoneIndex == INDEX_NONE)
	{
		OutReason = LOCTEXT("MissingInstance", "找不到该安装实例；核心不可拆卸。");
		return false;
	}
	if (InstalledMuscles.ContainsByPredicate([InstanceId](const FAssemblyMuscleInstance& M)
		{ return M.EndpointA.BoneId == InstanceId || M.EndpointB.BoneId == InstanceId; }))
	{
		OutReason = LOCTEXT("BoneHasMuscles", "请先拆下连接在这块骨头上的肌肉。");
		return false;
	}
	if (RoundPlacementMode == EAssemblyPlacementMode::Grid
		&& !AssemblyRules::ValidateLayout(RoundGrid, InstalledBones, nullptr, InstanceId, OutReason))
	{
		return false;
	}
	if (RoundPlacementMode == EAssemblyPlacementMode::Free && RoundFreeSettings.bRejectDisconnectingRemoval)
	{
		TArray<FAssemblyBoneInstance> RemainingBones = EffectiveBones;
		RemainingBones.RemoveAt(BoneIndex);
		TArray<FAssemblyJointInstance> RemainingJoints = EffectiveJoints;
		RemainingJoints.RemoveAll([InstanceId](const FAssemblyJointInstance& J) { return J.BoneA == InstanceId || J.BoneB == InstanceId; });
		if (!PreservesCurrentConnections(RemainingBones, InstalledMuscles, RemainingJoints, InstanceId))
		{
			OutReason = LOCTEXT("DisconnectingBone", "拆下这块骨头会使其他骨头与核心断开。");
			return false;
		}
	}
	++RemainingQuantities.FindChecked(InstalledBones[BoneIndex].TypeId);
	EffectiveBones.RemoveAt(BoneIndex);
	InstalledBones = MoveTemp(EffectiveBones);
	BonePoseSources.Remove(InstanceId);
	if (RoundPlacementMode == EAssemblyPlacementMode::Free) { RebuildFreeJoints(&InstalledBones); }
	NotifyChanged();
	return true;
}

FAssemblyValidationResult UAssemblyComponent::ValidateAssembly() const
{
	if (RoundPlacementMode != EAssemblyPlacementMode::Free) { return ValidateAssemblyState(InstalledBones, InstalledJoints); }
	TArray<FAssemblyBoneInstance> EffectiveBones;
	FAssemblyValidationResult Result;
	if (!BuildEffectiveFreeBoneSnapshot(EffectiveBones, Result.Reason)) { return Result; }
	// Validation is read-only. Playing keeps confirmed physical edges, including explicit breaks.
	return ValidateAssemblyState(EffectiveBones, InstalledJoints);
}

FAssemblyValidationResult UAssemblyComponent::ValidateAssemblyState(const TArray<FAssemblyBoneInstance>& Bones,
	const TArray<FAssemblyJointInstance>& Joints) const
{
	FAssemblyValidationResult Result;
	if (!bInitialized)
	{
		Result.Reason = LOCTEXT("NotInitialized", "请先初始化组装组件。");
		return Result;
	}
	const bool bConfigValid = RoundPlacementMode == EAssemblyPlacementMode::Free
		? AssemblyFreeRules::ValidateConfiguration(RoundFreeSettings, RoundBoneDefinitions, RoundMuscleDefinitions, Result.Reason)
		: AssemblyRules::ValidateConfiguration(RoundGrid, RoundBoneDefinitions, RoundMuscleDefinitions, Result.Reason);
	if (!bConfigValid || (RoundPlacementMode == EAssemblyPlacementMode::Grid
		&& !AssemblyRules::ValidateLayout(RoundGrid, InstalledBones, nullptr, FGuid(), Result.Reason)))
	{
		return Result;
	}
	TSet<FGuid> Ids;
	TMap<FName, int32> Used;
	for (const FAssemblyBoneInstance& Bone : Bones)
	{
		const FAssemblyBoneDefinition* Definition = FindBoneDefinition(Bone.TypeId);
		if (!Definition || !Bone.InstanceId.IsValid() || Ids.Contains(Bone.InstanceId))
		{
			Result.Reason = LOCTEXT("InvalidInstance", "安装实例标识或类型无效。");
			return Result;
		}
		const FAssemblyPlacementResult Expected = RoundPlacementMode == EAssemblyPlacementMode::Free
			? AssemblyFreeRules::MakePlacement(Bone.LocalPosition, Bone.RotationDegrees)
			: AssemblyRules::MakePlacement(RoundGrid, Definition->Footprint, Bone.AnchorCell, Bone.QuarterTurns);
		if (!Expected.Reason.IsEmpty() || (RoundPlacementMode == EAssemblyPlacementMode::Grid
			&& (Expected.QuarterTurns != Bone.QuarterTurns || Expected.OccupiedCells != Bone.OccupiedCells))
			|| !Expected.LocalVisualTransform.Equals(Bone.LocalVisualTransform))
		{
			Result.Reason = LOCTEXT("InconsistentGeometry", "骨头旋转、占格和显示变换不一致。");
			return Result;
		}
		if (RoundPlacementMode == EAssemblyPlacementMode::Free && Phase == EAssemblyPhase::Assembly
			&& !AssemblyFreeRules::ValidateGeometry(RoundFreeSettings, RoundBoneDefinitions, Bones, Bone.TypeId, Expected, Bone.InstanceId, Result.Reason))
		{
			return Result;
		}
		Ids.Add(Bone.InstanceId);
		++Used.FindOrAdd(Bone.TypeId);
	}
	for (const FAssemblyMuscleInstance& Muscle : InstalledMuscles)
	{
		if (!FindMuscleDefinition(Muscle.TypeId) || !Muscle.InstanceId.IsValid() || Ids.Contains(Muscle.InstanceId))
		{
			Result.Reason = LOCTEXT("InvalidInstance", "安装实例标识或类型无效。");
			return Result;
		}
		if (!ValidateMuscleEndpoints(Muscle.EndpointA, Muscle.EndpointB, Muscle.BindingKey, Result.Reason))
		{
			return Result;
		}
		if (Phase == EAssemblyPhase::Assembly && Muscle.bContracting)
		{
			Result.Reason = LOCTEXT("AssemblyDrive", "组装阶段不能驱动肌肉。");
			return Result;
		}
		Ids.Add(Muscle.InstanceId);
		++Used.FindOrAdd(Muscle.TypeId);
	}
	TMap<FGuid, TSet<FGuid>> UsedJointPairs;
	auto ValidateRigidAnchor = [](const FAssemblyBoneDefinition& Definition, FVector2D Point)
	{
		AssemblySurfaceGeometry::FShape Shape;
		FText Reason;
		return AssemblySurfaceGeometry::BuildShape(Definition, FTransform::Identity, Shape, Reason)
			&& AssemblySurfaceGeometry::IsOnBoundary(Shape, Point);
	};
	for (const FAssemblyJointInstance& Joint : Joints)
	{
		const FAssemblyBoneInstance* B = Bones.FindByPredicate([&Joint](const FAssemblyBoneInstance& Bone) { return Bone.InstanceId == Joint.BoneB; });
		const FAssemblyBoneInstance* A = Joint.BoneA.IsValid()
			? Bones.FindByPredicate([&Joint](const FAssemblyBoneInstance& Bone) { return Bone.InstanceId == Joint.BoneA; }) : nullptr;
		const FAssemblyBoneDefinition* DB = B ? FindBoneDefinition(B->TypeId) : nullptr;
		const FAssemblyBoneDefinition* DA = A ? FindBoneDefinition(A->TypeId) : nullptr;
		if (!Joint.InstanceId.IsValid() || Ids.Contains(Joint.InstanceId) || !DB || (Joint.BoneA.IsValid() && !DA)
			|| Joint.BoneA == Joint.BoneB || !AssemblyFreeRules::IsJointKindValid(Joint.Kind)
			|| (DA && DA->PartKind == EAssemblyRigidPartKind::Bone && DB->PartKind == EAssemblyRigidPartKind::Bone)
			|| UsedJointPairs.FindRef(Joint.BoneA).Contains(Joint.BoneB)
			|| UsedJointPairs.FindRef(Joint.BoneB).Contains(Joint.BoneA)
			|| !AssemblyFreeRules::IsJointKindAllowed(DA, DB, Joint.Kind)
			|| !AssemblyFreeRules::ValidateJointLimits(Joint.MinAngleDegrees, Joint.MaxAngleDegrees)
			|| !ValidateRigidAnchor(*DB, Joint.LocalAnchorB)
			|| (DA && !ValidateRigidAnchor(*DA, Joint.LocalAnchorA))
			|| (!DA && (!FMath::IsFinite(Joint.LocalAnchorA.X) || !FMath::IsFinite(Joint.LocalAnchorA.Y)
				|| !FMath::IsNearlyEqual(Joint.LocalAnchorA.Size(), static_cast<double>(RoundFreeSettings.CoreRadius), 0.001))))
		{
			Result.Reason = LOCTEXT("InvalidJoint", "关节标识、连接点、种类或限角无效。");
			return Result;
		}
		if (Phase == EAssemblyPhase::Assembly)
		{
			const FVector PointA = A ? A->LocalVisualTransform.TransformPosition(FVector(Joint.LocalAnchorA.X, 0.0, Joint.LocalAnchorA.Y))
				: FVector(RoundFreeSettings.CoreLocalPosition.X + Joint.LocalAnchorA.X, 0.0,
					RoundFreeSettings.CoreLocalPosition.Y + Joint.LocalAnchorA.Y);
			const FVector PointB = B->LocalVisualTransform.TransformPosition(FVector(Joint.LocalAnchorB.X, 0.0, Joint.LocalAnchorB.Y));
			if (!FMath::IsFinite(FVector::DistSquared(PointA, PointB))
				|| FVector::DistSquared(PointA, PointB) > FMath::Square(RoundFreeSettings.ConnectionDistance + AssemblySurfaceGeometry::Tolerance))
			{
				Result.Reason = LOCTEXT("StaleSurfaceConnection", "关节连接已离开实际组件表面，请重新摆放连接后再开始操控。");
				return Result;
			}
			AssemblySurfaceGeometry::FContact EndpointContact;
			if (!AssemblyFreeRules::GetEndpointConnection(RoundFreeSettings, DA,
				A ? A->LocalVisualTransform : FTransform::Identity, DB, B->LocalVisualTransform, EndpointContact)
				|| !FVector2D(PointA.X, PointA.Z).Equals(EndpointContact.PointA, AssemblySurfaceGeometry::Tolerance * 2.0)
				|| !FVector2D(PointB.X, PointB.Z).Equals(EndpointContact.PointB, AssemblySurfaceGeometry::Tolerance * 2.0))
			{
				Result.Reason = LOCTEXT("InvalidEndpointConnection", "关节只能连接骨头端点；圆形连接件须径向对齐，异形关节须将端口方向与骨头轴线对齐后再开始操控。");
				return Result;
			}
		}
		UsedJointPairs.FindOrAdd(Joint.BoneA).Add(Joint.BoneB);
		Ids.Add(Joint.InstanceId);
	}
	auto CheckQuantity = [this, &Used](FName TypeId, int32 Initial)
	{
		const int32* Remaining = RemainingQuantities.Find(TypeId);
		return Remaining && *Remaining >= 0 && static_cast<int64>(*Remaining) + Used.FindRef(TypeId) == Initial;
	};
	if (RemainingQuantities.Num() != RoundBoneDefinitions.Num() + RoundMuscleDefinitions.Num()
		|| RoundBoneDefinitions.ContainsByPredicate([&CheckQuantity](const FAssemblyBoneDefinition& D) { return !CheckQuantity(D.TypeId, D.InitialQuantity); })
		|| RoundMuscleDefinitions.ContainsByPredicate([&CheckQuantity](const FAssemblyMuscleDefinition& D) { return !CheckQuantity(D.TypeId, D.InitialQuantity); }))
	{
		Result.Reason = LOCTEXT("InvalidInventory", "库存不一致：剩余数量加安装数量必须等于初始数量。");
		return Result;
	}
	if (RoundPlacementMode == EAssemblyPlacementMode::Free && Phase == EAssemblyPhase::Assembly && !IsFreeConnected(Bones, InstalledMuscles, Joints))
	{
		Result.Reason = LOCTEXT("DisconnectedStructure", "开始操控前，每块骨头必须通过关节或肌肉连接到核心。");
		return Result;
	}
	Result.bValid = true;
	return Result;
}

bool UAssemblyComponent::SetMuscleInput(EAssemblyMuscleKey Key, bool bPressed)
{
	if (!AssemblyRules::IsValidKey(Key)
		|| (bPressed && (bNotifying || bInLifecycleCallback || bEndingPlay || !CanControlMuscles())))
	{
		return false;
	}
	TArray<FGuid> Changed;
	for (FAssemblyMuscleInstance& Muscle : InstalledMuscles)
	{
		if (Muscle.BindingKey == Key && Muscle.bContracting != bPressed)
		{
			Muscle.bContracting = bPressed;
			Changed.Add(Muscle.InstanceId);
		}
	}
	for (const FGuid Id : Changed)
	{
		RequestMuscleDrive(Id, bPressed);
	}
	return true;
}

bool UAssemblyComponent::SetCoreRotationInput(bool bHeld)
{
	// Stops remain legal during callbacks and teardown, so a previously forwarded drive can be released.
	if (bHeld && (bNotifying || bInLifecycleCallback || bEndingPlay || !bInitialized || Phase != EAssemblyPhase::Playing))
	{
		return false;
	}
	if (bCoreRotationHeld != bHeld)
	{
		bCoreRotationHeld = bHeld;
		RequestCoreRotation(bHeld);
	}
	return true;
}

void UAssemblyComponent::ClearInputState()
{
	// Release is allowed inside notifications/lifecycle callbacks. Clear every state before invoking
	// listeners so a nested stop cannot leave later instances held or replay an outer stale press.
	TArray<FGuid> Active;
	for (FAssemblyMuscleInstance& Muscle : InstalledMuscles)
	{
		if (Muscle.bContracting)
		{
			Muscle.bContracting = false;
			Active.Add(Muscle.InstanceId);
		}
	}
	const bool bWasRotating = bCoreRotationHeld;
	bCoreRotationHeld = false;
	for (const FGuid Id : Active)
	{
		RequestMuscleDrive(Id, false);
	}
	if (bWasRotating)
	{
		RequestCoreRotation(false);
	}
}

void UAssemblyComponent::NotifyChanged()
{
	TGuardValue<bool> Guard(bNotifying, true);
	OnAssemblyChanged.Broadcast();
}

void UAssemblyComponent::NotifyPhase()
{
	TGuardValue<bool> Guard(bNotifying, true);
	OnPhaseChanged.Broadcast(Phase);
}

void UAssemblyComponent::RequestMuscleDrive(FGuid Id, bool bContracting)
{
	auto MatchesCurrentState = [&]()
	{
		if (!IsValid(this)) { return false; }
		const FAssemblyMuscleInstance* Muscle = FindMuscle(Id);
		return Muscle && Muscle->bContracting == bContracting
			&& (!bContracting || (!bEndingPlay && CanControlMuscles()));
	};
	if (!MatchesCurrentState()) { return; }
	TGuardValue<bool> Guard(bNotifying, true);
	OnMuscleDriveRequestedNative.Broadcast(Id, bContracting);
	// Native listeners can synchronously stop or shut down the session. Re-read the instance rather
	// than forwarding the pre-callback edge to Blueprint/the physics bridge after it was canceled.
	if (MatchesCurrentState()) { OnMuscleDriveRequested.Broadcast(Id, bContracting); }
}

void UAssemblyComponent::RequestCoreRotation(bool bHeld)
{
	auto MatchesCurrentState = [&]()
	{
		return IsValid(this) && bCoreRotationHeld == bHeld
			&& (!bHeld || (!bEndingPlay && bInitialized && Phase == EAssemblyPhase::Playing));
	};
	if (!MatchesCurrentState()) { return; }
	TGuardValue<bool> Guard(bNotifying, true);
	OnCoreRotationRequestedNative.Broadcast(bHeld);
	if (MatchesCurrentState()) { OnCoreRotationRequested.Broadcast(bHeld); }
}

#undef LOCTEXT_NAMESPACE
