// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "TiXExporterHelper.h"
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

//DEFINE_LOG_CATEGORY(LogTiXExporter);
void TryCreateDirectory(const FString& InTargetPath)
{
	FString TargetPath = InTargetPath;
	TArray<FString> Dirs;
	int32 SplitIndex;
	while (TargetPath.FindChar(L'/', SplitIndex))
	{
		FString Dir = TargetPath.Left(SplitIndex);
		Dirs.Add(Dir);
		TargetPath = TargetPath.Right(TargetPath.Len() - SplitIndex - 1);
	}
	if (!TargetPath.IsEmpty())
	{
		Dirs.Add(TargetPath);
	}


	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString TargetDir = "";
	for (int32 Dir = 0; Dir < Dirs.Num(); ++Dir)
	{
		TargetDir += Dirs[Dir] + TEXT("/");
		if (!PlatformFile.DirectoryExists(*TargetDir))
		{
			PlatformFile.CreateDirectory(*TargetDir);
		}
	}
}

bool VerifyOrCreateDirectory(FString& TargetDir)
{
	TargetDir.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (!TargetDir.EndsWith(TEXT("/")))
	{
		TargetDir += TEXT("/");
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Directory Exists? 
	if (!PlatformFile.DirectoryExists(*TargetDir))
	{
		TryCreateDirectory(TargetDir);
	}

	if (!PlatformFile.DirectoryExists(*TargetDir)) {
		return false;
	}
	return true;
}

void ConvertToJsonArray(const FVector2D& VectorValue, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	TSharedRef< FJsonValueNumber > JsonValueX = MakeShareable(new FJsonValueNumber(VectorValue.X));
	TSharedRef< FJsonValueNumber > JsonValueY = MakeShareable(new FJsonValueNumber(VectorValue.Y));

	OutArray.Add(JsonValueX);
	OutArray.Add(JsonValueY);
}

void ConvertToJsonArray(const FVector& VectorValue, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	TSharedRef< FJsonValueNumber > JsonValueX = MakeShareable(new FJsonValueNumber(VectorValue.X));
	TSharedRef< FJsonValueNumber > JsonValueY = MakeShareable(new FJsonValueNumber(VectorValue.Y));
	TSharedRef< FJsonValueNumber > JsonValueZ = MakeShareable(new FJsonValueNumber(VectorValue.Z));

	OutArray.Add(JsonValueX);
	OutArray.Add(JsonValueY);
	OutArray.Add(JsonValueZ);
}

void ConvertToJsonArray(const FQuat& QuatValue, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	TSharedRef< FJsonValueNumber > JsonValueX = MakeShareable(new FJsonValueNumber(QuatValue.X));
	TSharedRef< FJsonValueNumber > JsonValueY = MakeShareable(new FJsonValueNumber(QuatValue.Y));
	TSharedRef< FJsonValueNumber > JsonValueZ = MakeShareable(new FJsonValueNumber(QuatValue.Z));
	TSharedRef< FJsonValueNumber > JsonValueW = MakeShareable(new FJsonValueNumber(QuatValue.W));

	OutArray.Add(JsonValueX);
	OutArray.Add(JsonValueY);
	OutArray.Add(JsonValueZ);
	OutArray.Add(JsonValueW);
}

void ConvertToJsonArray(const FVector4& Vector4Value, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	TSharedRef< FJsonValueNumber > JsonValueX = MakeShareable(new FJsonValueNumber(Vector4Value.X));
	TSharedRef< FJsonValueNumber > JsonValueY = MakeShareable(new FJsonValueNumber(Vector4Value.Y));
	TSharedRef< FJsonValueNumber > JsonValueZ = MakeShareable(new FJsonValueNumber(Vector4Value.Z));
	TSharedRef< FJsonValueNumber > JsonValueW = MakeShareable(new FJsonValueNumber(Vector4Value.W));

	OutArray.Add(JsonValueX);
	OutArray.Add(JsonValueY);
	OutArray.Add(JsonValueZ);
	OutArray.Add(JsonValueW);
}

void ConvertToJsonArray(const TArray<int32>& IntArray, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : IntArray)
	{
		TSharedRef< FJsonValueNumber > JsonValue = MakeShareable(new FJsonValueNumber(v));
		OutArray.Add(JsonValue);
	}
}
void ConvertToJsonArray(const TArray<FVector>& VectorArray, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : VectorArray)
	{
		ConvertToJsonArray(v, OutArray);
	}
}
void ConvertToJsonArray(const TArray<FVector2D>& VectorArray, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : VectorArray)
	{
		ConvertToJsonArray(v, OutArray);
	}
}

