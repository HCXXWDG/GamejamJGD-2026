#include "UI/MenuPlaygroundWidget.h"
#include "Audio/JGDInputFeedbackLibrary.h"
#include "Assembly/AssemblyPlayingToolbarWidget.h"
#include "Assembly/AssemblySpriteImage.h"
#include "Assembly/AssemblyUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BoxComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SphereComponent.h"
#include "Components/TextBlock.h"
#include "Rendering/DrawElements.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PaperSprite.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr double PlaygroundY = 100000.0;
	FVector ToPhysics(FVector2D Point) { return FVector(Point.X, PlaygroundY, -Point.Y); }
	FGeometry DisplayGeometry(const UWidget* Widget)
	{
		if (!Widget) return FGeometry();
		const FGeometry Tick=Widget->GetCachedGeometry();
		return Tick.GetLocalSize().X>0 && Tick.GetLocalSize().Y>0 ? Tick : Widget->GetPaintSpaceGeometry();
	}
	FVector2D SpritePixelSize(const UPaperSprite* Sprite)
	{
		if (!IsValid(Sprite) || Sprite->BakedRenderData.IsEmpty()) return FVector2D::ZeroVector;
		FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
		for (const FVector4& Vertex : Sprite->BakedRenderData)
		{
			Min.X=FMath::Min(Min.X,Vertex.X); Min.Y=FMath::Min(Min.Y,Vertex.Y);
			Max.X=FMath::Max(Max.X,Vertex.X); Max.Y=FMath::Max(Max.Y,Vertex.Y);
		}
		// Baked positions are in Unreal units and survive cooking, unlike SourceDimension.
		return (Max-Min)*Sprite->GetPixelsPerUnrealUnit();
	}
	UButton* FindButton(UWidget* Widget)
	{
		if (UButton* Button = Cast<UButton>(Widget)) return Button;
		if (UUserWidget* User = Cast<UUserWidget>(Widget)) return FindButton(User->GetRootWidget());
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			for (int32 Index=0; Index<Panel->GetChildrenCount(); ++Index)
				if (UButton* Button = FindButton(Panel->GetChildAt(Index))) return Button;
		return nullptr;
	}
	bool BindMenuAction(UWidget* Wrapper, UObject* Target, FName Handler)
	{
		// The existing art buttons broadcast OnButtonClick from OnReleased.
		// Replace that dispatcher rather than leaving its immediate map-switch listener active.
		if (!Wrapper) return false;
		FMulticastDelegateProperty* Event = FindFProperty<FMulticastDelegateProperty>(Wrapper->GetClass(), TEXT("OnButtonClick"));
		if (!Event) return false;
		FScriptDelegate Action; Action.BindUFunction(Target, Handler);
		Event->ClearDelegate(Wrapper);
		Event->AddDelegate(Action, Wrapper);
		return true;
	}
}

void UMenuPhysicsPieceWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Image = WidgetTree->ConstructWidget<UAssemblySpriteImage>();
	Image->SetVisibility(ESlateVisibility::Visible);
	WidgetTree->RootWidget = Image;
	SetRenderTransformPivot(FVector2D(.5,.5));
}

void UMenuPhysicsPieceWidget::InitializePiece(UMenuPlaygroundWidget* Owner, int32 Index, UPaperSprite* Sprite, FVector2D Size, const FText& Label)
{
	Playground = Owner; PieceIndex = Index;
	Image->SetSprite(Sprite); Image->SetDesiredSize(Size);
}

FReply UMenuPhysicsPieceWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Playground.IsValid())
	{
		Playground->BeginPieceDrag(PieceIndex, Event.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(Geometry,Event);
}
FReply UMenuPhysicsPieceWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (HasMouseCapture() && Playground.IsValid()) { Playground->MovePieceDrag(Event.GetScreenSpacePosition()); return FReply::Handled(); }
	return Super::NativeOnMouseMove(Geometry,Event);
}
FReply UMenuPhysicsPieceWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
	{
		if (Playground.IsValid()) Playground->EndPieceDrag(true);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(Geometry,Event);
}
void UMenuPhysicsPieceWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	if (Playground.IsValid()) Playground->EndPieceDrag(false);
	Super::NativeOnMouseCaptureLost(Event);
}

