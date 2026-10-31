#include "EnginePCH.h"
#include "Frustum.h"

namespace
{
    bool IsSeparatedOnAxis(const FVector& Axis, const FOrientedBox& OBB, const FFrustumCorners& Corners)
    {
        const float AxisLengthSquared = Axis.SizeSquared();

        if (!std::isfinite(AxisLengthSquared) ||  AxisLengthSquared <= 1.0e-8f)
        {
            return false;
        }

        const FVector NormalizedAxis = Axis / std::sqrt(AxisLengthSquared);

        const float CenterProjection = OBB.Center.Dot(NormalizedAxis);

        float OBBDistance = 0.0f;

        for (int32 Index = 0; Index < 3; ++Index)
        {
            OBBDistance +=  OBB.Extent[Index] * std::fabs(OBB.Axes[Index].Dot(NormalizedAxis));
        }

        const float OBBMin = CenterProjection - OBBDistance;
        const float OBBMax = CenterProjection + OBBDistance;

        float FrustumMin = std::numeric_limits<float>::max();
        float FrustumMax = std::numeric_limits<float>::lowest();

        for (const FVector& Corner : Corners.Points)
        {
            const float Projection = Corner.Dot(NormalizedAxis);

            FrustumMin = std::min(FrustumMin, Projection);
            FrustumMax = std::max(FrustumMax, Projection);
        }

        constexpr float Tolerance = 1.0e-4f;

        return OBBMax < FrustumMin - Tolerance || FrustumMax < OBBMin - Tolerance;
    }
}

static FPlane NormalizePlane(const FPlane Plane)
{
    const FVector& N = Plane.Normal;
    const float Length = sqrtf(N.X * N.X + N.Y * N.Y + N.Z * N.Z);
    assert(Length > 1.0e-6f);

    const float InvLength = 1.0f / Length;
    return {
        { N.X * InvLength, N.Y * InvLength, N.Z * InvLength },
        Plane.Distance * InvLength
    };
}

FFrustumPlanes ExtractFrustumPlanes(const FMatrix& Matrix)
{
    auto MakePlane = [&Matrix](const int ColumnA, const float ScaleA, const int ColumnB, const float ScaleB)
        {
            return NormalizePlane({
                { ScaleA * Matrix.M[0][ColumnA] + ScaleB * Matrix.M[0][ColumnB], ScaleA * Matrix.M[1][ColumnA] + ScaleB * Matrix.M[1][ColumnB], ScaleA * Matrix.M[2][ColumnA] + ScaleB * Matrix.M[2][ColumnB] },
                ScaleA * Matrix.M[3][ColumnA] + ScaleB * Matrix.M[3][ColumnB] });
        };
    FFrustumPlanes Result{};
    Result.Planes[0] = MakePlane(3, 1.0f, 0, 1.0f);
    Result.Planes[1] = MakePlane(3, 1.0f, 0, -1.0f);
    Result.Planes[2] = MakePlane(3, 1.0f, 1, 1.0f);
    Result.Planes[3] = MakePlane(3, 1.0f, 1, -1.0f);
    Result.Planes[4] = NormalizePlane({ { Matrix.M[0][2], Matrix.M[1][2], Matrix.M[2][2] }, Matrix.M[3][2] });
    Result.Planes[5] = MakePlane(3, 1.0f, 2, -1.0f);
    return Result;
}

bool IsAABBInFrustum(const FAABB& Bounds, const FFrustumPlanes& Frustum)
{
    FVectorRegister Boundreg = VectorSIMD::LoadFloat3(&Bounds.Extent.X);
    FVectorRegister Centerreg = VectorSIMD::LoadFloat3(&Bounds.Center.X);
    for (const FPlane& Plane : Frustum.Planes)
    {
        FVectorRegister Planereg = VectorSIMD::LoadFloat3(&Plane.Normal.X);
        FVectorRegister absPlanereg = VectorSIMD::Abs(Planereg);

        const float Radius = VectorSIMD::Dot(absPlanereg, Boundreg);
        const float CenterDist = VectorSIMD::Dot(Planereg, Centerreg);

        if (CenterDist + Plane.Distance + Radius < 0.0f)
        {
            return false;
        }
    }
    return true;
}


