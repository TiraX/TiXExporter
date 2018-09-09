// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "TiXExporterBPLibrary.h"
#include "TiXExporter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "RawMesh.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "MeshUtilities.h"

DEFINE_LOG_CATEGORY(LogTiXExporter);

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

	bool operator == (const FTiXVertex& Other) const
	{
		return FMemory::Memcmp(this, &Other, sizeof(FTiXVertex)) == 0;
	}
};

/**
* Creates a hash value from a FTiXVertex.
*/
uint32 GetTypeHash(const FTiXVertex& Vertex)
{
	// Note: this assumes there's no padding in FVector that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Vertex, sizeof(Vertex));
}

static bool VerifyOrCreateDirectory(const FString& TargetDir)
{
	// Every function call, unless the function is inline, adds a small 
	// overhead which we can avoid by creating a local variable like so. 
	// But beware of making every function inline! 
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Directory Exists? 
	if (!PlatformFile.DirectoryExists(*TargetDir))
	{
		PlatformFile.CreateDirectory(*TargetDir);
	}

	if (!PlatformFile.DirectoryExists(*TargetDir)) {
		return false;
	}
	return true; 
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
		TSharedRef< FJsonValueNumber > JsonValueX = MakeShareable(new FJsonValueNumber(v.X));
		TSharedRef< FJsonValueNumber > JsonValueY = MakeShareable(new FJsonValueNumber(v.Y));
		TSharedRef< FJsonValueNumber > JsonValueZ = MakeShareable(new FJsonValueNumber(v.Z));

		OutArray.Add(JsonValueX);
		OutArray.Add(JsonValueY);
		OutArray.Add(JsonValueZ);
	}
}
void ConvertToJsonArray(const TArray<FVector2D>& VectorArray, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : VectorArray)
	{
		TSharedRef< FJsonValueNumber > JsonValueX = MakeShareable(new FJsonValueNumber(v.X));
		TSharedRef< FJsonValueNumber > JsonValueY = MakeShareable(new FJsonValueNumber(v.Y));

		OutArray.Add(JsonValueX);
		OutArray.Add(JsonValueY);
	}
}
void ConvertToJsonArray(const TArray<FTiXVertex>& VertexArray, uint32 VsFormat, TArray< TSharedPtr<FJsonValue> >& OutArray)
{
	for (const auto& v : VertexArray)
	{
		TSharedRef< FJsonValueNumber > PX = MakeShareable(new FJsonValueNumber(v.Position.X));
		TSharedRef< FJsonValueNumber > PY = MakeShareable(new FJsonValueNumber(v.Position.Y));
		TSharedRef< FJsonValueNumber > PZ = MakeShareable(new FJsonValueNumber(v.Position.Z));
		TSharedRef< FJsonValueNumber > NX = MakeShareable(new FJsonValueNumber(v.Normal.X));
		TSharedRef< FJsonValueNumber > NY = MakeShareable(new FJsonValueNumber(v.Normal.Y));
		TSharedRef< FJsonValueNumber > NZ = MakeShareable(new FJsonValueNumber(v.Normal.Z));
		TSharedRef< FJsonValueNumber > TX = MakeShareable(new FJsonValueNumber(v.TangentX.X));
		TSharedRef< FJsonValueNumber > TY = MakeShareable(new FJsonValueNumber(v.TangentX.Y));
		TSharedRef< FJsonValueNumber > TZ = MakeShareable(new FJsonValueNumber(v.TangentX.Z));

		OutArray.Add(PX);
		OutArray.Add(PY);
		OutArray.Add(PZ);
		OutArray.Add(NX);
		OutArray.Add(NY);
		OutArray.Add(NZ);
		OutArray.Add(TX);
		OutArray.Add(TY);
		OutArray.Add(TZ);

		if ((VsFormat & EVSSEG_TEXCOORD0) != 0)
		{
			TSharedRef< FJsonValueNumber > JU = MakeShareable(new FJsonValueNumber(v.TexCoords[0].X));
			TSharedRef< FJsonValueNumber > JV = MakeShareable(new FJsonValueNumber(v.TexCoords[0].Y));
			OutArray.Add(JU);
			OutArray.Add(JV);
		}
		if ((VsFormat & EVSSEG_TEXCOORD1) != 0)
		{
			TSharedRef< FJsonValueNumber > JU = MakeShareable(new FJsonValueNumber(v.TexCoords[1].X));
			TSharedRef< FJsonValueNumber > JV = MakeShareable(new FJsonValueNumber(v.TexCoords[1].Y));
			OutArray.Add(JU);
			OutArray.Add(JV);
		}
	}
}

