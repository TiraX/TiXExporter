
#include "FTiXMeshCluster.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Classes/Animation/SkeletalMeshActor.h"
#include "InstancedFoliageActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Runtime/Engine/Classes/Engine/DirectionalLight.h"
#include "Runtime/Engine/Classes/Components/LightComponent.h"
#include "RawMesh.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include "ImageUtils.h"
#include "TiXExporterBPLibrary.h"
#include "FTiXBoundingSphere.h"

static float VolumeCellSize = 1.f;
static const bool EnableVerbose = false;

FTiXMeshCluster::FTiXMeshCluster()
{
}

FTiXMeshCluster::FTiXMeshCluster(const TArray<FTiXVertex>& InVertices, const TArray<int32>& InIndices, float PositionScale)
{
	P.Reserve(InVertices.Num());
	for (const auto& V : InVertices)
	{
		P.Push(V.Position * PositionScale);
	}
	BBox = FBox(P);
	float Extent = BBox.GetExtent().Size() * 2.f;
	BBox.ExpandBy(Extent * 0.1f);
	Prims.Reserve(InIndices.Num() / 3);
	for (int32 i = 0 ; i < InIndices.Num() ; i += 3)
	{
		Prims.Push(FIntVector(InIndices[i + 0], InIndices[i + 1], InIndices[i + 2]));
	}
}

FTiXMeshCluster::~FTiXMeshCluster()
{
}

void FTiXMeshCluster::GenerateCluster(uint32 ClusterTriangles)
{
	check(P.Num() > 0 && Prims.Num() > 0);
	SortPrimitives();
	CalcPrimNormals();
	ScatterToVolume();
	MakeClusters(ClusterTriangles);
	MergeSmallClusters(ClusterTriangles);
}

void FTiXMeshCluster::SortPrimitives()
{

}

void FTiXMeshCluster::CalcPrimNormals()
{
	PrimsN.Reserve(Prims.Num());
	for (const auto& Prim : Prims)
	{
		const FVector& P0 = P[Prim.X];
		const FVector& P1 = P[Prim.Y];
		const FVector& P2 = P[Prim.Z];

		FVector P10 = P1 - P0;
		FVector P20 = P2 - P0;
		FVector PN = P10 ^ P20;
		PN.Normalize();
		PrimsN.Push(PN);
	}
}

