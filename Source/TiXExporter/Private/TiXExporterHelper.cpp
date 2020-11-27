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

void ConvertToJsonArray(const FIntPoint& IntPointValue, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	TSharedRef< FJsonValueNumber > JsonValueX = MakeShareable(new FJsonValueNumber(IntPointValue.X));
	TSharedRef< FJsonValueNumber > JsonValueY = MakeShareable(new FJsonValueNumber(IntPointValue.Y));

	OutArray.Add(JsonValueX);
	OutArray.Add(JsonValueY);
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

void ConvertToJsonArray(const FRotator& RotatorValue, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	TSharedRef< FJsonValueNumber > JsonValuePitch = MakeShareable(new FJsonValueNumber(RotatorValue.Pitch));
	TSharedRef< FJsonValueNumber > JsonValueYaw = MakeShareable(new FJsonValueNumber(RotatorValue.Yaw));
	TSharedRef< FJsonValueNumber > JsonValueRoll = MakeShareable(new FJsonValueNumber(RotatorValue.Roll));

	OutArray.Add(JsonValuePitch);
	OutArray.Add(JsonValueYaw);
	OutArray.Add(JsonValueRoll);
}

void ConvertToJsonArray(const FBox& BoxValue, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	ConvertToJsonArray(BoxValue.Min, OutArray);
	ConvertToJsonArray(BoxValue.Max, OutArray);
}

void ConvertToJsonArray(const TArray<int32>& IntArray, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : IntArray)
	{
		TSharedRef< FJsonValueNumber > JsonValue = MakeShareable(new FJsonValueNumber(v));
		OutArray.Add(JsonValue);
	}
}
void ConvertToJsonArray(const TArray<uint32>& UIntArray, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : UIntArray)
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
void ConvertToJsonArray(const TArray<FString>& StringArray, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& s : StringArray)
	{
		TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(s));
		OutArray.Add(JsonValue);
	}
}

void ConvertToJsonArray(const float* FloatData, int32 Count, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (int32 i = 0; i < Count; i++)
	{
		TSharedRef< FJsonValueNumber > JsonValue = MakeShareable(new FJsonValueNumber(FloatData[i]));
		OutArray.Add(JsonValue);
	}
}

void ConvertToJsonArray(const FSHVectorRGB3& SH3, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	ConvertToJsonArray(SH3.R.V, TSHVector<3>::NumTotalFloats, OutArray);
	ConvertToJsonArray(SH3.G.V, TSHVector<3>::NumTotalFloats, OutArray);
	ConvertToJsonArray(SH3.B.V, TSHVector<3>::NumTotalFloats, OutArray);
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

void ConvertToJsonArray(const TArray<FTiXVertex>& VertexArray, uint32 VsFormat, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : VertexArray)
	{
		ConvertToJsonArray(v.Position, OutArray);

		if ((VsFormat & EVSSEG_NORMAL) != 0)
		{
			ConvertToJsonArray(v.Normal, OutArray);
		}
		if ((VsFormat & EVSSEG_COLOR) != 0)
		{
			ConvertToJsonArray(v.Color, OutArray);
		}
		if ((VsFormat & EVSSEG_TEXCOORD0) != 0)
		{
			ConvertToJsonArray(v.TexCoords[0], OutArray);
		}
		if ((VsFormat & EVSSEG_TEXCOORD1) != 0)
		{
			ConvertToJsonArray(v.TexCoords[1], OutArray);
		}
		if ((VsFormat & EVSSEG_TANGENT) != 0)
		{
			ConvertToJsonArray(v.TangentX, OutArray);
		}
		if ((VsFormat & EVSSEG_BLENDINDEX) != 0)
		{
			ConvertToJsonArray(v.BlendIndex, OutArray);
		}
		if ((VsFormat & EVSSEG_BLENDWEIGHT) != 0)
		{
			ConvertToJsonArray(v.BlendWeight, OutArray);
		}
	}
}


bool ContainComponent(const TArray<FString>& Components, const FString& CompName)
{
	return Components.Find(CompName) != INDEX_NONE;
}

FString GetResourcePath(const UObject * Resource)
{
	FString SM_GamePath = Resource->GetPathName();
	SM_GamePath = SM_GamePath.Replace(TEXT("/Game/"), TEXT(""));
	int32 DotIndex;
	bool LastDot = SM_GamePath.FindLastChar('.', DotIndex);
	if (LastDot)
	{
		SM_GamePath = SM_GamePath.Mid(0, DotIndex);
	}
	int32 SlashIndex;
	bool LastSlash = SM_GamePath.FindLastChar('/', SlashIndex);
	FString Path;
	if (LastSlash)
	{
		Path = SM_GamePath.Mid(0, SlashIndex + 1);
	}
	return Path;
}

FString GetResourcePathName(const UObject * Resource)
{
	return GetResourcePath(Resource) + Resource->GetName();
}

FString CombineResourceExportPath(const UObject * Resource, const FString& InExportPath)
{
	FString Path = GetResourcePath(Resource);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + Path;

	FString FullPathName = Path + Resource->GetName();
	return FullPathName;
}

TSharedPtr<FJsonObject> SaveMeshDataToJson(const TArray<FTiXVertex>& Vertices, const TArray<uint32>& Indices, int32 VsFormat)
{
	TSharedPtr<FJsonObject> JSection = MakeShareable(new FJsonObject);

	TArray< TSharedPtr<FJsonValue> > IndicesArray, VerticesArray;
	TArray< TSharedPtr<FJsonValue> > FormatArray;

#define ADD_VS_FORMAT(Format) if ((VsFormat & Format) != 0) FormatArray.Add(MakeShareable(new FJsonValueString(TEXT(#Format))))
	ADD_VS_FORMAT(EVSSEG_POSITION);
	ADD_VS_FORMAT(EVSSEG_NORMAL);
	ADD_VS_FORMAT(EVSSEG_COLOR);
	ADD_VS_FORMAT(EVSSEG_TEXCOORD0);
	ADD_VS_FORMAT(EVSSEG_TEXCOORD1);
	ADD_VS_FORMAT(EVSSEG_TANGENT);
	ADD_VS_FORMAT(EVSSEG_BLENDINDEX);
	ADD_VS_FORMAT(EVSSEG_BLENDWEIGHT);
#undef ADD_VS_FORMAT

	JSection->SetArrayField(TEXT("vs_format"), FormatArray);

	ConvertToJsonArray(Vertices, VsFormat, VerticesArray);
	JSection->SetArrayField(TEXT("vertices"), VerticesArray);

	ConvertToJsonArray(Indices, IndicesArray);
	JSection->SetArrayField(TEXT("indices"), IndicesArray);

	return JSection;
}

TSharedPtr<FJsonObject> SaveMeshSectionToJson(const FTiXMeshSection& TiXSection, const FString& SectionName, const FString& MaterialInstanceName)
{
	TSharedPtr<FJsonObject> JSection = MakeShareable(new FJsonObject);

	JSection->SetStringField(TEXT("name"), SectionName);
	JSection->SetStringField(TEXT("material"), MaterialInstanceName);
	JSection->SetNumberField(TEXT("index_start"), TiXSection.IndexStart);
	JSection->SetNumberField(TEXT("triangles"), TiXSection.NumTriangles);

	return JSection;
}