UMenuPlaygroundWidget::UMenuPlaygroundWidget(const FObjectInitializer& Initializer): Super(Initializer)
{
	const auto AddPiece = [this](const TCHAR* SpriteName, const TCHAR* Label, FVector2D Offset,
		float Angle, EMenuPieceCollision Collision, FVector2D Impulse)
	{
		const FString Path=FString::Printf(TEXT("/Game/Res/Art/Sprites/%s.%s"),SpriteName,SpriteName);
		ConstructorHelpers::FObjectFinder<UPaperSprite> Sprite(*Path);
		FMenuPlaygroundPieceDefinition& Piece=PieceDefinitions.AddDefaulted_GetRef();
		Piece.Sprite=Sprite.Object; Piece.Label=FText::FromString(Label);
		Piece.BottomLeftOffset=Offset; Piece.AngleDegrees=Angle;
		Piece.Collision=Collision; Piece.ScatterImpulse=Impulse;
	};
	AddPiece(TEXT("SP_Core_Eye"),TEXT("核心"),{225,365},0,EMenuPieceCollision::Circle,{-55,80});
	AddPiece(TEXT("SP_Bone_Long"),TEXT("长骨头"),{165,245},80,EMenuPieceCollision::Box,{45,64});
	AddPiece(TEXT("SP_Bone_Long"),TEXT("长骨头"),{305,255},-65,EMenuPieceCollision::Box,{-45,72});
	AddPiece(TEXT("SP_Bone_Short"),TEXT("短骨头"),{235,170},-15,EMenuPieceCollision::Box,{55,54});
	AddPiece(TEXT("SP_Joint_Round"),TEXT("紫色活动关节"),{80,160},0,EMenuPieceCollision::Circle,{-65,48});
	AddPiece(TEXT("SP_Joint_Bar"),TEXT("黄色直线关节"),{90,80},18,EMenuPieceCollision::Box,{45,70});
	AddPiece(TEXT("SP_Joint_Corner"),TEXT("黄色转角关节"),{185,80},12,EMenuPieceCollision::Box,{-35,55});
	AddPiece(TEXT("SP_Joint_Tee"),TEXT("黄色T形关节"),{315,80},0,EMenuPieceCollision::Box,{65,65});
	AddPiece(TEXT("SP_Joint_Cross"),TEXT("黄色十字关节"),{415,170},-15,EMenuPieceCollision::Box,{45,90});
	AddPiece(TEXT("SP_Joint_Fork"),TEXT("黄色分叉关节"),{410,300},0,EMenuPieceCollision::Box,{-45,60});
	AddPiece(TEXT("SP_Joint_FixedRound"),TEXT("黄色固定圆关节"),{335,370},0,EMenuPieceCollision::Circle,{55,75});
}

void UMenuPlaygroundWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget=Canvas;
	GuideCaption=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MenuComponentGuide"));
	GuideCaption->SetText(FText::FromString(TEXT("试着拖动这些组件")));
	GuideCaption->SetVisibility(ESlateVisibility::HitTestInvisible);
	GuideCaption->SetColorAndOpacity(FSlateColor(FLinearColor(FColor(244, 230, 166))));
	GuideCaption->SetShadowColorAndOpacity(FLinearColor(.02f,.015f,.006f,.9f));
	GuideCaption->SetShadowOffset(FVector2D(1,2));
	FSlateFontInfo Font=GuideCaption->GetFont(); Font.Size=22;
	if (UAssemblyUITheme* Theme=UAssemblyUITheme::LoadDefault()) Font=Theme->ApplyFont(Font);
	GuideCaption->SetFont(Font);
	Canvas->AddChildToCanvas(GuideCaption)->SetAutoSize(true);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UMenuPlaygroundWidget::InitializeMenu(UUserWidget* InMenu)
{
	Menu=InMenu;
	MainPanel=InMenu->GetWidgetFromName(TEXT("Canvas_Items"));
	SelectorPanel=InMenu->GetWidgetFromName(TEXT("WBP_LevelSelector"));
	UWidget* Start=InMenu->GetWidgetFromName(TEXT("Btn_Start"));
	if (!BindMenuAction(Start,this,TEXT("HandleStart")))
	if (UButton* Button=FindButton(Start))
	{
		Button->OnClicked.Clear(); Button->OnClicked.AddDynamic(this,&ThisClass::HandleStart);
	}
	UWidget* Settings=InMenu->GetWidgetFromName(TEXT("Btn_Setting"));
	if (!BindMenuAction(Settings,this,TEXT("HandleSettings")))
	if (UButton* Button=FindButton(Settings))
	{
		Button->OnClicked.Clear(); Button->OnClicked.AddDynamic(this,&ThisClass::HandleSettings);
	}
	if (UUserWidget* Selector=Cast<UUserWidget>(SelectorPanel.Get()))
		if (!BindMenuAction(Selector->GetWidgetFromName(TEXT("WBP_BackButton")),this,TEXT("HandleBack")))
		if (UButton* Button=FindButton(Selector->GetWidgetFromName(TEXT("WBP_BackButton"))))
		{
			Button->OnClicked.Clear(); Button->OnClicked.AddDynamic(this,&ThisClass::HandleBack);
		}
}

