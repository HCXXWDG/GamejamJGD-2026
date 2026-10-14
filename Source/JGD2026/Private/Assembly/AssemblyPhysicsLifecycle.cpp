#include "Assembly/AssemblyPhysicsLifecycle.h"

#define LOCTEXT_NAMESPACE "AssemblyPhysicsLifecycle"

bool IAssemblyPhysicsLifecycle::PrepareForPlaying_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = LOCTEXT("PrepareNotImplemented", "物理执行器尚未实现开始前的同步准备。");
	return false;
}

void IAssemblyPhysicsLifecycle::AbortPreparedPlaying_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot) {}

bool IAssemblyPhysicsLifecycle::StopBeforeReset_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = LOCTEXT("StopNotImplemented", "物理执行器尚未实现重开前的同步停止。");
	return false;
}

bool IAssemblyPhysicsLifecycle::ApplyAssemblyPhase_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = LOCTEXT("ApplyNotImplemented", "物理执行器尚未实现阶段应用的同步成功确认。");
	return false;
}

void IAssemblyPhysicsLifecycle::ShutdownPhysics_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot) {}

#undef LOCTEXT_NAMESPACE
