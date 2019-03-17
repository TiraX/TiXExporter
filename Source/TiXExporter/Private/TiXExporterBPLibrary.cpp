// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "TiXExporterBPLibrary.h"
#include "TiXExporter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Classes/Animation/SkeletalMeshActor.h"
#include "InstancedFoliageActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Runtime/Landscape/Classes/Landscape.h"
#include "Runtime/Landscape/Classes/LandscapeComponent.h"
#include "Runtime/Landscape/Classes/LandscapeInfo.h"
#include "RawMesh.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"

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
	FVector4 Color;
	FVector4 BlendIndex;
	FVector4 BlendWeight;

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

/**
* Creates a hash value from a FTiXVertex.
*/
uint32 GetTypeHash(const FTiXVertex& Vertex)
{
	// Note: this assumes there's no padding in FVector that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Vertex, sizeof(Vertex));
}

static bool VerifyOrCreateDirectory(FString& TargetDir)
{
	TargetDir.Replace(TEXT("\\"), TEXT("/"));
	if (!TargetDir.EndsWith(TEXT("/")))
	{
		TargetDir += TEXT("/");
	}

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

void SaveLandscapeToJson(ALandscape * LandscapeActor, const FString& LandscapeName, const FString& ExportPath)
{
	ULandscapeInfo * LandscapeInfo = LandscapeActor->GetLandscapeInfo();

	// output basic info
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
		JsonObject->SetStringField(TEXT("name"), LandscapeName);
		JsonObject->SetStringField(TEXT("type"), TEXT("landscape"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Landscape sections from TiX exporter."));
		JsonObject->SetNumberField(TEXT("section_total"), LandscapeInfo->XYtoComponentMap.Num());

		// output meshes and instances
		TArray< TSharedPtr<FJsonValue> > JsonLandscapeSections;
		for (const auto& CompPair : LandscapeInfo->XYtoComponentMap)
		{
			TSharedPtr<FJsonObject> JSection = MakeShareable(new FJsonObject);

			// Position
			const FIntPoint& Position = CompPair.Key;
			TArray< TSharedPtr<FJsonValue> > JPoint;
			ConvertToJsonArray(Position, JPoint);
			JSection->SetArrayField(TEXT("point"), JPoint);

			// Set section path
			//FString SectionPath = ExportPath;
			//VerifyOrCreateDirectory(SectionPath);
			FString SectionPath = LandscapeName + "_sections/";
			FString SectionName = FString::Printf(TEXT("%ssection_%d_%d"), *SectionPath, Position.X, Position.Y);

			JSection->SetStringField(TEXT("section_name"), SectionName);

			TSharedRef< FJsonValueObject > JsonSection = MakeShareable(new FJsonValueObject(JSection));
			JsonLandscapeSections.Add(JsonSection);
		}
		JsonObject->SetArrayField(TEXT("sections"), JsonLandscapeSections);

		SaveJsonToFile(JsonObject, LandscapeName, ExportPath);
	}

	// Save sections
	{
		// output meshes and instances
		for (const auto& CompPair : LandscapeInfo->XYtoComponentMap)
		{
			const FIntPoint& Position = CompPair.Key;
			FString SectionName = FString::Printf(TEXT("section_%d_%d"), Position.X, Position.Y);

			TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
			JsonObject->SetStringField(TEXT("name"), SectionName);
			JsonObject->SetStringField(TEXT("type"), TEXT("landscape_section"));
			JsonObject->SetNumberField(TEXT("version"), 1);
			JsonObject->SetStringField(TEXT("desc"), TEXT("Landscape heightmap from TiX exporter."));
			JsonObject->SetNumberField(TEXT("section_x"), Position.X);
			JsonObject->SetNumberField(TEXT("section_y"), Position.Y);

			// Set section path
			FString SectionPath = ExportPath;
			VerifyOrCreateDirectory(SectionPath);
			SectionPath += LandscapeName + "_sections/";

			// Heightmap
			const ULandscapeComponent * LandscapeComponent = CompPair.Value;
			UTexture2D * HeightmapTexture = LandscapeComponent->HeightmapTexture;
			FColor * HeightmapData = (FColor*)HeightmapTexture->Source.LockMip(0);
			const int32 W = HeightmapTexture->GetSizeX();
			const int32 H = HeightmapTexture->GetSizeY();

			TArray< TSharedPtr<FJsonValue> > JHeightmap;
			JHeightmap.Reserve(W * H);
			for (int32 y = 0; y < H; ++y)
			{
				for (int32 x = 0; x < W; ++x)
				{
					const FColor& C = HeightmapData[y * W + x];
					uint32 Height = (C.R << 8) | C.G;
					TSharedRef< FJsonValueNumber > JHeightValue = MakeShareable(new FJsonValueNumber(Height));
					JHeightmap.Add(JHeightValue);
				}
			}
			JsonObject->SetArrayField(TEXT("heightmap"), JHeightmap);
			HeightmapTexture->Source.UnlockMip(0);

			SaveJsonToFile(JsonObject, SectionName, SectionPath);
		}
	}
}

UTiXExporterBPLibrary::UTiXExporterBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

void UTiXExporterBPLibrary::ExportCurrentScene(AActor * Actor, const FString& ExportPath)
{
	UWorld * CurrentWorld = Actor->GetWorld();
	ULevel * CurrentLevel = CurrentWorld->GetCurrentLevel();

	TMap<UStaticMesh *, TArray<FTiXInstance> > ActorInstances;

	TArray<AActor*> Actors;
	UE_LOG(LogTiXExporter, Log, TEXT("Export tix scene ..."));

	UE_LOG(LogTiXExporter, Log, TEXT("  Static meshes..."));
	UGameplayStatics::GetAllActorsOfClass(Actor, AStaticMeshActor::StaticClass(), Actors);
	int32 a = 0;
	for (auto A : Actors)
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
		AStaticMeshActor * SMActor = static_cast<AStaticMeshActor*>(A);
		UStaticMesh * StaticMesh = SMActor->GetStaticMeshComponent()->GetStaticMesh();

		TArray<FTiXInstance>& Instances = ActorInstances.FindOrAdd(StaticMesh);
		FTiXInstance InstanceInfo;
		InstanceInfo.Position = SMActor->GetTransform().GetLocation();
		InstanceInfo.Rotation = SMActor->GetTransform().GetRotation();
		InstanceInfo.Scale = SMActor->GetTransform().GetScale3D();
		Instances.Add(InstanceInfo);
	}

	UE_LOG(LogTiXExporter, Log, TEXT(" Skeletal meshes..."));
	Actors.Empty();
	UGameplayStatics::GetAllActorsOfClass(Actor, ASkeletalMeshActor::StaticClass(), Actors);
	for (auto A : Actors)
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
	}

	UE_LOG(LogTiXExporter, Log, TEXT(" Foliage and grass..."));
	Actors.Empty();
	UGameplayStatics::GetAllActorsOfClass(Actor, AInstancedFoliageActor::StaticClass(), Actors);
	for (auto A : Actors)
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
		AInstancedFoliageActor * FoliageActor = (AInstancedFoliageActor*)A;
		for (const auto& MeshPair : FoliageActor->FoliageMeshes)
		{
			const FFoliageMeshInfo& MeshInfo = *MeshPair.Value;

			UHierarchicalInstancedStaticMeshComponent* MeshComponent = MeshInfo.Component;
			TArray<FInstancedStaticMeshInstanceData> MeshDataArray = MeshComponent->PerInstanceSMData;

			UStaticMesh * StaticMesh = MeshComponent->GetStaticMesh();
			TArray<FTiXInstance>& Instances = ActorInstances.FindOrAdd(StaticMesh);

			for (auto& MeshMatrix : MeshDataArray)
			{
				FTransform MeshTransform = FTransform(MeshMatrix.Transform);
				FTiXInstance InstanceInfo;
				InstanceInfo.Position = MeshTransform.GetLocation();
				InstanceInfo.Rotation = MeshTransform.GetRotation();
				InstanceInfo.Scale = MeshTransform.GetScale3D();
				Instances.Add(InstanceInfo);
			}
		}
	}

	UE_LOG(LogTiXExporter, Log, TEXT(" Landscapes..."));
	TArray<AActor*> LandscapeActors;
	UGameplayStatics::GetAllActorsOfClass(Actor, ALandscape::StaticClass(), LandscapeActors);
	for (auto A : LandscapeActors)
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
		ALandscape * Landscape = static_cast<ALandscape *>(A); 
		ULandscapeInfo * LandscapeInfo = Landscape->GetLandscapeInfo();

		for (const auto& CompPair : LandscapeInfo->XYtoComponentMap)
		{
			const FIntPoint& Position = CompPair.Key;
			const ULandscapeComponent * LandscapeComponent = CompPair.Value;
			UTexture2D * HeightmapTexture = LandscapeComponent->HeightmapTexture;
			FColor * HeightmapData = (FColor*)HeightmapTexture->Source.LockMip(0);

			HeightmapTexture->Source.UnlockMip(0);
		}
	}

	UE_LOG(LogTiXExporter, Log, TEXT("Scene structure: "));
	int32 NumInstances = 0;
	for (const auto& MeshPair : ActorInstances)
	{
		const UStaticMesh * Mesh = MeshPair.Key;
		FString MeshName = Mesh->GetName();
		const TArray<FTiXInstance>& Instances = MeshPair.Value;

		UE_LOG(LogTiXExporter, Log, TEXT("  %s : %d instances."), *MeshName, Instances.Num());
		NumInstances += Instances.Num();
	}

	// output json
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		// output basic info
		JsonObject->SetStringField(TEXT("name"), CurrentWorld->GetName());
		JsonObject->SetStringField(TEXT("type"), TEXT("scene"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Scene information from TiX exporter."));
		JsonObject->SetNumberField(TEXT("mesh_total"), ActorInstances.Num());
		JsonObject->SetNumberField(TEXT("instances_total"), NumInstances);
		JsonObject->SetStringField(TEXT("texture_total"), TEXT("Unknown Yet"));

		// output meshes and instances
		TArray< TSharedPtr<FJsonValue> > JsonMeshes;
		for (const auto& MeshPair : ActorInstances)
		{
			const UStaticMesh * Mesh = MeshPair.Key;
			FString MeshName = Mesh->GetName();
			const TArray<FTiXInstance>& Instances = MeshPair.Value;

			// Mesh
			TSharedPtr<FJsonObject> JMesh = MakeShareable(new FJsonObject);
			JMesh->SetStringField(TEXT("name"), MeshName);

			TArray< TSharedPtr<FJsonValue> > JMeshInstances;
			for (const auto& Instance : Instances)
			{
				// Instance
				TSharedPtr<FJsonObject> JInstance = MakeShareable(new FJsonObject);
				TArray< TSharedPtr<FJsonValue> > JPosition, JRotation, JScale;
				ConvertToJsonArray(Instance.Position, JPosition);
				ConvertToJsonArray(Instance.Rotation, JRotation);
				ConvertToJsonArray(Instance.Scale, JScale);
				JInstance->SetArrayField(TEXT("position"), JPosition);
				JInstance->SetArrayField(TEXT("rotation"), JRotation);
				JInstance->SetArrayField(TEXT("scale"), JScale);

				TSharedRef< FJsonValueObject > JsonInstance = MakeShareable(new FJsonValueObject(JInstance));
				JMeshInstances.Add(JsonInstance);
			}
			JMesh->SetNumberField(TEXT("count"), Instances.Num());
			JMesh->SetArrayField(TEXT("instances"), JMeshInstances);
			TSharedRef< FJsonValueObject > JsonMesh = MakeShareable(new FJsonValueObject(JMesh));
			JsonMeshes.Add(JsonMesh);
		}
		JsonObject->SetArrayField(TEXT("scene"), JsonMeshes);

		// output landscapes
		if (LandscapeActors.Num() > 0)
		{
			TArray< TSharedPtr<FJsonValue> > JsonLandscapes;
			for (auto A : LandscapeActors)
			{
				ALandscape * LandscapeActor = static_cast<ALandscape *>(A);
				TSharedPtr<FJsonObject> JLandscape = MakeShareable(new FJsonObject);
				FString LandscapeName = CurrentWorld->GetName() + TEXT("-") + LandscapeActor->GetName();
				JLandscape->SetStringField(TEXT("name"), LandscapeName);

				TArray< TSharedPtr<FJsonValue> > JPosition, JRotation, JScale;
				ConvertToJsonArray(LandscapeActor->GetTransform().GetLocation(), JPosition);
				ConvertToJsonArray(LandscapeActor->GetTransform().GetRotation(), JRotation);
				ConvertToJsonArray(LandscapeActor->GetTransform().GetScale3D(), JScale);
				JLandscape->SetArrayField(TEXT("position"), JPosition);
				JLandscape->SetArrayField(TEXT("rotation"), JRotation);
				JLandscape->SetArrayField(TEXT("scale"), JScale);

				TSharedRef< FJsonValueObject > JsonLandscape = MakeShareable(new FJsonValueObject(JLandscape));
				JsonLandscapes.Add(JsonLandscape);
			}
			JsonObject->SetArrayField(TEXT("landscape"), JsonLandscapes);
		}

		SaveJsonToFile(JsonObject, CurrentWorld->GetName(), ExportPath);
	}
	ActorInstances.Empty();

	// Output landscape to Json file.
	if (LandscapeActors.Num() > 0)
	{
		for (auto A : LandscapeActors)
		{
			ALandscape * LandscapeActor = static_cast<ALandscape *>(A);
			FString LandscapeName = CurrentWorld->GetName() + TEXT("-") + LandscapeActor->GetName();

			SaveLandscapeToJson(LandscapeActor, LandscapeName, ExportPath);
		}
	}
}

