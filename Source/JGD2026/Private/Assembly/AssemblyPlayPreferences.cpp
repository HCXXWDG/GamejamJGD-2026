#include "Assembly/AssemblyPlayPreferences.h"

#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	float SanitizeVolume(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : 1.0f;
	}
}

UAssemblyPlayPreferences* UAssemblyPlayPreferences::Get()
{
	return GetMutableDefault<UAssemblyPlayPreferences>();
}

float UAssemblyPlayPreferences::GetMasterVolume() const
{
	return SanitizeVolume(MasterVolume);
}

void UAssemblyPlayPreferences::SetMasterVolume(float Value)
{
	MasterVolume = SanitizeVolume(Value);
}

void UAssemblyPlayPreferences::ApplyAudio(UObject* WorldContext) const
{
	if (!GEngine || !IsValid(WorldContext)) { return; }
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if (!World) { return; }
	// The handle retains the device while the setter queues its audio-thread command.
	FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
	if (AudioDevice.IsValid()) { AudioDevice->SetTransientPrimaryVolume(GetMasterVolume()); }
}

void UAssemblyPlayPreferences::SavePreferences()
{
	MasterVolume = SanitizeVolume(MasterVolume);
	// An explicit generated file prevents editor saves from altering Default*.ini.
	if (!GGameUserSettingsIni.IsEmpty()) { SaveConfig(CPF_Config, *GGameUserSettingsIni); }
}

float AssemblyPlayingHUD::CalculateRouteProgress(const FVector& Start, const FVector& Goal, const FVector& Core)
{
	if (Start.ContainsNaN() || Goal.ContainsNaN() || Core.ContainsNaN()) { return 0.0f; }
	const FVector2D InitialDelta(Goal.X - Start.X, Goal.Z - Start.Z);
	const FVector2D RemainingDelta(Goal.X - Core.X, Goal.Z - Core.Z);
	const double InitialDistance = InitialDelta.Size();
	const double RemainingDistance = RemainingDelta.Size();
	if (!FMath::IsFinite(InitialDistance) || !FMath::IsFinite(RemainingDistance)
		|| InitialDistance <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	const double Progress = 1.0 - RemainingDistance / InitialDistance;
	return FMath::IsFinite(Progress) ? static_cast<float>(FMath::Clamp(Progress, 0.0, 1.0)) : 0.0f;
}
