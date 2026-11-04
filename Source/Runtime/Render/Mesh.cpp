#include "EnginePCH.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include <algorithm>
#include <cmath>
#include <limits>

UStaticMesh::~UStaticMesh()
{
	// 프록시 알림은 ~FMeshRenderResource가 한다
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex]) {
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

void UStaticMesh::RebuildRenderData()
{
    FBox Bounds = MeshData.AABB;
    for (const auto& LOD : AdditionalLODs)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Bounds.Min[Axis] = std::min(Bounds.Min[Axis], LOD.Data.AABB.Min[Axis]);
            Bounds.Max[Axis] = std::max(Bounds.Max[Axis], LOD.Data.AABB.Max[Axis]);
        }
    }

    FLODSphere Sphere;
    Sphere.Center = (Bounds.Min + Bounds.Max) * 0.5f;
    double RadiusSquared = 0.0;
    for (uint32 LOD = 0; LOD < GetLODCount(); ++LOD)
    {
        for (const FVertexPNCT& Vertex : GetMeshData(LOD).Vertices)
        {
            const double X = static_cast<double>(Vertex.Position.X) - Sphere.Center.X;
            const double Y = static_cast<double>(Vertex.Position.Y) - Sphere.Center.Y;
            const double Z = static_cast<double>(Vertex.Position.Z) - Sphere.Center.Z;
            RadiusSquared = std::max(RadiusSquared, X * X + Y * Y + Z * Z);
        }
    }
    // Include a small geometric margin and round outwards.
    Sphere.RadiusSquared = std::nextafter(
        static_cast<float>(RadiusSquared * (1.0 + 2e-5)), std::numeric_limits<float>::infinity());

    SetRenderData(Bounds, Sphere);
}