UBoxComponent* UMenuPlaygroundWidget::MakeObstacle()
{
	AActor* Actor=GetWorld()->SpawnActor<AActor>(); Actors.Add(Actor);
	UBoxComponent* Body=NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body); Body->RegisterComponent();
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_WorldStatic);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetGenerateOverlapEvents(false);
	return Body;
}

void UMenuPlaygroundWidget::BuildPieces(FVector2D Size)
{
	BuiltViewSize=Size;
	// Fit the whole arrangement below the title and left of the menu buttons. Small
	// windows scale the complete composition uniformly, keeping all authored ratios.
	const FVector2D Anchor(24,Size.Y-20);
	FVector2D Required(1,1);
	for (const FMenuPlaygroundPieceDefinition& Definition : PieceDefinitions)
	{
		const FVector2D Half=SpritePixelSize(Definition.Sprite)*ArtUnitsPerPixel*.5;
		const double Radians=FMath::DegreesToRadians(Definition.AngleDegrees);
		const FVector2D RotatedHalf(FMath::Abs(FMath::Cos(Radians))*Half.X+FMath::Abs(FMath::Sin(Radians))*Half.Y,
			FMath::Abs(FMath::Sin(Radians))*Half.X+FMath::Abs(FMath::Cos(Radians))*Half.Y);
		Required.X=FMath::Max(Required.X,Definition.BottomLeftOffset.X+RotatedHalf.X);
		Required.Y=FMath::Max(Required.Y,Definition.BottomLeftOffset.Y+RotatedHalf.Y);
	}
	FVector2D Available(FMath::Max(1.0,Size.X*.48-Anchor.X),FMath::Max(1.0,Size.Y*.55-20));
	if (Menu.IsValid())
	{
		const FGeometry& Root=TickGeometry;
		if (UWidget* Title=Menu->GetWidgetFromName(TEXT("Img_Title")))
		{
			const FGeometry Geometry=DisplayGeometry(Title);
			if (Geometry.GetLocalSize().Y>0)
				Available.Y=FMath::Max(1.0,Anchor.Y-Root.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize())).Y-24);
		}
		for (const TCHAR* Name : {TEXT("Btn_Start"),TEXT("Btn_Setting"),TEXT("Btn_Quit")})
			if (UWidget* Button=Menu->GetWidgetFromName(Name))
			{
				const FGeometry Geometry=DisplayGeometry(Button);
				if (Geometry.GetLocalSize().X>0)
					Available.X=FMath::Min(Available.X,FMath::Max(1.0,Root.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector)).X-Anchor.X-24));
			}
	}
	const double LayoutScale=FMath::Min(1.0,FMath::Min(Available.X/Required.X,Available.Y/Required.Y));
	for (const FMenuPlaygroundPieceDefinition& Definition : PieceDefinitions)
	{
		const FVector2D PieceSize=SpritePixelSize(Definition.Sprite)*ArtUnitsPerPixel*LayoutScale;
		if (PieceSize.ContainsNaN() || PieceSize.X<=0 || PieceSize.Y<=0) continue;
		const FVector2D Center=Anchor+FVector2D(Definition.BottomLeftOffset.X,-Definition.BottomLeftOffset.Y)*LayoutScale;
		const FVector2D CollisionSize=PieceSize*FVector2D(FMath::Clamp(Definition.CollisionScale.X,.1,1.0),FMath::Clamp(Definition.CollisionScale.Y,.1,1.0));
		AActor* Actor=GetWorld()->SpawnActor<AActor>(); Actors.Add(Actor);
		UPrimitiveComponent* Body;
		if (Definition.Collision==EMenuPieceCollision::Circle)
		{
			USphereComponent* Sphere=NewObject<USphereComponent>(Actor); Sphere->SetSphereRadius(FMath::Min(CollisionSize.X,CollisionSize.Y)*.5); Body=Sphere;
		}
		else { UBoxComponent* Box=NewObject<UBoxComponent>(Actor); Box->SetBoxExtent(FVector(CollisionSize.X*.5,8,CollisionSize.Y*.5)); Body=Box; }
		Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body); Body->RegisterComponent();
		Body->SetWorldLocationAndRotation(ToPhysics(Center),FQuat(FVector::YAxisVector,FMath::DegreesToRadians(Definition.AngleDegrees)));
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetCollisionObjectType(ECC_PhysicsBody); Body->SetCollisionResponseToAllChannels(ECR_Block);
		Body->SetGenerateOverlapEvents(false); Body->SetConstraintMode(EDOFMode::XZPlane);
		Body->SetLinearDamping(.3f); Body->SetAngularDamping(.35f); Body->SetUseCCD(true);
		Body->SetMassOverrideInKg(NAME_None,FMath::Max(.01f,Definition.MassKg),true);
		const int32 Index=Bodies.Num();
		Bodies.Add(Body);
		PieceSizes.Add(PieceSize); PieceScatterImpulses.Add(Definition.ScatterImpulse);
		UMenuPhysicsPieceWidget* Piece=CreateWidget<UMenuPhysicsPieceWidget>(GetOwningPlayer());
		Piece->InitializePiece(this,Index,Definition.Sprite,PieceSize,Definition.Label);
		UCanvasPanelSlot* Layout=Canvas->AddChildToCanvas(Piece);
		Layout->SetSize(PieceSize);
		Pieces.Add(Piece);
	}
	if (Actors.IsEmpty()) { bBuilt=true; return; }
	GrabHandle=NewObject<UPhysicsHandleComponent>(Actors[0]);
	Actors[0]->AddInstanceComponent(GrabHandle); GrabHandle->RegisterComponent();
	GrabHandle->SetLinearStiffness(2500); GrabHandle->SetLinearDamping(180);
	GrabHandle->SetAngularStiffness(1800); GrabHandle->SetAngularDamping(130);
	for (int32 Index=0;Index<7;++Index) Obstacles.Add(MakeObstacle());
	bBuilt=true;
}

