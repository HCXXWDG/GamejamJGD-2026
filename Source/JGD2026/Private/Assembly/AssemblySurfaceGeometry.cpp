#include "Assembly/AssemblySurfaceGeometry.h"

#include "Assembly/AssemblyJointPartTypes.h"

#define LOCTEXT_NAMESPACE "AssemblySurfaceGeometry"

namespace
{
	using AssemblySurfaceGeometry::FShape;
	using AssemblySurfaceGeometry::FContact;
	bool Finite(FVector2D P) { return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y); }
	double Cross(FVector2D A, FVector2D B) { return A.X * B.Y - A.Y * B.X; }
	double AreaTwice(const TArray<FVector2D>& Points)
	{
		double Sum = 0.0;
		if (Points.IsEmpty()) return Sum;
		const FVector2D Origin = Points[0];
		for (int32 I = 0; I < Points.Num(); ++I) Sum += Cross(Points[I] - Origin, Points[(I + 1) % Points.Num()] - Origin);
		return Sum;
	}
	FVector2D NearestOnSegment(FVector2D P, FVector2D A, FVector2D B)
	{
		const FVector2D Edge = B - A;
		const double Denominator = Edge.SizeSquared();
		return Denominator > UE_DOUBLE_SMALL_NUMBER ? A + Edge * FMath::Clamp(FVector2D::DotProduct(P - A, Edge) / Denominator, 0.0, 1.0) : A;
	}
	bool OnSegment(FVector2D P, FVector2D A, FVector2D B, double Epsilon = AssemblySurfaceGeometry::Tolerance)
	{
		return FVector2D::DistSquared(P, NearestOnSegment(P, A, B)) <= FMath::Square(Epsilon);
	}
	bool IntersectSegments(FVector2D A, FVector2D B, FVector2D C, FVector2D D, FVector2D* OutPoint = nullptr)
	{
		const FVector2D R = B - A, S = D - C;
		const double Denominator = Cross(R, S);
		if (FMath::Abs(Denominator) > UE_DOUBLE_SMALL_NUMBER)
		{
			const double T = Cross(C - A, S) / Denominator, U = Cross(C - A, R) / Denominator;
			if (T >= 0.0 && T <= 1.0 && U >= 0.0 && U <= 1.0)
			{
				if (OutPoint) *OutPoint = A + R * T;
				return true;
			}
			return false;
		}
		for (FVector2D P : { A, B }) if (OnSegment(P, C, D)) { if (OutPoint) *OutPoint = P; return true; }
		for (FVector2D P : { C, D }) if (OnSegment(P, A, B)) { if (OutPoint) *OutPoint = P; return true; }
		return false;
	}
	bool ProperIntersection(FVector2D A, FVector2D B, FVector2D C, FVector2D D)
	{
		const FVector2D R = B - A, S = D - C;
		// Quarter-turn float angles can leave an end face microscopically tilted. Its mathematical
		// crossing may occur at both segment midpoints while every point is within the legal contact
		// tolerance. Require an actual crossing of both physical half-planes before treating it as area.
		const double RLength = R.Size(), SLength = S.Size();
		if (RLength <= AssemblySurfaceGeometry::Tolerance || SLength <= AssemblySurfaceGeometry::Tolerance) return false;
		const double SideC = Cross(C - A, R) / RLength, SideD = Cross(D - A, R) / RLength;
		const double SideA = Cross(A - C, S) / SLength, SideB = Cross(B - C, S) / SLength;
		auto Straddles = [](double X, double Y) { return (X > AssemblySurfaceGeometry::Tolerance && Y < -AssemblySurfaceGeometry::Tolerance) || (Y > AssemblySurfaceGeometry::Tolerance && X < -AssemblySurfaceGeometry::Tolerance); };
		if (!Straddles(SideC, SideD) || !Straddles(SideA, SideB)) return false;
		const double Denominator = Cross(R, S);
		if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER) return false;
		const double T = Cross(C - A, S) / Denominator, U = Cross(C - A, R) / Denominator;
		const double TEpsilon = AssemblySurfaceGeometry::Tolerance / FMath::Max(R.Size(), AssemblySurfaceGeometry::Tolerance);
		const double UEpsilon = AssemblySurfaceGeometry::Tolerance / FMath::Max(S.Size(), AssemblySurfaceGeometry::Tolerance);
		return T > TEpsilon && T < 1.0 - TEpsilon && U > UEpsilon && U < 1.0 - UEpsilon;
	}
	bool PolygonContains(const TArray<FVector2D>& Points, FVector2D P, bool bIncludeBoundary)
	{
		if (!Finite(P) || Points.Num() < 3) return false;
		bool bInside = false;
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			const FVector2D A = Points[I], B = Points[(I + 1) % Points.Num()];
			if (OnSegment(P, A, B)) return bIncludeBoundary;
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X) bInside = !bInside;
		}
		return bInside;
	}
	FVector2D OutwardNormal(const TArray<FVector2D>& Points, int32 Edge)
	{
		const FVector2D Delta = Points[(Edge + 1) % Points.Num()] - Points[Edge];
		const FVector2D N(Delta.Y, -Delta.X);
		return N.GetSafeNormal() * (AreaTwice(Points) >= 0.0 ? 1.0 : -1.0);
	}
	void AddContact(TArray<FContact>& Contacts, FVector2D A, FVector2D B)
	{
		if (!Finite(A) || !Finite(B)) return;
		const double DistanceSquared = FVector2D::DistSquared(A, B);
		if (!FMath::IsFinite(DistanceSquared)) return;
		Contacts.Add({ A, B, DistanceSquared });
	}
	bool ContactBefore(const FContact& A, const FContact& B)
	{
		if (A.DistanceSquared != B.DistanceSquared) return A.DistanceSquared < B.DistanceSquared;
		if (A.PointA.X != B.PointA.X) return A.PointA.X < B.PointA.X;
		if (A.PointA.Y != B.PointA.Y) return A.PointA.Y < B.PointA.Y;
		if (A.PointB.X != B.PointB.X) return A.PointB.X < B.PointB.X;
		return A.PointB.Y < B.PointB.Y;
	}
	void SortContacts(TArray<FContact>& Contacts)
	{
		Contacts.Sort([](const FContact& A, const FContact& B) { return ContactBefore(A, B); });
	}
	TArray<FVector2D> StandardOutline(const FAssemblyBoneDefinition& D, const FNormalBonePhysicalStats& Stats)
	{
		TArray<FVector2D> Points;
		if (D.PartKind == EAssemblyRigidPartKind::Bone)
		{
			Points = { { -.5, -.5 }, { .5, -.5 }, { .5, .5 }, { -.5, .5 } };
		}
		else switch (D.JointShape)
		{
		case EAssemblyJointPartShape::Bar:
			Points = { { -.5, -.5 }, { .5, -.5 }, { .5, .5 }, { -.5, .5 } }; break;
		case EAssemblyJointPartShape::Corner:
			Points = { { -.5, 0 }, { 0, 0 }, { 0, -.5 }, { .5, -.5 }, { .5, .5 }, { -.5, .5 } }; break;
		case EAssemblyJointPartShape::Tee:
			Points = { { -.5, -.5 }, { .5, -.5 }, { .5, 0 }, { 1.0 / 6.0, 0 }, { 1.0 / 6.0, .5 },
				{ -1.0 / 6.0, .5 }, { -1.0 / 6.0, 0 }, { -.5, 0 } }; break;
		case EAssemblyJointPartShape::Cross:
			Points = { { -1.0 / 6.0, -.5 }, { 1.0 / 6.0, -.5 }, { 1.0 / 6.0, -1.0 / 6.0 }, { .5, -1.0 / 6.0 },
				{ .5, 1.0 / 6.0 }, { 1.0 / 6.0, 1.0 / 6.0 }, { 1.0 / 6.0, .5 }, { -1.0 / 6.0, .5 },
				{ -1.0 / 6.0, 1.0 / 6.0 }, { -.5, 1.0 / 6.0 }, { -.5, -1.0 / 6.0 }, { -1.0 / 6.0, -1.0 / 6.0 } }; break;
		case EAssemblyJointPartShape::Fork:
			Points = { { -.5, -.125 }, { -.125, -.125 }, { .5, -.5 }, { .5, -.125 }, { .125, 0 },
				{ .5, .125 }, { .5, .5 }, { -.125, .125 }, { -.5, .125 } }; break;
		default: break;
		}
		for (FVector2D& P : Points) P *= FVector2D(Stats.Length, Stats.Thickness);
		return Points;
	}
	bool Usable(const FShape& Shape)
	{
		if (!Finite(Shape.Center)) return false;
		if (Shape.bCircle) return FMath::IsFinite(Shape.Radius) && Shape.Radius > 0.0;
		if (Shape.Vertices.Num() < 3) return false;
		for (const FVector2D P : Shape.Vertices) if (!Finite(P)) return false;
		const double Area = AreaTwice(Shape.Vertices);
		return FMath::IsFinite(Area) && FMath::Abs(Area) > AssemblySurfaceGeometry::Tolerance * AssemblySurfaceGeometry::Tolerance;
	}
	bool CirclePolygonOverlap(const FShape& Circle, const FShape& Polygon)
	{
		if (PolygonContains(Polygon.Vertices, Circle.Center, true)) return true;
		const double RadiusSquared = FMath::Square(FMath::Max(0.0, Circle.Radius - AssemblySurfaceGeometry::Tolerance));
		for (int32 I = 0; I < Polygon.Vertices.Num(); ++I)
		{
			if (FVector2D::DistSquared(Circle.Center, NearestOnSegment(Circle.Center, Polygon.Vertices[I], Polygon.Vertices[(I + 1) % Polygon.Vertices.Num()])) < RadiusSquared) return true;
		}
		return false;
	}
	void CirclePolygonContacts(const FShape& Circle, const FShape& Polygon, bool bCircleMoving, TArray<FContact>& Contacts)
	{
		for (int32 I = 0; I < Polygon.Vertices.Num(); ++I)
		{
			const FVector2D A = Polygon.Vertices[I], B = Polygon.Vertices[(I + 1) % Polygon.Vertices.Num()];
			const FVector2D P = NearestOnSegment(Circle.Center, A, B);
			const FVector2D Normal = OutwardNormal(Polygon.Vertices, I);
			FVector2D Radial = (P - Circle.Center).GetSafeNormal();
			if (Radial.IsNearlyZero()) Radial = Normal;
			// The radial point is exact near vertices. Both sides/normals also offer legal exits from penetration.
			for (FVector2D Direction : { Radial, -Radial, Normal, -Normal })
			{
				const FVector2D C = Circle.Center + Direction * Circle.Radius;
				if (bCircleMoving) AddContact(Contacts, C, P); else AddContact(Contacts, P, C);
			}
		}
	}
	void AddSupportAxisContacts(const FShape& Moving, const FShape& Target, TArray<FContact>& Contacts)
	{
		// Vertex-to-edge contacts already resolve most penetrations. Support-axis exits also cover
		// coincident faces and degenerate projection choices without filling a concave contour's voids.
		for (const FShape* Axes : { &Moving, &Target })
		{
			for (int32 I = 0; I < Axes->Vertices.Num(); ++I)
			{
				const FVector2D Axis = OutwardNormal(Axes->Vertices, I);
				double AMin = TNumericLimits<double>::Max(), AMax = TNumericLimits<double>::Lowest();
				double BMin = TNumericLimits<double>::Max(), BMax = TNumericLimits<double>::Lowest();
				for (const FVector2D P : Moving.Vertices)
				{
					const double Projection = FVector2D::DotProduct(P, Axis);
					AMin = FMath::Min(AMin, Projection); AMax = FMath::Max(AMax, Projection);
				}
				for (const FVector2D P : Target.Vertices)
				{
					const double Projection = FVector2D::DotProduct(P, Axis);
					BMin = FMath::Min(BMin, Projection); BMax = FMath::Max(BMax, Projection);
				}
				for (const double Amount : { BMax - AMin, BMin - AMax })
				{
					const FVector2D Delta = Axis * Amount;
					if (!Finite(Delta)) continue;
					FShape Moved = Moving;
					Moved.Center += Delta;
					for (FVector2D& P : Moved.Vertices) P += Delta;
					FContact Contact;
					if (!AssemblySurfaceGeometry::Overlaps(Moved, Target)
						&& AssemblySurfaceGeometry::ClosestContact(Moved, Target, Contact)
						&& Contact.DistanceSquared <= AssemblySurfaceGeometry::Tolerance * AssemblySurfaceGeometry::Tolerance * 4.0)
					{
						AddContact(Contacts, Contact.PointA - Delta, Contact.PointB);
					}
				}
			}
		}
	}
}

