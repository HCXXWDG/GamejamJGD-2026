#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AssemblyPlayPreferences.generated.h"

/** Local player preferences. Saved only to the generated GameUserSettings.ini. */
UCLASS(Config = GameUserSettings)
class JGD2026_API UAssemblyPlayPreferences : public UObject
{
	GENERATED_BODY()

public:
	static UAssemblyPlayPreferences* Get();
	float GetMasterVolume() const;
	// Changes the in-memory value; applying and saving remain explicit.
	void SetMasterVolume(float Value);
	// Uses this world's audio device, including sounds without an assigned SoundClass.
	void ApplyAudio(UObject* WorldContext) const;
	void SavePreferences();

private:
	UPROPERTY(Config)
	float MasterVolume = 1.0f;
};

namespace AssemblyPlayingHUD
{
	/** Straight-line progress toward the destination in the game's XZ plane. */
	JGD2026_API float CalculateRouteProgress(const FVector& Start, const FVector& Goal, const FVector& Core);
}