inline FVector vec_floor(const FVector& vec)
{
	FVector r;
	r.X = floor(vec.X);
	r.Y = floor(vec.Y);
	r.Z = floor(vec.Z);
	return r;
}
inline FVector vec_ceil(const FVector& vec)
{
	FVector r;
	r.X = ceil(vec.X);
	r.Y = ceil(vec.Y);
	r.Z = ceil(vec.Z);
	return r;
}
inline FBox GetBoundingVolume(const FBox& BBox)
{
	FBox VolumeBox;
	VolumeBox.Min = vec_floor(BBox.Min / VolumeCellSize) * VolumeCellSize;
	VolumeBox.Max = vec_ceil(BBox.Max / VolumeCellSize) * VolumeCellSize;
	return VolumeBox;
}
inline FIntVector GetVolumeCellCount(const FBox& VolumeBox)
{
	FVector VolumeSize = VolumeBox.GetExtent() * 2.f;
	FIntVector VolumeCellCount;
	for (int32 i = 0; i < 3; ++i)
	{
		VolumeCellCount[i] = FMath::RoundToInt(VolumeSize[i] / VolumeCellSize);
	}
	return VolumeCellCount;
}
inline uint32 GetCellIndex(const FIntVector& CellPosition, const FIntVector& VolumeCellCount)
{
	const uint32 PageSize = VolumeCellCount.X * VolumeCellCount.Y;
	return CellPosition.Z * PageSize + CellPosition.Y * VolumeCellCount.X + CellPosition.X;
}
inline FIntVector GetCellPosition(uint32 CellIndex, const FIntVector& VolumeCellCount)
{
	const uint32 PageSize = VolumeCellCount.X * VolumeCellCount.Y;

	FIntVector Result;
	Result.Z = CellIndex / PageSize;
	Result.Y = (CellIndex % PageSize) / VolumeCellCount.X;
	Result.X = (CellIndex % PageSize) % VolumeCellCount.X;

	return Result;
}
inline bool IsTriangleIntersectWithBox(const TArray<FVector>& TrianglePoints, const FBox& Box)
{
	float TriangleMin, TriangleMax;
	float BoxMin, BoxMax;

	// Test the box normals (x-, y- and z-axes)
	FVector BoxNormals[3] =
	{
		FVector(1,0,0),
		FVector(0,1,0),
		FVector(0,0,1)
	};

	auto ProjectTriangle = [](const TArray<FVector>& Tri, const FVector& Axis, float& MinValue, float& MaxValue)
	{
		MinValue = FLT_MAX;
		MaxValue = FLT_MIN;

		const FVector Points[] =
		{
			Tri[0],
			Tri[1],
			Tri[2]
		};

		for (int32 i = 0; i < 3; ++i)
		{
			float V = Axis | Points[i];
			if (V < MinValue)
				MinValue = V;
			if (V > MaxValue)
				MaxValue = V;
		}
	};
	auto ProjectBox = [](const FBox& Box, const FVector& Axis, float& MinValue, float& MaxValue)
	{
		MinValue = FLT_MAX;
		MaxValue = FLT_MIN;

		const FVector Points[] =
		{
			FVector(Box.Min.X, Box.Min.Y, Box.Min.Z),
			FVector(Box.Max.X, Box.Min.Y, Box.Min.Z),
			FVector(Box.Min.X, Box.Max.Y, Box.Min.Z),
			FVector(Box.Max.X, Box.Max.Y, Box.Min.Z),

			FVector(Box.Min.X, Box.Min.Y, Box.Max.Z),
			FVector(Box.Max.X, Box.Min.Y, Box.Max.Z),
			FVector(Box.Min.X, Box.Max.Y, Box.Max.Z),
			FVector(Box.Max.X, Box.Max.Y, Box.Max.Z)
		};

		for (int32 i = 0; i < 8; ++i)
		{
			float V = Axis | Points[i];
			if (V < MinValue)
				MinValue = V;
			if (V > MaxValue)
				MaxValue = V;
		}
	};
	for (int32 i = 0; i < 3; i++)
	{
		const FVector& N = BoxNormals[i];
		ProjectTriangle(TrianglePoints, BoxNormals[i], TriangleMin, TriangleMax);
		if (TriangleMax < Box.Min[i] || TriangleMin > Box.Max[i])
			return false; // No intersection possible.
	}

	// Test the triangle normal
	FVector TriN = ((TrianglePoints[1] - TrianglePoints[0]) ^ (TrianglePoints[2] - TrianglePoints[0]));
	TriN.Normalize();
	float TriangleOffset = TriN | TrianglePoints[0];
	ProjectBox(Box, TriN, BoxMin, BoxMax);
	if (BoxMax < TriangleOffset || BoxMin > TriangleOffset)
		return false; // No intersection possible.

	// Test the nine edge cross-products
	FVector TriangleEdges[] =
	{
		TrianglePoints[0] - TrianglePoints[1],
		TrianglePoints[1] - TrianglePoints[2],
		TrianglePoints[2] - TrianglePoints[0]
	};
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			// The box normals are the same as it's edge tangents
			FVector Axis = TriangleEdges[i] ^ BoxNormals[j];
			ProjectBox(Box, Axis, BoxMin, BoxMax);
			ProjectTriangle(TrianglePoints, Axis, TriangleMin, TriangleMax);
			if (BoxMax <= TriangleMin || BoxMin >= TriangleMax)
				return false; // No intersection possible
		}
	}

	// No separating axis found.
	return true;
}
void FTiXMeshCluster::ScatterToVolume()
{
	const uint32 PointCount = (uint32)P.Num();
	const uint32 PrimCount = (uint32)Prims.Num();

	// Determine VolumeCellSize
	MeshVolume = GetBoundingVolume(BBox);
	MeshVolumeCellCount = GetVolumeCellCount(MeshVolume);
	while (MeshVolumeCellCount.X * MeshVolumeCellCount.Y * MeshVolumeCellCount.Z > 10 * 10 * 10)
	{
		VolumeCellSize += 1.f;
		MeshVolume = GetBoundingVolume(BBox);
		MeshVolumeCellCount = GetVolumeCellCount(MeshVolume);
	}
	VolumeCells.InsertZeroed(0, MeshVolumeCellCount.X * MeshVolumeCellCount.Y * MeshVolumeCellCount.Z);
	PrimVolumePositions.InsertZeroed(0, PrimCount);

	UE_LOG(LogTiXExporter, Log, TEXT("Mesh Volumes [%d, %d, %d] with size %f. Total : %d"), 
		MeshVolumeCellCount.X, MeshVolumeCellCount.Y, MeshVolumeCellCount.Z, VolumeCellSize, VolumeCells.Num());

	// Scatter every triangle to volume cell
	for (uint32 PrimIndex = 0; PrimIndex < PrimCount; ++PrimIndex)
	{
		const FIntVector& Prim = Prims[PrimIndex];

		TArray<FVector> TrianglePoints;
		TrianglePoints.Push(P[Prim.X]);
		TrianglePoints.Push(P[Prim.Y]);
		TrianglePoints.Push(P[Prim.Z]);

		FBox Box(TrianglePoints);
		FBox VolumeBox = GetBoundingVolume(Box);

		FIntVector VolumeCellCount = GetVolumeCellCount(VolumeBox);
		FIntVector VolumeCellStart = GetVolumeCellCount(FBox(MeshVolume.Min, VolumeBox.Min));

		for (int32 z = 0; z < VolumeCellCount.Z; ++z)
		{
			for (int32 y = 0; y < VolumeCellCount.Y; ++y)
			{
				for (int32 x = 0; x < VolumeCellCount.X; ++x)
				{
					FBox Cell;
					Cell.Min = VolumeBox.Min + FVector(VolumeCellSize * x, VolumeCellSize * y, VolumeCellSize * z);
					Cell.Max = Cell.Min + FVector(VolumeCellSize, VolumeCellSize, VolumeCellSize);

					if (IsTriangleIntersectWithBox(TrianglePoints, Cell))
					{
						// Mark prim in this cell
						FIntVector PrimVolumePosition = FIntVector(VolumeCellStart.X + x, VolumeCellStart.Y + y, VolumeCellStart.Z + z);
						uint32 CellIndex = GetCellIndex(PrimVolumePosition, MeshVolumeCellCount);
						VolumeCells[CellIndex].Push(PrimIndex);

						// Mark cell position for this prim
						PrimVolumePositions[PrimIndex].Push(CellIndex);
					}
				}
			}
		}
	}
}

