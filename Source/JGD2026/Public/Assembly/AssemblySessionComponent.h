#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineBaseTypes.h"
#include "Assembly/AssemblyTypes.h"
#include "Assembly/AssemblyWorkbenchTypes.h"
#include "AssemblySessionComponent.generated.h"

class APlayerController;
class UAssemblyComponent;
class UAssemblyInteractionComponent;
class UAssemblyWorkbenchComponent;
class UAssemblyHUDWidget;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class UInputAction;
class UGameViewportClient;
class SWidget;

/** Local assembly lifecycle and input. Reuses the character's model, presentation and camera. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblySessionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAssemblySessionComponent();
	// Automatic initialization runs after the owning pawn's BeginPlay and local possession.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Session")
	bool bAutoInitialize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	bool bCreateHUD = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	TSubclassOf<UAssemblyHUDWidget> HUDClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	int32 HUDZOrder = 10;
	// Applies only to a HUD created by this component. Provided HUDs retain their input configuration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	bool bConfigureHUDInputMode = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputMappingContext> InputMappingContext;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	int32 MappingPriority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> WAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> AAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> SAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> DAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	float RotationStep = 90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	bool bBindBackspaceToRestart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	bool bBindJointBreakKey = true;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyWorkbenchChanged OnSessionChanged;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyWorkbenchMessage OnMessage;

	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	bool InitializeSession(APlayerController* PC, FText& OutReason);
	// Explicit shutdown suspends automatic initialization until InitializeSession is called again.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	void ShutdownSession();
	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	bool StartPlaying(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	bool RestartSession(FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Session")
	UAssemblyWorkbenchComponent* GetWorkbench() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Session")
	UAssemblyComponent* GetAssemblyComponent() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Session")
	APlayerController* GetPlayerController() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	UAssemblyHUDWidget* GetHUD() const;
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void ShowMessage(FText Message);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Input")
	void ClearHeldInput();
	// Initializes an existing user HUD; its viewport membership and input mode stay caller-owned.
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void SetHUD(UAssemblyHUDWidget* InHUD);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyComponent> BoundAssembly;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyInteractionComponent> BoundInteraction;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyWorkbenchComponent> BoundWorkbench;
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundController;
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundInputSubsystem;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AddedMappingContext;
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> SessionInput;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblyHUDWidget> HUD;
	UPROPERTY(Transient)
	TWeakObjectPtr<UGameViewportClient> SavedHUDViewport;
	TArray<uint32> ActionBindingHandles;
	TSet<EAssemblyMuscleKey> HeldMuscleKeys;
	FDelegateHandle DeactivationHandle;
	FDelegateHandle BackgroundHandle;
	TWeakPtr<SWidget> PreviousKeyboardFocus;
	EMouseCaptureMode PreviousMouseCaptureMode = EMouseCaptureMode::NoCapture;
	EMouseLockMode PreviousMouseLockMode = EMouseLockMode::DoNotLock;
	bool bInitialized = false;
	bool bInitializing = false;
	bool bReleasing = false;
	bool bAutoInitializationSuppressed = false;
	bool bOwnedHUD = false;
	bool bHUDAddedToViewport = false;
	bool bHUDInputConfigured = false;
	bool bPreviousShowMouseCursor = false;
	bool bPreviousHideCursorDuringCapture = false;
	bool bPreviousViewportIgnoreInput = false;
	bool bIgnoreMoveInputAdded = false;
	bool bSpaceHeld = false;
	bool bViewportWasFocused = false;
	bool bModelRefreshPending = false;
	bool bReplayHeldMusclesPending = false;
	FText LastInitializationFailure;

	bool CanUseSession(FText& OutReason) const;
	bool BindSessionInput(FText& OutReason);
	void UnbindSessionInput();
	bool EnsureHUD(FText& OutReason);
	void DetachHUD();
	void RestoreHUDInput();
	void ReleaseSession();
	void UpdateMovementGate();
	void ApplyHeldMuscleInput();
	bool IsSessionViewportFocused() const;
	bool CanAcceptInput() const;
	void HandleApplicationDeactivated();
	void HandleMusclePressed(EAssemblyMuscleKey Key);
	void HandleMuscleReleased(EAssemblyMuscleKey Key);
	void HandleSpacePressed();
	void HandleSpaceReleased();
	void HandleRotatePressed();
	void HandleCancelPressed();
	void HandleDeletePressed();
	void HandleRestartPressed();
	void HandleJointBreakPressed();
	UFUNCTION()
	void HandleModelChanged();
	UFUNCTION()
	void HandleModelReset();
	UFUNCTION()
	void HandlePhaseChanged(EAssemblyPhase Phase);
	UFUNCTION()
	void HandleWorkbenchChanged();
	UFUNCTION()
	void HandleWorkbenchMessage(FText Message);
};