void UMenuPlaygroundWidget::ClearPieces()
{
	EndPieceDrag(false);
	// A user widget can be removed and re-added without being garbage-collected.
	// Its old presentation children must leave with their physics bodies.
	if (Canvas)
	{
		Canvas->ClearChildren();
		if (GuideCaption) Canvas->AddChildToCanvas(GuideCaption)->SetAutoSize(true);
	}
	for (AActor* Actor:Actors) if (IsValid(Actor)) Actor->Destroy();
	Actors.Reset(); Bodies.Reset(); Pieces.Reset(); Obstacles.Reset(); PieceSizes.Reset(); PieceScatterImpulses.Reset();
	GrabHandle=nullptr; BuiltViewSize=FVector2D::ZeroVector;
	LastPointer=FVector2D::ZeroVector; ThrowVelocity=FVector::ZeroVector; LastPointerTime=0;
	bBuilt=false; bScattered=false;
}

void UMenuPlaygroundWidget::KeepPiecesInsideView(FVector2D Size)
{
	EndPieceDrag(false);
	TArray<FVector> PreviousPositions, Velocities, AngularVelocities;
	TArray<int32> Order;
	for (int32 Index=0;Index<Bodies.Num();++Index)
	{
		UPrimitiveComponent* Body=Bodies[Index];
		PreviousPositions.Add(Body->GetComponentLocation());
		Velocities.Add(Body->GetPhysicsLinearVelocity());
		AngularVelocities.Add(Body->GetPhysicsAngularVelocityInDegrees());
		Order.Add(Index);
		// Relocate the entire arrangement as one operation, without an intermediate
		// frame of penetrating bodies that would create a large Chaos separation impulse.
		Body->SetSimulatePhysics(false);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Order.Sort([this](int32 A,int32 B) { return PieceSizes[A].X*PieceSizes[A].Y>PieceSizes[B].X*PieceSizes[B].Y; });
	TArray<FBox2D> StaticBounds;
	for (int32 Index=0;Index<FMath::Min(4,Obstacles.Num());++Index)
	{
		if (Obstacles[Index]->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
		const FVector P=Obstacles[Index]->GetComponentLocation(), E=Obstacles[Index]->GetScaledBoxExtent();
		StaticBounds.Emplace(FVector2D(P.X-E.X,-P.Z-E.Z),FVector2D(P.X+E.X,-P.Z+E.Z));
	}
	TArray<FVector2D> Planned; Planned.SetNum(Bodies.Num());
	bool bPlacedAll=false;
	for (int32 Attempt=0;Attempt<12 && !bPlacedAll;++Attempt)
	{
		TArray<FBox2D> Occupied=StaticBounds;
		bPlacedAll=true;
		for (int32 Index:Order)
		{
			const FVector Axis=Bodies[Index]->GetComponentQuat().RotateVector(FVector::XAxisVector);
			const FVector2D Half=PieceSizes[Index]*.5;
			const FVector2D Extent(FMath::Abs(Axis.X)*Half.X+FMath::Abs(Axis.Z)*Half.Y,
				FMath::Abs(Axis.Z)*Half.X+FMath::Abs(Axis.X)*Half.Y);
			const FVector2D Min=Extent+FVector2D(4,10), Max=Size-Min;
			const auto IsFree=[&](FVector2D Center)
			{
				if (Center.X<Min.X || Center.X>Max.X || Center.Y<Min.Y || Center.Y>Max.Y) return false;
				const FBox2D Box(Center-Extent-FVector2D(3,3),Center+Extent+FVector2D(3,3));
				for (const FBox2D& Other:Occupied) if (Box.Intersect(Other)) return false;
				return true;
			};
			FVector2D Point(FMath::Clamp(PreviousPositions[Index].X,Min.X,FMath::Max(Min.X,Max.X)),
				FMath::Clamp(-PreviousPositions[Index].Z,Min.Y,FMath::Max(Min.Y,Max.Y)));
			bool bFound=IsFree(Point);
			const double Step=FMath::Max(4.0,FMath::Min(Extent.X,Extent.Y)*.5);
			for (double Y=Max.Y;Y>=Min.Y && !bFound;Y-=Step)
				for (double X=Min.X;X<=Max.X && !bFound;X+=Step)
					if (IsFree(FVector2D(X,Y))) { Point=FVector2D(X,Y); bFound=true; }
			if (!bFound) { bPlacedAll=false; break; }
			Planned[Index]=Point; Occupied.Emplace(Point-Extent,Point+Extent);
		}
		if (!bPlacedAll)
		{
			// Very small containers uniformly shrink both artwork and real colliders.
			// This keeps all pieces available rather than stacking them into the same wall.
			for (int32 Index=0;Index<Bodies.Num();++Index)
			{
				PieceSizes[Index]*=.8;
				Bodies[Index]->SetWorldScale3D(Bodies[Index]->GetComponentScale()*.8);
				if (UCanvasPanelSlot* Layout=Cast<UCanvasPanelSlot>(Pieces[Index]->Slot)) Layout->SetSize(PieceSizes[Index]);
			}
		}
	}
	if (!bPlacedAll)
	{
		// A pathological container may be covered completely by the title/buttons.
		// Keep an editable, stable decoration rather than resuming penetrating bodies.
		ClearPieces(); BuildPieces(Size); return;
	}
	for (int32 Index=0;Index<Bodies.Num();++Index)
	{
		UPrimitiveComponent* Body=Bodies[Index];
		Body->SetWorldLocation(ToPhysics(Planned[Index]),false,nullptr,ETeleportType::TeleportPhysics);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetSimulatePhysics(true);
		const bool bRelocated=!FVector2D(PreviousPositions[Index].X,-PreviousPositions[Index].Z).Equals(Planned[Index],1);
		Body->SetPhysicsLinearVelocity(bRelocated?FVector::ZeroVector:Velocities[Index].GetClampedToMaxSize(1800));
		Body->SetPhysicsAngularVelocityInDegrees(bRelocated?FVector::ZeroVector:AngularVelocities[Index].GetClampedToMaxSize(360));
	}
	BuiltViewSize=Size;
}

void UMenuPlaygroundWidget::UpdateObstacles()
{
	if (!Menu.IsValid() || Obstacles.Num()!=7) return;
	if (TickGeometry.GetLocalSize().X<=100 || TickGeometry.GetLocalSize().Y<=100) return;
	const TCHAR* Names[]={TEXT("Img_Title"),TEXT("Btn_Start"),TEXT("Btn_Setting"),TEXT("Btn_Quit")};
	const FGeometry& Root=TickGeometry;
	for (int32 Index=0;Index<4;++Index)
	{
		UWidget* Widget=Menu->GetWidgetFromName(Names[Index]);
		const FGeometry Geometry=DisplayGeometry(Widget);
		if (!Widget || !Widget->IsVisible() || Geometry.GetLocalSize().X<=0 || Geometry.GetLocalSize().Y<=0)
		{
			Obstacles[Index]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			continue;
		}
		{
			const FVector2D A=Root.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));
			const FVector2D B=Root.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()));
			const FVector2D Size=(B-A).GetAbs();
			Obstacles[Index]->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Obstacles[Index]->SetBoxExtent(FVector(FMath::Max(1.0,Size.X*.5),32,FMath::Max(1.0,Size.Y*.5)));
			Obstacles[Index]->SetWorldLocation(ToPhysics((A+B)*.5));
		}
	}
	const FVector2D Size=Root.GetLocalSize();
	Obstacles[4]->SetBoxExtent(FVector(Size.X*.5+100,40,12)); Obstacles[4]->SetWorldLocation(ToPhysics(FVector2D(Size.X*.5,Size.Y+6)));
	Obstacles[5]->SetBoxExtent(FVector(12,40,Size.Y+300)); Obstacles[5]->SetWorldLocation(ToPhysics(FVector2D(-12,Size.Y*.5)));
	Obstacles[6]->SetBoxExtent(FVector(12,40,Size.Y+300)); Obstacles[6]->SetWorldLocation(ToPhysics(FVector2D(Size.X+12,Size.Y*.5)));
}

