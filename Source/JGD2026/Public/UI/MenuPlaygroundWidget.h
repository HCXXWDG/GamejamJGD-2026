#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Layout/Geometry.h"
#include "Subsystems/WorldSubsystem.h"
#include "MenuPlaygroundWidget.generated.h"

class UCanvasPanel;
class UPrimitiveComponent;
class UBoxComponent;
class UPhysicsHandleComponent;
class UPaperSprite;
class UAssemblySpriteImage;
class UAssemblyPlayingToolbarWidget;
class UMenuPlaygroundWidget;
class UTextBlock;
class UJGDLevelSelectionRatings;

UENUM(BlueprintType)
enum class EMenuPieceCollision : uint8 { Box, Circle };

/** Menu decoration only. The sprite's cooked triangles supply its aspect ratio. */
USTRUCT(BlueprintType)
struct JGD2026_API FMenuPlaygroundPieceDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") TObjectPtr<UPaperSprite> Sprite;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") FText Label;
	// Logical pixels from the lower-left corner of the decoration area, positive Y upward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") FVector2D BottomLeftOffset = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") float AngleDegrees = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") EMenuPieceCollision Collision = EMenuPieceCollision::Box;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") FVector2D CollisionScale = FVector2D(1,1);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu", meta=(ClampMin="0.01")) float MassKg = .25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Menu") FVector2D ScatterImpulse = FVector2D(45,60);
};

UCLASS()
class JGD2026_API UMenuPhysicsPieceWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void InitializePiece(UMenuPlaygroundWidget* Owner, int32 Index, UPaperSprite* Sprite, FVector2D Size, const FText& Label);
protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FReply NativeOnMouseMove(const FGeometry&, const FPointerEvent&) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent&) override;
private:
	UPROPERTY(Transient) TWeakObjectPtr<UMenuPlaygroundWidget> Playground;
	UPROPERTY(Transient) TObjectPtr<UAssemblySpriteImage> Image;
	int32 PieceIndex = INDEX_NONE;
};

/** Actual Chaos bodies presented inside the existing hand-drawn menu canvas. */
UCLASS()
class JGD2026_API UMenuPlaygroundWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UMenuPlaygroundWidget(const FObjectInitializer& Initializer);
	void InitializeMenu(UUserWidget* InMenu);
	void BeginPieceDrag(int32 Index, FVector2D Screen);
	void MovePieceDrag(FVector2D Screen);
	void EndPieceDrag(bool bThrow);
	void AdvanceMenuTransition(float DeltaTime);
	UFUNCTION(BlueprintPure, Category="Menu|Playground") bool HasScattered() const { return bScattered; }
	UFUNCTION(BlueprintPure, Category="Menu|Playground") TArray<UPrimitiveComponent*> GetPieceBodies() const;
	UFUNCTION(BlueprintPure, Category="Menu|Transition") bool IsTransitioning() const { return TransitionStage != 0; }
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Menu|Playground") TArray<FMenuPlaygroundPieceDefinition> PieceDefinitions;
	// One scale for every source pixel; no per-axis stretching or editor-only source dimensions.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Menu|Playground", meta=(ClampMin="0.001")) float ArtUnitsPerPixel = 112.0f / 960.0f;
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry&, float DeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32 LayerId, const FWidgetStyle&, bool bParentEnabled) const override;
	virtual void NativeDestruct() override;
private:
	UPROPERTY(Transient) TWeakObjectPtr<UUserWidget> Menu;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GuideCaption;
	UPROPERTY(Transient) TObjectPtr<UJGDLevelSelectionRatings> LevelRatings;
	float RatingRefreshTime = 0;
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> DraggedBody;
	UPROPERTY(Transient) TObjectPtr<UPhysicsHandleComponent> GrabHandle;
	UPROPERTY(Transient) TObjectPtr<UAssemblyPlayingToolbarWidget> SettingsWidget;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Actors;
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> Bodies;
	UPROPERTY(Transient) TArray<TObjectPtr<UMenuPhysicsPieceWidget>> Pieces;
	UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> Obstacles;
	UPROPERTY(Transient) TWeakObjectPtr<UWidget> MainPanel;
	UPROPERTY(Transient) TWeakObjectPtr<UWidget> SelectorPanel;
	TArray<FVector2D> PieceSizes;
	TArray<FVector2D> PieceScatterImpulses;
	FVector2D BuiltViewSize = FVector2D::ZeroVector;
	FGeometry TickGeometry;
	FVector2D LastPointer = FVector2D::ZeroVector;
	FVector ThrowVelocity = FVector::ZeroVector;
	double LastPointerTime = 0;
	float TransitionTime = 0;
	float GuideTime = 0;
	uint8 TransitionStage = 0;
	bool bToSelector = true;
	bool bBuilt = false;
	bool bScattered = false;
	void BuildPieces(FVector2D ViewSize);
	void ClearPieces();
	void KeepPiecesInsideView(FVector2D ViewSize);
	void UpdateObstacles();
	void BeginMenuTransition(bool bForward);
	void TickMenuTransition(float DeltaTime);
	void InvokeMenuFunction(FName Function);
	UBoxComponent* MakeObstacle();
	UFUNCTION() void HandleStart();
	UFUNCTION() void HandleBack();
	UFUNCTION() void HandleSettings();
};

/** Adds the optional playground to the existing Menu widget without editing that asset. */
UCLASS()
class JGD2026_API UMenuExperienceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
	UPROPERTY(Transient) TWeakObjectPtr<UMenuPlaygroundWidget> Playground;
	float RetryTime = 0;
};
