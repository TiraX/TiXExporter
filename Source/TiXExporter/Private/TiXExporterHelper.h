// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Runtime/Landscape/Classes/Landscape.h"
#include "Runtime/Landscape/Classes/LandscapeComponent.h"
#include "Runtime/Landscape/Classes/LandscapeInfo.h"


bool VerifyOrCreateDirectory(FString& TargetDir);

void ConvertToJsonArray(const FVector2D& VectorValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FVector& VectorValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FQuat& QuatValue, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const FVector4& Vector4Value, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<int32>& IntArray, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<FVector>& VectorArray, TArray< TSharedPtr<FJsonValue> >& OutArray);
void ConvertToJsonArray(const TArray<FVector2D>& VectorArray, TArray< TSharedPtr<FJsonValue> >& OutArray);

void SaveJsonToFile(TSharedPtr<FJsonObject> JsonObject, const FString& Name, const FString& Path);
void SaveUTextureToHDR(UTexture2D* Texture, const FString& FileName, const FString& Path);
void SaveLandscapeToJson(ALandscape * LandscapeActor, const FString& LandscapeName, const FString& ExportPath);

bool ContainComponent(const TArray<FString>& Components, const FString& CompName);