TSharedPtr<FJsonObject> SaveMeshSectionToJson(const TArray<FTiXVertex>& Vertices, const TArray<int32>& Indices, const FString& MaterialInstanceName, int32 VsFormat)
{
	TSharedPtr<FJsonObject> JSection = MakeShareable(new FJsonObject);
	JSection->SetNumberField(TEXT("vertex_count"), Vertices.Num());

	JSection->SetStringField(TEXT("material"), MaterialInstanceName);

	TArray< TSharedPtr<FJsonValue> > IndicesArray, VerticesArray;
	TArray< TSharedPtr<FJsonValue> > FormatArray;

	ConvertToJsonArray(Vertices, VsFormat, VerticesArray);
	JSection->SetArrayField(TEXT("vertices"), VerticesArray);

	ConvertToJsonArray(Indices, IndicesArray);
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

	return JSection;
}

void ExportStaticMeshFromRenderData(UStaticMesh* StaticMesh, const FString& Path, const TArray<FString>& Components, float MeshVertexPositionScale)
{
	const int32 TotalLODs = StaticMesh->RenderData->LODResources.Num();

	// Export LOD0 only for now.
	int32 CurrentLOD = 0;
	FStaticMeshLODResources& LODResource = StaticMesh->RenderData->LODResources[CurrentLOD];

	const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LODResource.VertexBuffers.StaticMeshVertexBuffer;
	const FPositionVertexBuffer& PositionVertexBuffer = LODResource.VertexBuffers.PositionVertexBuffer;
	const FColorVertexBuffer& ColorVertexBuffer = LODResource.VertexBuffers.ColorVertexBuffer;
	const int32 TotalNumTexCoords = LODResource.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();

	// Get Vertex format
	uint32 VsFormat = 0;
	if (PositionVertexBuffer.GetNumVertices() > 0 && Components.Find(TEXT("POSITION")) != INDEX_NONE)
	{
		VsFormat |= EVSSEG_POSITION;
	}
	else
	{
		UE_LOG(LogTiXExporter, Error, TEXT("Static mesh [%s] do not have position stream."), *StaticMesh->GetPathName());
		return;
	}
	if (StaticMeshVertexBuffer.GetNumVertices() > 0)
	{
		if (Components.Find(TEXT("NORMAL")) != INDEX_NONE)
			VsFormat |= EVSSEG_NORMAL;
		if (Components.Find(TEXT("TANGENT")) != INDEX_NONE)
			VsFormat |= EVSSEG_TANGENT;
	}
	if (ColorVertexBuffer.GetNumVertices() > 0 && Components.Find(TEXT("COLOR")) != INDEX_NONE)
	{
		VsFormat |= EVSSEG_COLOR;
	}
	if (TotalNumTexCoords > 0 && Components.Find(TEXT("TEXCOORD0")) != INDEX_NONE)
	{
		VsFormat |= EVSSEG_TEXCOORD0;
	}
	if (TotalNumTexCoords > 1 && Components.Find(TEXT("TEXCOORD1")) != INDEX_NONE)
	{
		VsFormat |= EVSSEG_TEXCOORD1;
	}

	TArray<uint32> MeshIndices;
	LODResource.IndexBuffer.GetCopy(MeshIndices);

	TArray< TSharedPtr<FJsonValue> > JsonSections;
	for (int32 Section = 0; Section < LODResource.Sections.Num(); ++Section)
	{
		FStaticMeshSection& MeshSection = LODResource.Sections[Section];

		const int32 TotalFaces = MeshSection.NumTriangles;
		const int32 MinVertexIndex = MeshSection.MinVertexIndex;
		const int32 MaxVertexIndex = MeshSection.MaxVertexIndex;
		const int32 FirstIndex = MeshSection.FirstIndex;
		FString MaterialName = StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface->GetName();
		
		// data container
		TArray<FTiXVertex> VertexSection;
		TArray<int32> IndexSection;
		TMap<FTiXVertex, int32> IndexMapSection;

		// go through indices
		const int32 MaxIndex = FirstIndex + TotalFaces * 3;
		for (int32 ii = FirstIndex; ii < MaxIndex; ++ii)
		{
			uint32 Index = MeshIndices[ii];
			FTiXVertex Vertex;
			Vertex.Position = PositionVertexBuffer.VertexPosition(Index) * MeshVertexPositionScale;
			Vertex.Normal = StaticMeshVertexBuffer.VertexTangentZ(Index).GetSafeNormal();
			Vertex.TangentX = StaticMeshVertexBuffer.VertexTangentX(Index).GetSafeNormal();
			for (int32 uv = 0; uv < TotalNumTexCoords && uv < MAX_TIX_TEXTURE_COORDS; ++uv)
			{
				Vertex.TexCoords[uv] = StaticMeshVertexBuffer.GetVertexUV(Index, uv);
			}
			FColor C = ColorVertexBuffer.VertexColor(Index);
			const float OneOver255 = 1.f / 255.f;
			Vertex.Color.X = C.R * OneOver255;
			Vertex.Color.Y = C.G * OneOver255;
			Vertex.Color.Z = C.B * OneOver255;
			Vertex.Color.W = C.A * OneOver255;

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

		TSharedPtr<FJsonObject> JSection = SaveMeshSectionToJson(VertexSection, IndexSection, MaterialName, VsFormat);

		TSharedRef< FJsonValueObject > JsonSectionValue = MakeShareable(new FJsonValueObject(JSection));
		JsonSections.Add(JsonSectionValue);
	}

	// output json
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		// output basic info
		JsonObject->SetStringField(TEXT("name"), StaticMesh->GetName());
		JsonObject->SetStringField(TEXT("type"), TEXT("static_mesh"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Static mesh (Render Resource) from TiX exporter."));
		JsonObject->SetNumberField(TEXT("vertex_count_total"), LODResource.VertexBuffers.PositionVertexBuffer.GetNumVertices());
		JsonObject->SetNumberField(TEXT("index_count_total"), MeshIndices.Num());
		JsonObject->SetNumberField(TEXT("texcoord_count"), TotalNumTexCoords);
		JsonObject->SetNumberField(TEXT("total_lod"), 1);

		// output mesh sections
		JsonObject->SetArrayField(TEXT("sections"), JsonSections);

		SaveJsonToFile(JsonObject, StaticMesh->GetName(), Path);
	}
}

void ExportStaticMeshFromRawMesh(UStaticMesh* StaticMesh, const FString& Path, const TArray<FString>& Components, float MeshVertexPositionScale)
{
	FMeshDescription* Description = StaticMesh->GetOriginalMeshDescription(0);
	for (const auto& Model : StaticMesh->SourceModels)
	{
		FRawMesh ReadMesh;
		Model.LoadRawMesh(ReadMesh);
		const FRawMesh& MeshData = ReadMesh;

		TArray<TArray<FTiXVertex>> Vertices;
		TArray<TArray<int32>> Indices;
		TArray<TMap<FTiXVertex, int32>> IndexMap;
		TArray<FStaticMaterial*> Materials;

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
		check(MaterialSections.Num() == StaticMesh->StaticMaterials.Num());

		// Validate Material indices
		for (int32 i = 0; i < MaterialSections.Num(); ++i)
		{
			checkf(MaterialSections.Find(i) != nullptr, TEXT("Invalid material face index %d for %s."), i, *StaticMesh->GetPathName());
		}

		// Init array
		Vertices.AddZeroed(MaterialSections.Num());
		Indices.AddZeroed(MaterialSections.Num());
		IndexMap.AddZeroed(MaterialSections.Num());
		Materials.AddZeroed(MaterialSections.Num());

		int32 TexCoordCount = 0;
		check(MAX_TIX_TEXTURE_COORDS <= MAX_MESH_TEXTURE_COORDS);
		for (int32 uv = 0; uv < MAX_TIX_TEXTURE_COORDS; ++uv)
		{
			if (MeshData.WedgeTexCoords[uv].Num() > 0)
			{
				++TexCoordCount;
			}
		}

		// Get Vertex format
		uint32 VsFormat = 0;
		if (MeshData.VertexPositions.Num() > 0 && Components.Find(TEXT("POSITION")) != INDEX_NONE)
		{
			VsFormat |= EVSSEG_POSITION;
		}
		else
		{
			UE_LOG(LogTiXExporter, Error, TEXT("Static mesh [%s] do not have position stream."), *StaticMesh->GetPathName());
			return;
		}
		if (MeshData.WedgeTangentZ.Num() > 0 && Components.Find(TEXT("NORMAL")) != INDEX_NONE)
		{
			VsFormat |= EVSSEG_NORMAL;
		}
		if (MeshData.WedgeColors.Num() > 0 && Components.Find(TEXT("COLOR")) != INDEX_NONE)
		{
			VsFormat |= EVSSEG_COLOR;
		}
		if (MeshData.WedgeTexCoords[0].Num() > 0 && Components.Find(TEXT("TEXCOORD0")) != INDEX_NONE)
		{
			VsFormat |= EVSSEG_TEXCOORD0;
		}
		if (MeshData.WedgeTexCoords[1].Num() > 0 && Components.Find(TEXT("TEXCOORD1")) != INDEX_NONE)
		{
			VsFormat |= EVSSEG_TEXCOORD1;
		}
		if (MeshData.WedgeTangentX.Num() > 0 && Components.Find(TEXT("TANGENT")) != INDEX_NONE)
		{
			VsFormat |= EVSSEG_TANGENT;
		}

		// Add data for each section
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
				if (MeshData.WedgeColors.Num() > 0)
				{
					const float OneOver255 = 1.f / 255.f;
					FColor C = MeshData.WedgeColors[IndexOffset + i];
					Vertex.Color.X = C.R * OneOver255;
					Vertex.Color.Y = C.G * OneOver255;
					Vertex.Color.Z = C.B * OneOver255;
					Vertex.Color.W = C.A * OneOver255;
				}
				if (MeshData.WedgeTangentX.Num() > 0)
				{
					Vertex.TangentX = MeshData.WedgeTangentX[IndexOffset + i];
				}
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

		// Get Material info
		for (int32 section = 0; section < MaterialSections.Num(); ++section)
		{
			Materials[section] = &StaticMesh->StaticMaterials[section];

			// Convert material interface (material instance or template)
		}

		// output json
		{
			TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

			// output basic info
			JsonObject->SetStringField(TEXT("name"), StaticMesh->GetName());
			JsonObject->SetStringField(TEXT("type"), TEXT("static_mesh"));
			JsonObject->SetNumberField(TEXT("version"), 1);
			JsonObject->SetStringField(TEXT("desc"), TEXT("Static mesh (Raw Mesh) from TiX exporter."));
			JsonObject->SetNumberField(TEXT("vertex_count_total"), MeshData.VertexPositions.Num());
			JsonObject->SetNumberField(TEXT("index_count_total"), MeshData.WedgeIndices.Num());
			JsonObject->SetNumberField(TEXT("texcoord_count"), TexCoordCount);
			JsonObject->SetNumberField(TEXT("total_lod"), 1);

			// output mesh sections
			TArray< TSharedPtr<FJsonValue> > JsonSections;
			for (int32 section = 0; section < MaterialSections.Num(); ++section)
			{
				TSharedPtr<FJsonObject> JSection = SaveMeshSectionToJson(Vertices[section], Indices[section], Materials[section]->MaterialInterface->GetName(), VsFormat);

				TSharedRef< FJsonValueObject > JsonSectionValue = MakeShareable(new FJsonValueObject(JSection));
				JsonSections.Add(JsonSectionValue);
			}
			JsonObject->SetArrayField(TEXT("sections"), JsonSections);

			SaveJsonToFile(JsonObject, StaticMesh->GetName(), Path);
		}
	}
}

void UTiXExporterBPLibrary::ExportStaticMesh(AStaticMeshActor * Actor, FString ExportPath, TArray<FString> Components, float MeshVertexPositionScale)
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

	if (StaticMesh->bAllowCPUAccess)
	{
		UE_LOG(LogTiXExporter, Log, TEXT("Exporting Static Mesh: %s to %s."), *StaticMesh->GetName(), *ExportFullPath);
		ExportStaticMeshFromRenderData(StaticMesh, ExportFullPath, Components, MeshVertexPositionScale);
	}
	else
	{
		UE_LOG(LogTiXExporter, Warning, TEXT("Exporting Static Mesh: %s to %s. Mesh do not have CPU Access, export from RawMesh"), *StaticMesh->GetName(), *ExportFullPath);
		ExportStaticMeshFromRawMesh(StaticMesh, ExportFullPath, Components, MeshVertexPositionScale);
	}
}