FAABB MakeWorldBounds(const FBox& Value)
{
    const FVector Center = (Value.Min + Value.Max) * 0.5f;
    const FVector Extent = (Value.Max - Value.Min) * 0.5f;
    return { Center, Extent };
}

bool ExtractFrustumCorners(const FMatrix& ViewProjection, FFrustumCorners& OutCorners)
{
    const float Determinant = ViewProjection.Determinant();
    if (!std::isfinite(Determinant) || std::fabs(Determinant) <= 1.0e-20f)
    {
        return false;
    }

    const FMatrix InverseViewProjection = ViewProjection.Inverse();

    for (int32 Index = 0; Index < 8; ++Index)
    {
        const FVector4 ClipPosition((Index & 1) ? 1.0f : -1.0f, (Index & 2) ? 1.0f : -1.0f, (Index & 4) ? 1.0f : 0.0f, 1.0f);
        const FVector4 WorldPosition = ClipPosition * InverseViewProjection;
        if (!std::isfinite(WorldPosition.W) || std::fabs(WorldPosition.W) <= 1.0e-6f)
        {
            return false;
        }

        const FVector Position(WorldPosition.X / WorldPosition.W, WorldPosition.Y / WorldPosition.W, WorldPosition.Z / WorldPosition.W);

        if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z))
        {
            return false;
        }

        OutCorners.Points[Index] = Position;
    }
    return true;
}

bool TryMakeWorldOBB(const FBox& LocalBounds, const FMatrix& WorldMatrix, FOrientedBox& OutOBB)
{
    const FVector LocalCenter = (LocalBounds.Min + LocalBounds.Max) * 0.5f;
    const FVector LocalExtent = (LocalBounds.Max - LocalBounds.Min) * 0.5f;

    const FVector RawAxes[3] =
    {
        FVector(WorldMatrix.M[0][0], WorldMatrix.M[0][1], WorldMatrix.M[0][2]),
        FVector(WorldMatrix.M[1][0], WorldMatrix.M[1][1], WorldMatrix.M[1][2]),
        FVector(WorldMatrix.M[2][0], WorldMatrix.M[2][1], WorldMatrix.M[2][2])
    };
    const float AxisLengths[3] = { RawAxes[0].Size(), RawAxes[1].Size(), RawAxes[2].Size() };
    OutOBB.Center = WorldMatrix.TransformPosition(LocalCenter);

    for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
    {
        const float AxisLength = AxisLengths[AxisIndex];
        if (!std::isfinite(AxisLength) || AxisLength <= 1.0e-8f)
        {
            return false;
        }

        OutOBB.Axes[AxisIndex] = RawAxes[AxisIndex] / AxisLength;
        OutOBB.Extent[AxisIndex] = std::fabs(LocalExtent[AxisIndex]) * AxisLength;
    }

    return true;
}

bool IsOBBInFrustumSAT(const FOrientedBox& OBB, const FFrustumPlanes& Frustum, const FFrustumCorners& Corners)
{
    for (const FPlane& Plane : Frustum.Planes)
    {
        if (IsSeparatedOnAxis(Plane.Normal, OBB, Corners))
        {
            return false;
        }
    }
    for (const FVector& Axis : OBB.Axes)
    {
        if (IsSeparatedOnAxis(Axis, OBB, Corners))
        {
            return false;
        }
    }
    static constexpr int32 FrustumEdges[12][2] =
    {
        { 0, 1 },{ 0, 2 },{ 1, 3 },{ 2, 3 },
        { 4, 5 },{ 4, 6 },{ 5, 7 },{ 6, 7 },
        { 0, 4 },{ 1, 5 },{ 2, 6 },{ 3, 7 }
    };

    for (const auto& Edge : FrustumEdges)
    {
        const FVector EdgeDirection = Corners.Points[Edge[1]] - Corners.Points[Edge[0]];
        
        for (const FVector& OBBAxis : OBB.Axes)
        {
            const FVector SeparatingAxis = EdgeDirection.Cross(OBBAxis);
            if (IsSeparatedOnAxis(SeparatingAxis, OBB, Corners))
            {
                return false;
            }
        }
    }
    return true;
}
