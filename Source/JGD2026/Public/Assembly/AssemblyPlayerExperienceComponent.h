#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "AssemblyPlayerExperienceComponent.generated.h"

class UBorder;
class ACameraActor;
class UAssemblySessionComponent;

UCLASS()
class JGD2026_API UAssemblyScreenFadeWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetFadeAlpha(float Alpha);
protected:
	virtual void NativeOnInitialized() override;
private:
	UPROPERTY(Transient) TObjectPtr<UBorder> Black;
};

/** Presentation and fall recovery; physical forces remain with the physics executor. */
UCLASS(ClassGroup=(Assembly), meta=(BlueprintSpawnableComponent))
class JGD2026_API UAssemblyPlayerExperienceComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAssemblyPlayerExperienceComponent();
	UFUNCTION(BlueprintPure, Category="Assembly|Experience")
	bool IsRestartTransitionActive() const { return FadeState != 0; }
	UFUNCTION(BlueprintPure, Category="Assembly|Experience")
	float GetFallThreshold() const { return FallThreshold; }
	UFUNCTION(BlueprintCallable, Category="Assembly|Experience")
	void RestartWithFade();
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Transient) TObjectPtr<ACameraActor> FollowCamera;
	UPROPERTY(Transient) TObjectPtr<UAssemblyScreenFadeWidget> FadeWidget;
	UPROPERTY(Transient) TWeakObjectPtr<AActor> PreviousView;
	UPROPERTY(Transient) TWeakObjectPtr<UAssemblySessionComponent> Session;
	float FallThreshold = -1200;
	float FadeTime = 0;
	uint8 FadeState = 0;
	bool bCameraReady = false;
	bool bWasPlaying = false;
};