bool AssemblySurfaceGeometry::ValidateOutline(const TArray<FVector2D>& Points, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (Points.Num() < 3 || Points.Num() > 128)
	{
		OutReason = LOCTEXT("VertexCount", "表面轮廓需要3至128个顶点，末点不重复首点。"); return false;
	}
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		if (!Finite(Points[I]) || FVector2D::DistSquared(Points[I], Points[(I + 1) % Points.Num()]) <= Tolerance * Tolerance)
		{
			OutReason = LOCTEXT("InvalidVertex", "表面轮廓顶点必须有限，相邻顶点不能重合。"); return false;
		}
		for (int32 J = I + 1; J < Points.Num(); ++J)
		{
			if (J == I + 1 || (I == 0 && J == Points.Num() - 1)) continue;
			if (IntersectSegments(Points[I], Points[(I + 1) % Points.Num()], Points[J], Points[(J + 1) % Points.Num()]))
			{
				OutReason = LOCTEXT("SelfIntersection", "表面轮廓必须是没有自交和重复边的简单闭合多边形。"); return false;
			}
		}
	}
	const double Area = AreaTwice(Points);
	if (!FMath::IsFinite(Area) || FMath::Abs(Area) <= Tolerance * Tolerance)
	{
		OutReason = LOCTEXT("ZeroArea", "表面轮廓面积必须是有效正数。"); return false;
	}
	return true;
}