inline bool IsNormalValid(const FVector& InN, const FVector& ClusterN)
{
	// angle between InN and ClusterN should NOT bigger than 60 degree
	static const float V = cos(FMath::DegreesToRadians(60));
	return (InN | ClusterN) > V;
}

void FTiXMeshCluster::MakeClusters(uint32 ClusterSize)
{
	// Go through each triangles
	const uint32 PointCount = (uint32)P.Num();
	const uint32 PrimCount = (uint32)Prims.Num();

	TArray<uint32> PrimsClusterId;
	PrimsClusterId.InsertZeroed(0, PrimCount);
	memset(PrimsClusterId.GetData(), 0, PrimCount * sizeof(uint32));

	Clusters.Empty();
	Clusters.Reserve(PrimCount / ClusterSize + 2);

	uint32 ClusterId = 0;
	Clusters.Push(TArray<uint32>());	// Cluster 0 always an empty cluster
	for (uint32 PrimIndex = 0; PrimIndex < PrimCount; ++PrimIndex)
	{
		if (PrimsClusterId[PrimIndex] != 0)
		{
			// Already in cluster, next
			continue;
		}

		++ClusterId;

		TArray<uint32> Cluster;
		Cluster.Reserve(ClusterSize);

		// Mark this prim in ClusterId
		PrimsClusterId[PrimIndex] = ClusterId;
		Cluster.Push(PrimIndex);

		// Calculate bounding sphere
		TArray<FVector> ClusterPoints;
		TMap<int32, uint32> PointsInCluster;
		TMap<FVector, uint32> UniquePosMap;
		ClusterPoints.Reserve(ClusterSize * 3);
		{
			const FIntVector& Prim = Prims[PrimIndex];
			ClusterPoints.Push(P[Prim.X]);
			ClusterPoints.Push(P[Prim.Y]);
			ClusterPoints.Push(P[Prim.Z]);
			PointsInCluster.Add(Prim.X);
			PointsInCluster.Add(Prim.Y);
			PointsInCluster.Add(Prim.Z);
			UniquePosMap.Add(P[Prim.X]);
			UniquePosMap.Add(P[Prim.Y]);
			UniquePosMap.Add(P[Prim.Z]);
		}

		// Cluster average normal
		TArray<FVector> ClusterPrimNormals;
		ClusterPrimNormals.Reserve(ClusterSize);
		FVector ClusterN = PrimsN[PrimIndex];
		ClusterPrimNormals.Push(ClusterN);

		// Init Bounding sphere
		FSphere BSphere = FTiXBoundingSphere::GetBoundingSphere(ClusterPoints);

		// Start search from this prim
		for (uint32 i = 1 ; i < ClusterSize; ++ i)
		{
			// Get neighbor triangles
			TArray<uint32> NeighbourPrims;
			GetNeighbourPrims(Cluster, NeighbourPrims, PrimsClusterId);
			if (EnableVerbose)
			{
				UE_LOG(LogTiXExporter, Log, TEXT("  %d Analysis %d neighbours with points %d"), i, NeighbourPrims.Num(), ClusterPoints.Num());
			}
			
			// Find the nearest prim,
			// 1, if any prim in BSphere, select it directly
			int32 PrimFound = -1;
			for (const auto& NeighbourPrimIndex : NeighbourPrims)
			{
				const FIntVector& NeighbourPrim = Prims[NeighbourPrimIndex];

				if (!IsNormalValid(PrimsN[NeighbourPrimIndex], ClusterN))
				{
					continue;
				}

				bool InsideBSphere = true;
				for (uint32 ii = 0; ii < 3; ++ii)
				{
					if (!BSphere.IsInside(P[NeighbourPrim[ii]]))
					{
						InsideBSphere = false;
						break;
					}
				}
				if (InsideBSphere)
				{
					PrimFound = NeighbourPrimIndex;
					break;
				}
			}

			// 2, No prims in BSphere, find the new Smallest BSphere
			if (PrimFound < 0)
			{
				float SmallestBSphereRadius = FLT_MAX;
				for (const auto& NeighbourPrimIndex : NeighbourPrims)
				{
					const FIntVector& NeighbourPrim = Prims[NeighbourPrimIndex];

					if (!IsNormalValid(PrimsN[NeighbourPrimIndex], ClusterN))
					{
						continue;
					}

					uint32 PointsAdded = 0;
					for (uint32 ii = 0; ii < 3; ++ii)
					{
						uint32 PIndex = NeighbourPrim[ii];
						uint32 * Result = PointsInCluster.Find(PIndex);
						if (Result == nullptr)
						{
							ClusterPoints.Push(P[PIndex]);
							++PointsAdded;
						}
					}
					FSphere NewBSphere = FTiXBoundingSphere::GetBoundingSphere(ClusterPoints);
					for (uint32 ii = 0 ; ii < PointsAdded ; ++ii)
					{
						ClusterPoints.Pop();
					}
					if (NewBSphere.W < SmallestBSphereRadius)
					{
						PrimFound = NeighbourPrimIndex;
						SmallestBSphereRadius = NewBSphere.W;
						BSphere = NewBSphere;
					}
				}
			}

			if (PrimFound < 0)
			{
				//TI_ASSERT(NeighbourPrims.size() == 0);
				break;
			}

			// Add PrimFound to Cluster
			PrimsClusterId[PrimFound] = ClusterId;
			Cluster.Push(PrimFound);
			{
				const FIntVector& Prim = Prims[PrimFound];
				for (int32 ii = 0 ; ii < 3 ; ++ ii)
				{
					uint32 * PointsInClusterResult = PointsInCluster.Find(Prim[ii]);
					if (PointsInClusterResult == nullptr)
					{
						uint32 * UniquePosMapResult = UniquePosMap.Find(P[Prim[ii]]);
						if (UniquePosMapResult == nullptr)
						{
							ClusterPoints.Push(P[Prim[ii]]);
							//UniquePosMap.FindOrAdd(P[Prim[ii]]);
							UniquePosMap.Add(P[Prim[ii]]);
						}
						PointsInCluster.Add(Prim[ii]);
						//PointsInCluster[Prim[ii]] = 1;
					}
				}
			}
			// Update Cluster N
			{
				ClusterPrimNormals.Push(PrimsN[PrimFound]);
				FSphere NBSphere = FTiXBoundingSphere::GetBoundingSphere(ClusterPrimNormals);
				ClusterN = NBSphere.Center;
				ClusterN.Normalize();
			}
			// Update Bounding Sphere
			BSphere = FTiXBoundingSphere::GetBoundingSphere(ClusterPoints);
		}

		check(Clusters.Num() == ClusterId);
		Clusters.Push(Cluster);

		UE_LOG(LogTiXExporter, Log, TEXT("Cluster %d generated with %d prims."), ClusterId, Cluster.Num());
	}
}