void RecomputeNormals(FRawMesh& RawMesh)
{
	//float ComparisonThreshold = THRESH_POINTS_ARE_SAME;
	//int32 NumWedges = RawMesh.WedgeIndices.Num();

	//// Find overlapping corners to accelerate adjacency.
	//IMeshUtilities* MeshUtilities = FModuleManager::Get().LoadModulePtr<IMeshUtilities>("MeshUtilities");
	//FOverlappingCorners OverlappingCorners;
	//MeshUtilities->FindOverlappingCorners(OverlappingCorners, RawMesh.VertexPositions, RawMesh.WedgeIndices, ComparisonThreshold);

	//// Figure out if we should recompute normals and tangents.
	//bool bRecomputeNormals = false;// SrcModel.BuildSettings.bRecomputeNormals || RawMesh.WedgeTangentZ.Num() != NumWedges;
	//bool bRecomputeTangents = true;// SrcModel.BuildSettings.bRecomputeTangents || RawMesh.WedgeTangentX.Num() != NumWedges || RawMesh.WedgeTangentY.Num() != NumWedges;

	//// Dump normals and tangents if we are recomputing them.
	//if (bRecomputeTangents)
	//{
	//	RawMesh.WedgeTangentX.Empty(NumWedges);
	//	RawMesh.WedgeTangentX.AddZeroed(NumWedges);
	//	RawMesh.WedgeTangentY.Empty(NumWedges);
	//	RawMesh.WedgeTangentY.AddZeroed(NumWedges);
	//}
	//if (bRecomputeNormals)
	//{
	//	RawMesh.WedgeTangentZ.Empty(NumWedges);
	//	RawMesh.WedgeTangentZ.AddZeroed(NumWedges);
	//}

	//// Compute any missing tangents.
	//{
	//	// Static meshes always blend normals of overlapping corners.
	//	uint32 TangentOptions = ETangentOptions::BlendOverlappingNormals | ETangentOptions::IgnoreDegenerateTriangles;

	//	//MikkTSpace should be use only when the user want to recompute the normals or tangents otherwise should always fallback on builtin
	//	ComputeTangents_MikkTSpace(RawMesh, OverlappingCorners, TangentOptions);
	//}
}

UTiXExporterBPLibrary::UTiXExporterBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

void UTiXExporterBPLibrary::ExportCurrentScene(AActor * Actor)
{
	UWorld * CurrentWorld = Actor->GetWorld();
	ULevel * CurrentLevel = CurrentWorld->GetCurrentLevel();
	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(Actor, AStaticMeshActor::StaticClass(), Actors);

	UE_LOG(LogTiXExporter, Log, TEXT("Export tix scene ..."));
	for (auto A : Actors)
	{
		static int32 a = 0;
		UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
	}
}