bool AssemblySurfaceGeometry::BuildShape(const FAssemblyBoneDefinition& D, const FTransform& Pose, FShape& OutShape, FText& OutReason)
{
	OutShape = FShape();
	OutReason = FText::GetEmpty();
	FNormalBonePhysicalStats Stats;
	if (!AssemblyRigidPartMetrics::Evaluate(D, Stats, OutReason)) return false;
	const FVector Scale = Pose.GetScale3D();
	const FVector XAxis = Pose.GetRotation().RotateVector(FVector::XAxisVector);
	const FVector ZAxis = Pose.GetRotation().RotateVector(FVector::ZAxisVector);
	if (Pose.ContainsNaN() || !Pose.GetRotation().IsNormalized() || Scale.X <= 0.0 || Scale.Z <= 0.0
		|| FMath::Abs(XAxis.Y) > UE_KINDA_SMALL_NUMBER || FMath::Abs(ZAxis.Y) > UE_KINDA_SMALL_NUMBER)
	{
		OutReason = LOCTEXT("InvalidPlanarTransform", "表面变换必须有限，保持当前XZ游戏平面，并使用正的尺寸缩放。"); return false;
	}
	OutShape.Center = FVector2D(Pose.GetLocation().X, Pose.GetLocation().Z);
	if (D.SurfaceOutline.IsEmpty() && D.PartKind == EAssemblyRigidPartKind::JointPart
		&& (D.JointShape == EAssemblyJointPartShape::Round || D.JointShape == EAssemblyJointPartShape::FixedRound))
	{
		if (!FMath::IsNearlyEqual(Scale.X, Scale.Z, UE_KINDA_SMALL_NUMBER))
		{
			OutReason = LOCTEXT("CircleScale", "圆形表面需要统一缩放，不能变成椭圆。"); return false;
		}
		OutShape.bCircle = true;
		OutShape.Radius = Stats.Length * .5 * Scale.X;
		return FMath::IsFinite(OutShape.Radius) && OutShape.Radius > 0.0;
	}
	OutShape.Vertices = D.SurfaceOutline.IsEmpty() ? StandardOutline(D, Stats) : D.SurfaceOutline;
	if (!ValidateOutline(OutShape.Vertices, OutReason)) { OutShape = FShape(); return false; }
	for (FVector2D& P : OutShape.Vertices)
	{
		const FVector Transformed = Pose.TransformPosition(FVector(P.X, 0.0, P.Y));
		P = FVector2D(Transformed.X, Transformed.Z);
		if (!Finite(P)) { OutShape = FShape(); OutReason = LOCTEXT("TransformOverflow", "表面变换超出有效范围。"); return false; }
	}
	return true;
}