inline void GetNeighbourCells(uint32 CellIndex, const FIntVector& MeshVolumeCellCount, TArray<uint32>& OutNeighbourCells)
{
	OutNeighbourCells.Reserve(9);
	FIntVector CellPosition = GetCellPosition(CellIndex, MeshVolumeCellCount);

	for (int32 z = CellPosition.Z - 1 ; z <= CellPosition.Z + 1 ; ++ z)
	{
		if (z >= 0 && z < MeshVolumeCellCount.Z)
		{
			for (int32 y = CellPosition.Y - 1 ; y <= CellPosition.Y + 1 ; ++ y)
			{
				if (y >= 0 && y < MeshVolumeCellCount.Y)
				{
					for (int32 x = CellPosition.X - 1 ; x <= CellPosition.X + 1 ; ++ x)
					{
						if (x >= 0 && x < MeshVolumeCellCount.X)
						{
							uint32 Index = GetCellIndex(FIntVector(x, y, z), MeshVolumeCellCount);
							OutNeighbourCells.Push(Index);
						}
					}
				}
			}
		}
	}
}

void FTiXMeshCluster::GetNeighbourPrims(const TArray<uint32>& InPrims, TArray<uint32>& OutNeighbourPrims, const TArray<uint32>& InPrimsClusterId)
{
	const uint32 MIN_PRIMS_FOUND = 12;
	TMap<uint32, uint32> CellSearched;
	TMap<uint32, uint32> PrimsAdded;
	for (uint32 PrimIndex : InPrims)
	{
		const TArray<uint32>& CellPositions = PrimVolumePositions[PrimIndex];

		for (uint32 CellIndex : CellPositions)
		{
			uint32* CellResult = CellSearched.Find(CellIndex);
			if (CellResult == nullptr)
			{
				// Mark cell as searched
				CellSearched.Add(CellIndex);

				// Get triangles NOT in cluster in this cell
				const TArray<uint32>& Primitives = VolumeCells[CellIndex];
				for (uint32 CellPrimIndex : Primitives)
				{
					uint32 * PrimAddedResult = PrimsAdded.Find(CellPrimIndex);
					if (InPrimsClusterId[CellPrimIndex] == 0 && PrimAddedResult == nullptr)
					{
						OutNeighbourPrims.Push(CellPrimIndex);
						PrimsAdded.Add(CellPrimIndex);
					}
				}
			}
		}
	}
	if (PrimsAdded.Num() > MIN_PRIMS_FOUND)
	{
		return;
	}

	for (uint32 PrimIndex : InPrims)
	{
		// Search neighbour cells
		const uint32 MAX_ITERATION = 5;
		uint32 Iteration = 0;
		TMap<uint32, uint32> LastSearchedCells;
		while (Iteration < MAX_ITERATION)
		{
			TMap<uint32, uint32> CellSearchedCopy = CellSearched;
			// Remove last searched cells
			for (const auto LastCell : LastSearchedCells)
			{
				CellSearchedCopy.Remove(LastCell.Key);
			}

			// iteration on cell
			for (const auto& CellItem : CellSearchedCopy)
			{
				uint32 CellIndex = CellItem.Key;
				TArray<uint32> NeighbourCells;
				GetNeighbourCells(CellIndex, MeshVolumeCellCount, NeighbourCells);

				for (uint32 NeighbourCellIndex : NeighbourCells)
				{
					uint32 * CellSearchedResult = CellSearched.Find(NeighbourCellIndex);
					if (CellSearchedResult == nullptr)
					{
						// Mark cell as searched
						CellSearched.Add(NeighbourCellIndex);

						// Get triangles NOT in cluster in this cell
						const TArray<uint32>& Primitives = VolumeCells[NeighbourCellIndex];
						for (uint32 CellPrimIndex : Primitives)
						{
							uint32 * PrimsAddedResult = PrimsAdded.Find(CellPrimIndex);
							if (InPrimsClusterId[CellPrimIndex] == 0 && PrimsAddedResult == nullptr)
							{
								OutNeighbourPrims.Push(CellPrimIndex);
								PrimsAdded.Add(CellPrimIndex);
							}
						}
					}
				}
			}
			if (PrimsAdded.Num() > MIN_PRIMS_FOUND)
			{
				break;
			}

			LastSearchedCells = CellSearchedCopy;
			++Iteration;
		}
	}
}