void UMenuPlaygroundWidget::BeginPieceDrag(int32 Index,FVector2D Screen)
{
	if (!Bodies.IsValidIndex(Index) || !GrabHandle) return;
	if (!bScattered)
	{
		for (int32 Part=0;Part<Bodies.Num();++Part)
		{
			Bodies[Part]->SetSimulatePhysics(true);
			const FVector2D Impulse=PieceScatterImpulses[Part];
			Bodies[Part]->AddImpulse(FVector(Impulse.X,0,Impulse.Y),NAME_None,true);
		}
		bScattered=true;
	}
	EndPieceDrag(false);
	DraggedBody=Bodies[Index]; LastPointer=TickGeometry.AbsoluteToLocal(Screen);
	LastPointerTime=FPlatformTime::Seconds(); ThrowVelocity=FVector::ZeroVector;
	GrabHandle->GrabComponentAtLocationWithRotation(DraggedBody,NAME_None,ToPhysics(LastPointer),DraggedBody->GetComponentRotation());
}
void UMenuPlaygroundWidget::MovePieceDrag(FVector2D Screen)
{
	if (!DraggedBody || !GrabHandle) return;
	const FVector2D Pointer=TickGeometry.AbsoluteToLocal(Screen);
	const double Now=FPlatformTime::Seconds();
	const double Delta=FMath::Clamp(Now-LastPointerTime,.005,.1);
	const FVector Velocity=FVector((Pointer.X-LastPointer.X)/Delta,0,-(Pointer.Y-LastPointer.Y)/Delta).GetClampedToMaxSize(1800);
	ThrowVelocity=FMath::Lerp(ThrowVelocity,Velocity,.55);
	LastPointer=Pointer; LastPointerTime=Now;
	GrabHandle->SetTargetLocation(ToPhysics(Pointer));
}
void UMenuPlaygroundWidget::EndPieceDrag(bool bThrow)
{
	if (!DraggedBody) return;
	if (GrabHandle) GrabHandle->ReleaseComponent();
	if (bThrow && FPlatformTime::Seconds()-LastPointerTime<.15)
	{
		DraggedBody->SetPhysicsLinearVelocity(ThrowVelocity);
		DraggedBody->SetPhysicsAngularVelocityInDegrees(FVector(0,ThrowVelocity.X*.08,0));
	}
	DraggedBody=nullptr;
}

