// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Runtime/Landscape/Classes/Landscape.h"
#include "Runtime/Landscape/Classes/LandscapeComponent.h"
#include "Runtime/Landscape/Classes/LandscapeInfo.h"
#include "TiXExporterDefines.h"


bool VerifyOrCreateDirectory(FString& TargetDir);

void ConvertToJsonArray(const FIntPoint& IntPointValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FVector2D& VectorValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FVector& VectorValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FQuat& QuatValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FVector4& Vector4Value, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FRotator& RotatorValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FBox& BoxValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<int32>& IntArray, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<uint32>& UIntArray, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<FVector>& VectorArray, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<FVector2D>& VectorArray, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<FTiXVertex>& VertexArray, uint32 VsFormat, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<FString>& StringArray, TArray< TSharedPtr<FJsonValue> >& OutArray);

void SaveJsonToFile(TSharedPtr<FJsonObject> JsonObject, const FString& Name, const FString& Path);
void SaveUTextureToHDR(UTexture2D* Texture, const FString& FileName, const FString& Path);

// Save mesh vertices and indices
TSharedPtr<FJsonObject> SaveMeshDataToJson(const TArray<FTiXVertex>& Vertices, const TArray<uint32>& Indices, int32 VsFormat);

// Save mesh sections info
TSharedPtr<FJsonObject> SaveMeshSectionToJson(const FTiXMeshSection& TiXSection, const FString& SectionName, const FString& MaterialInstanceName);

bool ContainComponent(const TArray<FString>& Components, const FString& CompName);

/**
* Creates a hash value from a FTiXVertex.
*/
inline uint32 GetTypeHash(const FTiXVertex& Vertex)
{
	// Note: this assumes there's no padding in FVector that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Vertex, sizeof(Vertex));
}

FString GetResourcePath(const UObject * Resource);
FString GetResourcePathName(const UObject * Resource);
FString CombineResourceExportPath(const UObject * Resource, const FString& InExportPath);