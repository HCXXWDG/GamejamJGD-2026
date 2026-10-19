#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AssemblyExperienceSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Player Experience"))
class JGD2026_API UAssemblyExperienceSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UAssemblyExperienceSettings() { CategoryName = TEXT("Game"); }
	UPROPERTY(Config, EditAnywhere, Category="Player", meta=(ClampMin="0.3", ClampMax="1.5"))
	float PlayerSizeScale = 0.8f;
	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="640"))
	float CameraOrthoWidth = 1536.0f;
	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0"))
	float CameraFollowSpeed = 7.0f;
	UPROPERTY(Config, EditAnywhere, Category="Restart", meta=(ClampMin="100"))
	float FallDistanceBelowMap = 500.0f;
	UPROPERTY(Config, EditAnywhere, Category="Restart", meta=(ClampMin="0.1"))
	float RestartFadeSeconds = 0.35f;
};
