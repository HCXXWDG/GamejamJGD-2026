#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "UObject/StrongObjectPtr.h"

// Runtime-only sprite triangles. A rectangular atlas brush cannot preserve a sprite's
// custom cutouts: those rectangles may contain unrelated artwork from the same atlas.
namespace AssemblySpriteGeometry
{
	struct FVertex
	{
		FVector2f Position = FVector2f::ZeroVector;
		FVector2f UV = FVector2f::ZeroVector;
	};

	struct FMesh
	{
		TArray<FVertex> Vertices;
		FVector2f Size = FVector2f::ZeroVector;
	};

	inline bool Build(TConstArrayView<FVector4> BakedVertices, FMesh& OutMesh, FString* OutError = nullptr)
	{
		OutMesh = FMesh();
		if (OutError) OutError->Reset();
		const auto Reject = [OutError](FString Reason)
		{
			if (OutError) *OutError = MoveTemp(Reason);
			return false;
		};
		// SlateIndex is 16 bit on some supported targets. Never truncate an index.
		if (BakedVertices.IsEmpty() || BakedVertices.Num() % 3 != 0 || BakedVertices.Num() > 65535)
			return Reject(FString::Printf(TEXT("Invalid baked vertex count: %d."), BakedVertices.Num()));
		// Paper2D generates UVs by multiplying double pixel positions by a float inverse
		// texture dimension. A right/bottom edge can therefore be slightly greater than 1.
		constexpr double UVTolerance = 0.000001;
		FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
		bool bHasArea = false;
		for (int32 Index = 0; Index < BakedVertices.Num(); ++Index)
		{
			const FVector4& Vertex = BakedVertices[Index];
			if (Vertex.ContainsNaN() || !FMath::IsFinite(Vertex.X) || !FMath::IsFinite(Vertex.Y)
				|| !FMath::IsFinite(Vertex.Z) || !FMath::IsFinite(Vertex.W)
				|| Vertex.Z < -UVTolerance || Vertex.Z > 1.0 + UVTolerance
				|| Vertex.W < -UVTolerance || Vertex.W > 1.0 + UVTolerance
				|| FMath::Abs(Vertex.X) > TNumericLimits<float>::Max() * 0.25
				|| FMath::Abs(Vertex.Y) > TNumericLimits<float>::Max() * 0.25)
				return Reject(FString::Printf(TEXT("Invalid vertex %d: XY=(%.17g,%.17g), UV=(%.17g,%.17g)."),
					Index, Vertex.X, Vertex.Y, Vertex.Z, Vertex.W));
			// PaperSprite XY describes world X/Z; Slate's second axis points down.
			const FVector2D Point(Vertex.X, -Vertex.Y);
			Min.X = FMath::Min(Min.X, Point.X);
			Min.Y = FMath::Min(Min.Y, Point.Y);
			Max.X = FMath::Max(Max.X, Point.X);
			Max.Y = FMath::Max(Max.Y, Point.Y);
			if (Index % 3 == 2)
			{
				const FVector4& A = BakedVertices[Index - 2];
				const FVector4& B = BakedVertices[Index - 1];
				bHasArea |= FMath::Abs((B.X - A.X) * (Vertex.Y - A.Y) - (B.Y - A.Y) * (Vertex.X - A.X)) > UE_DOUBLE_SMALL_NUMBER;
			}
		}
		const FVector2D Size = Max - Min;
		if (!bHasArea || Size.X <= UE_DOUBLE_SMALL_NUMBER || Size.Y <= UE_DOUBLE_SMALL_NUMBER)
			return Reject(FString::Printf(TEXT("Baked triangles have no area or dimensions: %.17g x %.17g."), Size.X, Size.Y));
		OutMesh.Size = FVector2f(Size);
		OutMesh.Vertices.Reserve(BakedVertices.Num());
		for (const FVector4& Vertex : BakedVertices)
		{
			OutMesh.Vertices.Add({ FVector2f(FVector2D(Vertex.X, -Vertex.Y) - Min),
				FVector2f(FMath::Clamp(Vertex.Z, 0.0, 1.0), FMath::Clamp(Vertex.W, 0.0, 1.0)) });
		}
		return true;
	}

	inline bool Fit(const FMesh& Mesh, FVector2f AvailableSize, FVector2f& OutOffset, float& OutScale)
	{
		OutOffset = FVector2f::ZeroVector;
		OutScale = 0.0f;
		if (Mesh.Vertices.IsEmpty() || AvailableSize.ContainsNaN() || Mesh.Size.ContainsNaN()
			|| !FMath::IsFinite(AvailableSize.X) || !FMath::IsFinite(AvailableSize.Y)
			|| AvailableSize.X <= 0.0f || AvailableSize.Y <= 0.0f || Mesh.Size.X <= 0.0f || Mesh.Size.Y <= 0.0f) return false;
		OutScale = FMath::Min(AvailableSize.X / Mesh.Size.X, AvailableSize.Y / Mesh.Size.Y);
		if (!FMath::IsFinite(OutScale) || OutScale <= 0.0f) return false;
		OutOffset = (AvailableSize - Mesh.Size * OutScale) * 0.5f;
		return !OutOffset.ContainsNaN();
	}

	struct FRenderData
	{
		FMesh Mesh;
		// Slate can outlive its UWidget. Retain the cooked texture for that entire lifetime,
		// rather than relying on a raw FSlateBrush resource pointer or an editor cache.
		TStrongObjectPtr<UTexture2D> Texture;
		FString LastError;

		bool SetSprite(const UPaperSprite* Sprite)
		{
			Mesh = FMesh();
			Texture.Reset();
			LastError.Reset();
			if (!IsValid(Sprite)) { LastError = TEXT("No valid sprite assigned."); return false; }
			// This is Paper2D's own draw-call texture, matching BakedRenderData UVs. The
			// Slate interface is a compatible fallback and never requires editor-only fields.
			UTexture2D* Source = Sprite->GetBakedTexture();
			if (!IsValid(Source)) Source = Cast<UTexture2D>(Sprite->GetSlateAtlasData().AtlasTexture);
			FString GeometryError;
			if (!IsValid(Source) || !Build(Sprite->BakedRenderData, Mesh, &GeometryError))
			{
				LastError = FString::Printf(TEXT("Sprite=%s, Texture=%s, Vertices=%d: %s"), *Sprite->GetPathName(),
					*GetPathNameSafe(Source), Sprite->BakedRenderData.Num(), IsValid(Source) ? *GeometryError : TEXT("No cooked/source texture available."));
				return false;
			}
			Texture.Reset(Source);
			return true;
		}

		bool IsRenderable() const { return Texture.IsValid() && !Mesh.Vertices.IsEmpty(); }
	};
}