AssemblySurfaceGeometry::FShape AssemblySurfaceGeometry::MakeCircle(FVector2D Center, double Radius)
{
	FShape Shape;
	Shape.bCircle = true; Shape.Center = Center; Shape.Radius = Radius;
	return Shape;
}

bool AssemblySurfaceGeometry::ContainsPoint(const FShape& Shape, FVector2D Point)
{
	if (!Usable(Shape) || !Finite(Point)) return false;
	return Shape.bCircle ? FVector2D::DistSquared(Point, Shape.Center) <= FMath::Square(Shape.Radius + Tolerance)
		: PolygonContains(Shape.Vertices, Point, true);
}

bool AssemblySurfaceGeometry::IsOnBoundary(const FShape& Shape, FVector2D Point)
{
	if (!Usable(Shape) || !Finite(Point)) return false;
	if (Shape.bCircle)
	{
		const double Distance = FVector2D::Distance(Point, Shape.Center);
		return FMath::IsFinite(Distance) && FMath::Abs(Distance - Shape.Radius) <= Tolerance;
	}
	for (int32 I = 0; I < Shape.Vertices.Num(); ++I)
	{
		if (OnSegment(Point, Shape.Vertices[I], Shape.Vertices[(I + 1) % Shape.Vertices.Num()])) return true;
	}
	return false;
}