void FTiXMeshCluster::MergeSmallClusters(uint32 ClusterTriangles)
{
	TArray<TArray<uint32>> FullClusters;
	TArray<TArray<uint32>> SmallClusters;

	for (int32 i = 0 ; i < Clusters.Num() ; ++ i)
	{
		if (Clusters[i].Num() < (int32)ClusterTriangles)
		{
			SmallClusters.Push(Clusters[i]);
		}
		else
		{
			FullClusters.Push(Clusters[i]);
		}
	}

	TArray<TArray<uint32>> MergedClusters;
	TArray<uint32> Merged;
	Merged.Reserve(ClusterTriangles);
	for (const auto& SC : SmallClusters)
	{
		const int32 Tri = SC.Num();
		for (int32 t = 0 ;t < Tri ; ++ t)
		{
			Merged.Push(SC[t]);
			if (Merged.Num() == ClusterTriangles)
			{
				MergedClusters.Push(Merged);
				Merged.Empty();
			}
		}
	}
	if (Merged.Num() > 0)
	{
		// Fill with last prim
		uint32 LastPrim = Merged[Merged.Num() - 1];
		for (uint32 m = Merged.Num() ; m < ClusterTriangles ; ++ m)
		{
			Merged.Push(LastPrim);
		}
		MergedClusters.Push(Merged);
	}
	for (const auto& MC : MergedClusters)
	{
		FullClusters.Push(MC);
	}
	Clusters = FullClusters;
}