TArray<UPrimitiveComponent*> UMenuPlaygroundWidget::GetPieceBodies() const
{
	TArray<UPrimitiveComponent*> Result; for (UPrimitiveComponent* Body:Bodies) Result.Add(Body); return Result;
}
void UMenuPlaygroundWidget::HandleStart() { UJGDInputFeedbackLibrary::PlayInputPress(this); BeginMenuTransition(true); }
void UMenuPlaygroundWidget::HandleBack() { UJGDInputFeedbackLibrary::PlayInputPress(this); BeginMenuTransition(false); }
void UMenuPlaygroundWidget::HandleSettings()
{
	EndPieceDrag(false);
	if (!SettingsWidget)
	{
		SettingsWidget=CreateWidget<UAssemblyPlayingToolbarWidget>(GetOwningPlayer());
		// Build the native tree before TakeWidget caches the viewport's Slate root.
		SettingsWidget->ShowStandaloneSettings();
		SettingsWidget->AddToViewport(100);
		return;
	}
	SettingsWidget->ShowStandaloneSettings();
}
void UMenuPlaygroundWidget::BeginMenuTransition(bool bForward)
{
	if (TransitionStage || !MainPanel.IsValid() || !SelectorPanel.IsValid()) return;
	bToSelector=bForward; TransitionStage=1; TransitionTime=0;
	(bForward?MainPanel:SelectorPanel)->SetIsEnabled(false);
	EndPieceDrag(false);
}
void UMenuPlaygroundWidget::InvokeMenuFunction(FName Name)
{
	if (Menu.IsValid()) if (UFunction* Function=Menu->FindFunction(Name)) Menu->ProcessEvent(Function,nullptr);
}
void UMenuPlaygroundWidget::TickMenuTransition(float DeltaTime)
{
	if (!TransitionStage) return;
	TransitionTime+=DeltaTime;
	const float A=FMath::SmoothStep(0.0f,1.0f,FMath::Clamp(TransitionTime/.22f,0.0f,1.0f));
	UWidget* Outgoing=bToSelector?MainPanel.Get():SelectorPanel.Get();
	UWidget* Incoming=bToSelector?SelectorPanel.Get():MainPanel.Get();
	if (TransitionStage==1)
	{
		Outgoing->SetRenderOpacity(1-A); Outgoing->SetRenderTranslation(FVector2D((bToSelector?-1:1)*A*54,0));
		if (TransitionTime>=.22f)
		{
			InvokeMenuFunction(bToSelector?TEXT("OnStart"):TEXT("OnLevelSelectorClose"));
			// The old Back button hides its selector before broadcasting OnClose.
			// We replace that button action, so normalize both panel visibilities here.
			Outgoing->SetVisibility(ESlateVisibility::Hidden);
			Incoming->SetVisibility(bToSelector ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
			Outgoing->SetRenderOpacity(1); Outgoing->SetRenderTranslation(FVector2D::ZeroVector);
			Incoming->SetRenderOpacity(0); Incoming->SetRenderTranslation(FVector2D(bToSelector?54:-54,0));
			TransitionTime=0; TransitionStage=2;
		}
	}
	else
	{
		Incoming->SetRenderOpacity(A); Incoming->SetRenderTranslation(FVector2D((bToSelector?1:-1)*(1-A)*54,0));
		if (TransitionTime>=.22f) { Incoming->SetIsEnabled(true); Incoming->SetRenderOpacity(1); Incoming->SetRenderTranslation(FVector2D::ZeroVector); TransitionStage=0; }
	}
}

void UMenuPlaygroundWidget::NativeTick(const FGeometry& Geometry,float DeltaTime)
{
	Super::NativeTick(Geometry,DeltaTime);
	TickGeometry=Geometry;
	GuideTime+=DeltaTime;
	if (GuideCaption)
	{
		const bool bShow=!bScattered && TransitionStage==0 && (!SelectorPanel.IsValid() || !SelectorPanel->IsVisible())
			&& !Bodies.IsEmpty() && (!SettingsWidget || !SettingsWidget->IsSettingsOpen());
		GuideCaption->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShow)
		{
			const FVector P=Bodies[0]->GetComponentLocation();
			const double Y=-P.Z-PieceSizes[0].Y*.5-140+FMath::Sin(GuideTime*2.5f)*6;
			CastChecked<UCanvasPanelSlot>(GuideCaption->Slot)->SetPosition(FVector2D(P.X+30,FMath::Max(30.0,Y)));
		}
	}
	const FVector2D Size=Geometry.GetLocalSize();
	if (Size.X>100 && Size.Y>100)
	{
		if (bBuilt && !Size.Equals(BuiltViewSize,.5))
		{
			// Move static boundaries before planning the bodies' non-overlapping new positions.
			UpdateObstacles();
			if (bScattered) KeepPiecesInsideView(Size);
			else ClearPieces();
		}
		if (!bBuilt) BuildPieces(Size);
	}
	if (bBuilt)
	{
		UpdateObstacles();
		for (int32 Index=0;Index<Pieces.Num();++Index)
		{
			if (!IsValid(Bodies[Index]) || !IsValid(Pieces[Index])) continue;
			const FVector Position=Bodies[Index]->GetComponentLocation();
			const FVector Axis=Bodies[Index]->GetComponentQuat().RotateVector(FVector::XAxisVector);
			if (UCanvasPanelSlot* Layout=Cast<UCanvasPanelSlot>(Pieces[Index]->Slot)) Layout->SetPosition(FVector2D(Position.X,-Position.Z)-PieceSizes[Index]*.5);
			Pieces[Index]->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(-Axis.Z,Axis.X)));
		}
	}
}
int32 UMenuPlaygroundWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 LayerId,
	const FWidgetStyle& Style, bool bEnabled) const
{
	const int32 Base=Super::NativePaint(Args,Geometry,Culling,Elements,LayerId,Style,bEnabled);
	if (!GuideCaption || GuideCaption->GetVisibility()!=ESlateVisibility::HitTestInvisible || Bodies.IsEmpty()) return Base;
	const FVector P=Bodies[0]->GetComponentLocation();
	const float Bob=FMath::Sin(GuideTime*2.5f)*6;
	const FVector2f End(P.X,-P.Z-PieceSizes[0].Y*.5-12+Bob);
	const FVector2f Start=End+FVector2f(110,-98);
	const FVector2f Control=End+FVector2f(-15,-110);
	TArray<FVector2f> Points;
	for (int32 I=0;I<=24;++I)
	{
		const float T=I/24.f;
		Points.Add((1-T)*(1-T)*Start+2*(1-T)*T*Control+T*T*End);
	}
	const FVector2f Direction=(End-Control).GetSafeNormal();
	const FVector2f Perp(-Direction.Y,Direction.X);
	TArray<FVector2f> Head {End-Direction*20+Perp*12,End,End-Direction*20-Perp*12};
	for (const TArray<FVector2f>* Line : {&Points,&Head})
	{
		FSlateDrawElement::MakeLines(Elements,Base+1,Geometry.ToPaintGeometry(),*Line,ESlateDrawEffect::None,
			FLinearColor(FColor(91,79,38)),true,9);
		FSlateDrawElement::MakeLines(Elements,Base+2,Geometry.ToPaintGeometry(),*Line,ESlateDrawEffect::None,
			FLinearColor(FColor(244,230,166)),true,4);
	}
	return Base+2;
}
void UMenuPlaygroundWidget::NativeDestruct()
{
	if (SettingsWidget) SettingsWidget->RemoveFromParent();
	SettingsWidget=nullptr;
	ClearPieces();
	Super::NativeDestruct();
}

