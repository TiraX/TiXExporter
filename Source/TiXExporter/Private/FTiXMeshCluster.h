#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "TiXExporterDefines.h"

class FTiXMeshCluster
{
public:
	FTiXMeshCluster();
	~FTiXMeshCluster();

	void GenerateCluster(uint32 ClusterTriangles);

private:
	void SortPrimitives();
	void CalcPrimNormals();
	void ScatterToVolume();
	void MakeClusters(uint32 ClusterTriangles);
	void GetNeighbourPrims(const TArray<uint32>& InPrims, TArray<uint32>& OutNeighbourPrims, const TArray<uint32>& InPrimsClusterId);
	void MergeSmallClusters(uint32 ClusterTriangles);

private:
	TArray<FVector> P;
	TArray<FVector> N;
	TArray<FVector> UV;
	FBox BBox;
	TArray<FIntVector> Prims;
	TArray<FVector> PrimsN;

	// Volume cells
	FBox MeshVolume;
	FIntVector MeshVolumeCellCount;
	
	// Each cell contains prims intersects with this cell
	TArray< TArray<uint32> > VolumeCells;
	// Each prim remember the cells it intersected
	TArray< TArray<uint32> > PrimVolumePositions;

	// Final Clusters
	TArray< TArray<uint32> > Clusters;
};