void UTiXExporterBPLibrary::ExportStaticMesh(AStaticMeshActor * Actor, FString ExportPath, float MeshVertexPositionScale)
{
	UStaticMesh* StaticMesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
	FString SM_GamePath = StaticMesh->GetPathName();
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
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + Path;
	FString ExportFullPathName = ExportFullPath + StaticMesh->GetName() + TEXT(".tjs");

	UE_LOG(LogTiXExporter, Log, TEXT("Exporting Static Mesh: %s to %s."), *StaticMesh->GetName(), *ExportFullPath);
	FMeshDescription* Description = StaticMesh->GetOriginalMeshDescription(0);
	for (const auto& Model : StaticMesh->SourceModels)
	{
		FRawMesh ReadMesh;
		Model.LoadRawMesh(ReadMesh);
		RecomputeNormals(ReadMesh);
		const FRawMesh& MeshData = ReadMesh;
		
		TArray<TArray<FTiXVertex>> Vertices;
		TArray<TArray<int32>> Indices;
		TArray<TMap<FTiXVertex, int32>> IndexMap;

		check(MeshData.FaceMaterialIndices.Num() * 3 == MeshData.WedgeIndices.Num());
		check(MeshData.WedgeTangentZ.Num() == MeshData.WedgeIndices.Num());
		check(MeshData.WedgeTangentX.Num() == MeshData.WedgeIndices.Num());
		check(MeshData.WedgeTexCoords[0].Num() == MeshData.WedgeIndices.Num() || MeshData.WedgeTexCoords[0].Num() == 0);
		check(MeshData.WedgeTexCoords[1].Num() == MeshData.WedgeIndices.Num() || MeshData.WedgeTexCoords[1].Num() == 0);
		TMap<int32, int32> MaterialSections;
		for (int32 i = 0; i < MeshData.FaceMaterialIndices.Num(); ++i)
		{
			int32 FaceMaterialIndex = MeshData.FaceMaterialIndices[i];
			if (MaterialSections.Find(FaceMaterialIndex) == nullptr)
			{
				MaterialSections.Add(FaceMaterialIndex, 0);
			}
			else
			{
				MaterialSections[FaceMaterialIndex] ++;
			}
		}

		// Validate Material indices
		for (int32 i = 0; i < MaterialSections.Num(); ++i)
		{
			checkf(MaterialSections.Find(i) != nullptr, TEXT("Invalid material face index %d for %s."), i, *SM_GamePath);
		}

		// Init array
		Vertices.AddZeroed(MaterialSections.Num());
		Indices.AddZeroed(MaterialSections.Num());
		IndexMap.AddZeroed(MaterialSections.Num());

		int32 TexCoordCount = 0;
		check(MAX_TIX_TEXTURE_COORDS <= MAX_MESH_TEXTURE_COORDS);
		for (int32 uv = 0; uv < MAX_TIX_TEXTURE_COORDS; ++uv)
		{
			if (MeshData.WedgeTexCoords[uv].Num() > 0)
			{
				++TexCoordCount;
			}
		}
		uint32 VsFormat = 0;
		if (MeshData.VertexPositions.Num() > 0)
		{
			VsFormat |= EVSSEG_POSITION;
		}
		if (MeshData.WedgeTangentZ.Num() > 0)
		{
			VsFormat |= EVSSEG_NORMAL;
		}
		if (MeshData.WedgeTangentX.Num() > 0)
		{
			VsFormat |= EVSSEG_TANGENT;
		}
		if (MeshData.WedgeTexCoords[0].Num() > 0)
		{
			VsFormat |= EVSSEG_TEXCOORD0;
		}
		if (MeshData.WedgeTexCoords[1].Num() > 0)
		{
			VsFormat |= EVSSEG_TEXCOORD1;
		}

		// Add data for each material
		for (int32 face = 0; face < MeshData.FaceMaterialIndices.Num(); ++face)
		{
			int32 FaceMaterialIndex = MeshData.FaceMaterialIndices[face];
			int32 IndexOffset = face * 3;

			TArray<FTiXVertex>& VertexSection = Vertices[FaceMaterialIndex];
			TArray<int32>& IndexSection = Indices[FaceMaterialIndex];
			TMap<FTiXVertex, int32>& IndexMapSection = IndexMap[FaceMaterialIndex];
			
			for (int32 i = 0; i < 3; ++i)
			{
				FTiXVertex Vertex;
				Vertex.Position = MeshData.VertexPositions[MeshData.WedgeIndices[IndexOffset + i]] * MeshVertexPositionScale;
				Vertex.Normal = MeshData.WedgeTangentZ[IndexOffset + i];
				Vertex.TangentX = MeshData.WedgeTangentX[IndexOffset + i];
				for (int32 uv = 0; uv < TexCoordCount; ++uv)
				{
					Vertex.TexCoords[uv] = MeshData.WedgeTexCoords[uv][IndexOffset + i];
				}

				// gather vertices and indices
				int32 * VertexIndex = IndexMapSection.Find(Vertex);
				if (VertexIndex == nullptr)
				{
					// Add a new vertex to vertex buffer
					VertexSection.Add(Vertex);
					int32 CurrentIndex = VertexSection.Num() - 1;
					IndexSection.Add(CurrentIndex);
					IndexMapSection.Add(Vertex, CurrentIndex);
				}
				else
				{
					// Add an exist vertex's index
					IndexSection.Add(*VertexIndex);
				}
			}
		}

		// output json
		{
			TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

			// output basic info
			JsonObject->SetStringField(TEXT("name"), StaticMesh->GetName());
			JsonObject->SetStringField(TEXT("type"), TEXT("static_mesh"));
			JsonObject->SetNumberField(TEXT("version"), 1);
			JsonObject->SetStringField(TEXT("desc"), TEXT("Static mesh from TiX exporter."));
			JsonObject->SetNumberField(TEXT("vertex_count_total"), MeshData.VertexPositions.Num());
			JsonObject->SetNumberField(TEXT("index_count_total"), MeshData.WedgeIndices.Num());
			JsonObject->SetNumberField(TEXT("texcoord_count"), TexCoordCount);

			// output mesh sections
			TArray< TSharedPtr<FJsonValue> > JsonSections;
			for (int32 section = 0; section < MaterialSections.Num(); ++section)
			{
				TSharedPtr<FJsonObject> JSection = MakeShareable(new FJsonObject);
				JSection->SetNumberField(TEXT("vertex_count"), Vertices[section].Num());

				TArray< TSharedPtr<FJsonValue> > IndicesArray, VerticesArray;
				TArray< TSharedPtr<FJsonValue> > FormatArray;

				ConvertToJsonArray(Vertices[section], VsFormat, VerticesArray);
				JSection->SetArrayField(TEXT("vertices"), VerticesArray);

				ConvertToJsonArray(Indices[section], IndicesArray);
				JSection->SetArrayField(TEXT("indices"), IndicesArray);

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

				TSharedRef< FJsonValueObject > JsonSectionValue = MakeShareable(new FJsonValueObject(JSection));
				JsonSections.Add(JsonSectionValue);
			}
			JsonObject->SetArrayField(TEXT("sections"), JsonSections);
			
			FString OutputString;
			TSharedRef< TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR> > > Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
			FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
			
			if (VerifyOrCreateDirectory(ExportFullPath))
			{
				FFileHelper::SaveStringToFile(OutputString, *ExportFullPathName);
			}
			else
			{
				UE_LOG(LogTiXExporter, Error, TEXT("Failed to create directory : %s."), *ExportFullPath);
			}
		}
	}
}