bool UMenuExperienceSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type==EWorldType::Game || Type==EWorldType::PIE; }
TStatId UMenuExperienceSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMenuExperienceSubsystem,STATGROUP_Tickables); }
void UMenuExperienceSubsystem::Tick(float DeltaTime)
{
	if (Playground.IsValid()) { Playground->AdvanceMenuTransition(DeltaTime); return; }
	if (UGameplayStatics::GetCurrentLevelName(GetWorld(),true)!=TEXT("Menu")) return;
	RetryTime+=DeltaTime; if (RetryTime<.1f) return; RetryTime=0;
	for (TObjectIterator<UUserWidget> It;It;++It)
		if (It->GetWorld()==GetWorld() && It->IsInViewport() && It->GetClass()->GetName()==TEXT("WBP_Menu_C"))
			if (UCanvasPanel* Parent=Cast<UCanvasPanel>(It->GetWidgetFromName(TEXT("Canvas_Items"))))
			{
				APlayerController* PC=It->GetOwningPlayer();
				if (!PC) PC=UGameplayStatics::GetPlayerController(GetWorld(),0);
				if (!PC) return;
				UMenuPlaygroundWidget* Widget=CreateWidget<UMenuPlaygroundWidget>(PC);
				if (!Widget) return;
				Widget->InitializeMenu(*It);
				UCanvasPanelSlot* Layout=Parent->AddChildToCanvas(Widget); Layout->SetAnchors(FAnchors(0,0,1,1)); Layout->SetOffsets(FMargin(0)); Layout->SetZOrder(5);
				Playground=Widget; return;
			}
}
void UMenuExperienceSubsystem::Deinitialize()
{
	if (Playground.IsValid()) Playground->RemoveFromParent();
	Playground.Reset(); Super::Deinitialize();
}