bool AssemblySurfaceGeometry::Overlaps(const FShape& A, const FShape& B)
{
	if (!Usable(A) || !Usable(B)) return true; // invalid geometry must never create a legal contact.
	if (A.bCircle && B.bCircle) return FVector2D::DistSquared(A.Center, B.Center) < FMath::Square(FMath::Max(0.0, A.Radius + B.Radius - Tolerance));
	if (A.bCircle) return CirclePolygonOverlap(A, B);
	if (B.bCircle) return CirclePolygonOverlap(B, A);
	for (int32 I = 0; I < A.Vertices.Num(); ++I)
	{
		const FVector2D A0 = A.Vertices[I], A1 = A.Vertices[(I + 1) % A.Vertices.Num()];
		if (PolygonContains(B.Vertices, A0, false) || PolygonContains(B.Vertices, (A0 + A1) * .5, false)) return true;
		for (int32 J = 0; J < B.Vertices.Num(); ++J)
		{
			if (ProperIntersection(A0, A1, B.Vertices[J], B.Vertices[(J + 1) % B.Vertices.Num()])) return true;
		}
	}
	for (int32 I = 0; I < B.Vertices.Num(); ++I)
	{
		if (PolygonContains(A.Vertices, B.Vertices[I], false) || PolygonContains(A.Vertices, (B.Vertices[I] + B.Vertices[(I + 1) % B.Vertices.Num()]) * .5, false)) return true;
	}
	// Coincident edges alone are ambiguous. Sample the interior side to distinguish shared area from touching.
	for (const FShape* Shape : { &A, &B })
	{
		const FShape& Other = Shape == &A ? B : A;
		for (int32 I = 0; I < Shape->Vertices.Num(); ++I)
		{
			const FVector2D Mid = (Shape->Vertices[I] + Shape->Vertices[(I + 1) % Shape->Vertices.Num()]) * .5;
			const FVector2D Inside = Mid - OutwardNormal(Shape->Vertices, I) * (Tolerance * 4.0);
			if (PolygonContains(Shape->Vertices, Inside, false) && PolygonContains(Other.Vertices, Inside, false)) return true;
		}
	}
	return false;
}

