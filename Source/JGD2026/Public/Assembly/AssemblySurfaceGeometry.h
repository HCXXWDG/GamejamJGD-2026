#pragma once

#include "Assembly/AssemblyTypes.h"

/** Shared deterministic contours for assembly and physics providers. No texture/editor service is needed.
 * BuildShape with Identity returns body-local units; any supplied planar Pose transforms the contour into
 * that pose's coordinate space. Physics may extrude/decompose these contours when creating real bodies.
 */
namespace AssemblySurfaceGeometry
{
	constexpr double Tolerance = 0.001;
	struct FShape
	{
		bool bCircle = false;
		double Radius = 0.0;
		FVector2D Center = FVector2D::ZeroVector;
		TArray<FVector2D> Vertices;
	};
	struct FContact
	{
		FVector2D PointA = FVector2D::ZeroVector;
		FVector2D PointB = FVector2D::ZeroVector;
		double DistanceSquared = 0.0;
	};
	JGD2026_API bool ValidateOutline(const TArray<FVector2D>& Outline, FText& OutReason);
	JGD2026_API bool BuildShape(const FAssemblyBoneDefinition& Definition, const FTransform& Pose, FShape& OutShape, FText& OutReason);
	JGD2026_API FShape MakeCircle(FVector2D Center, double Radius);
	JGD2026_API bool ContainsPoint(const FShape& Shape, FVector2D Point);
	// Attachment anchors must lie on the boundary, not merely somewhere inside the body.
	JGD2026_API bool IsOnBoundary(const FShape& Shape, FVector2D Point);
	// Touching boundaries are legal. Only a shared interior counts as overlap.
	JGD2026_API bool Overlaps(const FShape& A, const FShape& B);
	JGD2026_API bool ClosestContact(const FShape& A, const FShape& B, FContact& OutContact);
	// PointA belongs to the original moving contour. PointB belongs to the target.
	// Translating by PointB-PointA creates a contact; the caller must reject other intersections.
	JGD2026_API void GetContactCandidates(const FShape& Moving, const FShape& Target, TArray<FContact>& OutContacts);
}