void SaveJsonToFile(TSharedPtr<FJsonObject> JsonObject, const FString& Name, const FString& Path)
{
	FString OutputString;
	TSharedRef< TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR> > > Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	FString ExportPathStr = Path;
	if (VerifyOrCreateDirectory(ExportPathStr))
	{
		FString PathName = ExportPathStr + Name + TEXT(".tjs");
		FFileHelper::SaveStringToFile(OutputString, *PathName);
	}
	else
	{
		UE_LOG(LogTiXExporter, Error, TEXT("Failed to create directory : %s."), *ExportPathStr);
	}
}

void SaveUTextureToHDR(UTexture2D* Texture, const FString& FileName, const FString& Path)
{
	FString ExportPathStr = Path;
	FString ExportName;
	if (!VerifyOrCreateDirectory(ExportPathStr))
	{
		UE_LOG(LogTiXExporter, Error, TEXT("Failed to create directory : %s."), *ExportPathStr);
		return;
	}

	FString TotalFileName = FPaths::Combine(*ExportPathStr, *FileName);
	FText PathError;
	FPaths::ValidatePath(TotalFileName, &PathError);

	if (Texture && !FileName.IsEmpty() && PathError.IsEmpty())
	{
		FArchive* Ar = IFileManager::Get().CreateFileWriter(*TotalFileName);

		if (Ar)
		{
			FBufferArchive Buffer;
			bool bSuccess = FImageUtils::ExportTexture2DAsHDR(Texture, Buffer);

			if (bSuccess)
			{
				Ar->Serialize(const_cast<uint8*>(Buffer.GetData()), Buffer.Num());
			}

			delete Ar;
		}
		else
		{
			UE_LOG(LogTiXExporter, Error, TEXT("SaveUTextureToPNG: FileWrite failed to create."));
		}
	}
	else if (!Texture)
	{
		UE_LOG(LogTiXExporter, Error, TEXT("SaveUTextureToPNG: TextureRenderTarget must be non-null."));
	}
	if (!PathError.IsEmpty())
	{
		UE_LOG(LogTiXExporter, Error, TEXT("SaveUTextureToPNG: Invalid file path provided: '%s'"), *PathError.ToString());
	}
	if (FileName.IsEmpty())
	{
		UE_LOG(LogTiXExporter, Error, TEXT("SaveUTextureToPNG: FileName must be non-empty."));
	}
}

void SaveLandscapeToJson(ALandscape * LandscapeActor, const FString& LandscapeName, const FString& ExportPath)
{
	ULandscapeInfo * LandscapeInfo = LandscapeActor->GetLandscapeInfo();
	// Save sections
	{
		FString ExportPathLocal = ExportPath;
		VerifyOrCreateDirectory(ExportPathLocal);
		FString SectionPath = ExportPathLocal + LandscapeName + "_sections/";

		// output meshes and instances
		TArray<UTexture2D*> HeightmapTextures;
		TArray<FString> SectionNames;
		for (const auto& CompPair : LandscapeInfo->XYtoComponentMap)
		{
			const FIntPoint& Position = CompPair.Key;
			FString SectionName = FString::Printf(TEXT("section_%d_%d.hdr"), Position.X, Position.Y);
			
			// Heightmap
			const ULandscapeComponent * LandscapeComponent = CompPair.Value;
			UTexture2D * HeightmapTexture = LandscapeComponent->HeightmapTexture;
			if (HeightmapTextures.Find(HeightmapTexture) == INDEX_NONE)
			{
				HeightmapTextures.Add(HeightmapTexture);
				SectionNames.Add(SectionName);
			}
		}

		for (int32 TexIndex = 0 ; TexIndex < HeightmapTextures.Num() ; ++ TexIndex)
		{
			SaveUTextureToHDR(HeightmapTextures[TexIndex], SectionNames[TexIndex], SectionPath);
			UE_LOG(LogTiXExporter, Log, TEXT("  Exported landscape texture %s%s ..."), *SectionPath, *SectionNames[TexIndex]);
		}

	}
}


bool ContainComponent(const TArray<FString>& Components, const FString& CompName)
{
	return Components.Find(CompName) != INDEX_NONE;
}