bool AssemblySurfaceGeometry::ClosestContact(const FShape& A, const FShape& B, FContact& OutContact)
{
	OutContact = FContact();
	if (!Usable(A) || !Usable(B)) return false;
	if (A.bCircle && B.bCircle)
	{
		FVector2D Direction = (B.Center - A.Center).GetSafeNormal();
		if (Direction.IsNearlyZero()) Direction = FVector2D(1.0, 0.0);
		OutContact.PointA = A.Center + Direction * A.Radius;
		OutContact.PointB = B.Center - Direction * B.Radius;
		OutContact.DistanceSquared = FVector2D::DistSquared(OutContact.PointA, OutContact.PointB);
		return FMath::IsFinite(OutContact.DistanceSquared);
	}
	if (A.bCircle || B.bCircle)
	{
		const FShape& Circle = A.bCircle ? A : B;
		const FShape& Polygon = A.bCircle ? B : A;
		double Best = TNumericLimits<double>::Max();
		FVector2D Point = FVector2D::ZeroVector, Normal(1.0, 0.0);
		bool bFound = false;
		for (int32 I = 0; I < Polygon.Vertices.Num(); ++I)
		{
			const FVector2D P = NearestOnSegment(Circle.Center, Polygon.Vertices[I], Polygon.Vertices[(I + 1) % Polygon.Vertices.Num()]);
			const double Distance = FVector2D::DistSquared(P, Circle.Center);
			if (FMath::IsFinite(Distance) && (!bFound || Distance < Best)) { Best = Distance; Point = P; Normal = OutwardNormal(Polygon.Vertices, I); bFound = true; }
		}
		if (!bFound) return false;
		FVector2D Direction = (Point - Circle.Center).GetSafeNormal();
		if (Direction.IsNearlyZero()) Direction = Normal;
		const FVector2D CirclePoint = Circle.Center + Direction * Circle.Radius;
		OutContact.PointA = A.bCircle ? CirclePoint : Point;
		OutContact.PointB = A.bCircle ? Point : CirclePoint;
		OutContact.DistanceSquared = FVector2D::DistSquared(OutContact.PointA, OutContact.PointB);
		return FMath::IsFinite(OutContact.DistanceSquared);
	}
	bool bFound = false;
	auto Consider = [&](FVector2D PointA, FVector2D PointB)
	{
		if (!Finite(PointA) || !Finite(PointB)) return;
		const double Distance = FVector2D::DistSquared(PointA, PointB);
		if (!FMath::IsFinite(Distance)) return;
		const FContact Candidate{ PointA, PointB, Distance };
		if (!bFound || ContactBefore(Candidate, OutContact)) { OutContact = Candidate; bFound = true; }
	};
	for (int32 I = 0; I < A.Vertices.Num(); ++I)
	{
		const FVector2D A0 = A.Vertices[I], A1 = A.Vertices[(I + 1) % A.Vertices.Num()];
		for (int32 J = 0; J < B.Vertices.Num(); ++J)
		{
			const FVector2D B0 = B.Vertices[J], B1 = B.Vertices[(J + 1) % B.Vertices.Num()];
			FVector2D Intersection;
			if (IntersectSegments(A0, A1, B0, B1, &Intersection)) Consider(Intersection, Intersection);
			Consider(A0, NearestOnSegment(A0, B0, B1));
			Consider(NearestOnSegment(B0, A0, A1), B0);
		}
	}
	return bFound;
}

void AssemblySurfaceGeometry::GetContactCandidates(const FShape& Moving, const FShape& Target, TArray<FContact>& OutContacts)
{
	OutContacts.Reset();
	if (!Usable(Moving) || !Usable(Target)) return;
	FContact Nearest;
	if (ClosestContact(Moving, Target, Nearest)) AddContact(OutContacts, Nearest.PointA, Nearest.PointB);
	if (Moving.bCircle && Target.bCircle)
	{
		FVector2D Direction = (Moving.Center - Target.Center).GetSafeNormal();
		if (Direction.IsNearlyZero()) Direction = FVector2D(1.0, 0.0);
		for (FVector2D D : { Direction, -Direction, FVector2D(1.0, 0.0), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(0.0, -1.0) })
		{
			AddContact(OutContacts, Moving.Center - D * Moving.Radius, Target.Center + D * Target.Radius);
		}
	}
	else if (Moving.bCircle) CirclePolygonContacts(Moving, Target, true, OutContacts);
	else if (Target.bCircle) CirclePolygonContacts(Target, Moving, false, OutContacts);
	else
	{
		for (int32 I = 0; I < Moving.Vertices.Num(); ++I)
		{
			const FVector2D A0 = Moving.Vertices[I], A1 = Moving.Vertices[(I + 1) % Moving.Vertices.Num()];
			for (int32 J = 0; J < Target.Vertices.Num(); ++J)
			{
				const FVector2D B0 = Target.Vertices[J], B1 = Target.Vertices[(J + 1) % Target.Vertices.Num()];
				AddContact(OutContacts, A0, NearestOnSegment(A0, B0, B1));
				AddContact(OutContacts, NearestOnSegment(B0, A0, A1), B0);
			}
		}
		AddSupportAxisContacts(Moving, Target, OutContacts);
	}
	SortContacts(OutContacts);
}

#undef LOCTEXT_NAMESPACE
