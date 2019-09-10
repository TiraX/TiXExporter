// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

struct FTiXExporterSetting
{
	float TileSize;
	float MeshVertexPositionScale;
	bool bIgnoreMaterial;

	FTiXExporterSetting()
		: TileSize(16.f)
		, MeshVertexPositionScale(0.01f)
		, bIgnoreMaterial(false)
	{}
};

static const int32 MAX_TIX_TEXTURE_COORDS = 2;
enum E_VERTEX_STREAM_SEGMENT
{
	EVSSEG_POSITION = 1,
	EVSSEG_NORMAL = EVSSEG_POSITION << 1,
	EVSSEG_COLOR = EVSSEG_NORMAL << 1,
	EVSSEG_TEXCOORD0 = EVSSEG_COLOR << 1,
	EVSSEG_TEXCOORD1 = EVSSEG_TEXCOORD0 << 1,
	EVSSEG_TANGENT = EVSSEG_TEXCOORD1 << 1,
	EVSSEG_BLENDINDEX = EVSSEG_TANGENT << 1,
	EVSSEG_BLENDWEIGHT = EVSSEG_BLENDINDEX << 1,

	EVSSEG_TOTAL = EVSSEG_BLENDWEIGHT,
};

struct FTiXVertex
{
	FVector Position;
	FVector Normal;
	FVector TangentX;
	FVector2D TexCoords[MAX_TIX_TEXTURE_COORDS];
	FVector4 Color;
	FVector4 BlendIndex;
	FVector4 BlendWeight;

	FTiXVertex()
		: Color(1.f, 1.f, 1.f, 1.f)
	{}

	bool operator == (const FTiXVertex& Other) const
	{
		return FMemory::Memcmp(this, &Other, sizeof(FTiXVertex)) == 0;
	}
};

struct FTiXInstance
{
	FVector Position;
	FQuat Rotation;
	FVector Scale;
};

struct FDependency
{
	TArray<FString> DependenciesMeshes;
	TArray<FString> DependenciesMaterialInstances;
	TArray<FString> DependenciesMaterials;
	TArray<FString> DependenciesTextures;
};

struct FTiXSceneTile
{
	FIntPoint Position;
	float TileSize;
	int32 InstanceCount;
	FBox BBox;

	TMap<UStaticMesh*, TArray<FTiXInstance> > TileInstances;

	FTiXSceneTile()
		: TileSize(0.f)
		, InstanceCount(0)
		, BBox(ForceInit)
	{}
};