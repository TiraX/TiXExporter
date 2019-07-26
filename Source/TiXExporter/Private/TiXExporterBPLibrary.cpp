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
#include "Runtime/Engine/Classes/Engine/DirectionalLight.h"
#include "Runtime/Engine/Classes/Components/LightComponent.h"
#include "Runtime/Engine/Classes/Camera/CameraActor.h"
#include "Runtime/Engine/Classes/Camera/CameraComponent.h"
#include "Runtime/Engine/Classes/Materials/Material.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstance.h"
#include "Runtime/Engine/Classes/Engine/Texture2D.h"
#include "Runtime/Engine/Classes/Engine/TextureCube.h"
#include "RawMesh.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include "ImageUtils.h"
#include "Runtime/Engine/Classes/Exporters/Exporter.h"
#include "TiXExporterHelper.h"

DEFINE_LOG_CATEGORY(LogTiXExporter);

const FString ExtName = TEXT(".tasset");
const int32 MaxTextureSize = 1024;

UTiXExporterBPLibrary::UTiXExporterBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UTiXExporterBPLibrary::ExportCurrentScene(AActor * Actor, const FString& ExportPath, const TArray<FString>& SceneComponents, const TArray<FString>& MeshComponents, float MeshVertexPositionScale)
{
	UWorld * CurrentWorld = Actor->GetWorld();
	ULevel * CurrentLevel = CurrentWorld->GetCurrentLevel();

	TMap<UStaticMesh *, TArray<FTiXInstance> > ActorInstances;

	TArray<AActor*> Actors;
	int32 a = 0;
	UE_LOG(LogTiXExporter, Log, TEXT("Export tix scene ..."));
	FDependency Dependency;

	if (ContainComponent(SceneComponents, TEXT("STATIC_MESH")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT("  Static mesh actors..."));
		UGameplayStatics::GetAllActorsOfClass(Actor, AStaticMeshActor::StaticClass(), Actors);
		for (auto A : Actors)
		{
			UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
			AStaticMeshActor * SMActor = static_cast<AStaticMeshActor*>(A);
			UStaticMesh * StaticMesh = SMActor->GetStaticMeshComponent()->GetStaticMesh();

			TArray<FTiXInstance>& Instances = ActorInstances.FindOrAdd(StaticMesh);
			FTiXInstance InstanceInfo;
			InstanceInfo.Position = SMActor->GetTransform().GetLocation() * MeshVertexPositionScale;
			InstanceInfo.Rotation = SMActor->GetTransform().GetRotation();
			InstanceInfo.Scale = SMActor->GetTransform().GetScale3D();
			Instances.Add(InstanceInfo);
		}
	}

	if (ContainComponent(SceneComponents, TEXT("SKELETAL_MESH")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Skeletal mesh actors..."));
		Actors.Empty();
		UGameplayStatics::GetAllActorsOfClass(Actor, ASkeletalMeshActor::StaticClass(), Actors);
		for (auto A : Actors)
		{
			UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
		}
	}

	if (ContainComponent(SceneComponents, TEXT("FOLIAGE_AND_GRASS")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Foliage and grass  actors..."));
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
					InstanceInfo.Position = MeshTransform.GetLocation() * MeshVertexPositionScale;
					InstanceInfo.Rotation = MeshTransform.GetRotation();
					InstanceInfo.Scale = MeshTransform.GetScale3D();
					Instances.Add(InstanceInfo);
				}
			}
		}
	}

	// Export mesh resources
	if (ContainComponent(SceneComponents, TEXT("STATIC_MESH")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT("  Static meshes..."));

		for (auto& MeshPair : ActorInstances)
		{
			UStaticMesh * Mesh = MeshPair.Key;
			ExportStaticMeshInternal(Mesh, ExportPath, MeshComponents, MeshVertexPositionScale, Dependency);
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

		// output dependencies
		{
			TSharedPtr<FJsonObject> JDependency = MakeShareable(new FJsonObject);
			TArray< TSharedPtr<FJsonValue> > JTextures, JMaterialInstances, JMaterials, JMeshes;
			for (const auto& Tex : Dependency.DependenciesTextures)
			{
				TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Tex + ExtName));
				JTextures.Add(JsonValue);
			}
			JDependency->SetArrayField(TEXT("textures"), JTextures);
			for (const auto& Material : Dependency.DependenciesMaterials)
			{
				TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Material + ExtName));
				JMaterials.Add(JsonValue);
			}
			JDependency->SetArrayField(TEXT("materials"), JMaterials);
			for (const auto& MaterialInstance : Dependency.DependenciesMaterialInstances)
			{
				TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(MaterialInstance + ExtName));
				JMaterialInstances.Add(JsonValue);
			}
			JDependency->SetArrayField(TEXT("material_instances"), JMaterialInstances);
			for (const auto& Mesh : Dependency.DependenciesMeshes)
			{
				TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Mesh + ExtName));
				JMeshes.Add(JsonValue);
			}
			JDependency->SetArrayField(TEXT("meshes"), JMeshes);
			JsonObject->SetObjectField(TEXT("dependency"), JDependency);
		}

		// output meshes and instances
		TArray< TSharedPtr<FJsonValue> > JsonInstances;
		int32 MeshIndex = 0;
		for (const auto& MeshPair : ActorInstances)
		{
			const UStaticMesh * Mesh = MeshPair.Key;
			const TArray<FTiXInstance>& Instances = MeshPair.Value;
			ExportInstances(Mesh, Instances, ExportPath, CurrentWorld->GetName());

			FString ExportPathName = CurrentWorld->GetName() + TEXT("/") + Mesh->GetName() + TEXT("_Ins") + ExtName;
			TSharedRef< FJsonValueString > JMeshIns = MakeShareable(new FJsonValueString(ExportPathName));
			JsonInstances.Add(JMeshIns);
		}
		JsonObject->SetArrayField(TEXT("instances"), JsonInstances);

		// output cameras
		TArray<AActor*> Cameras;
		UGameplayStatics::GetAllActorsOfClass(Actor, ACameraActor::StaticClass(), Cameras);
		if (Cameras.Num() > 0)
		{
			TArray< TSharedPtr<FJsonValue> > JCameras;
			for (auto A : Cameras)
			{
				ACameraActor * Cam = Cast<ACameraActor>(A);
				UCameraComponent * CamComp = Cam->GetCameraComponent();
				TSharedPtr<FJsonObject> JCamera = MakeShareable(new FJsonObject);

				FVector CamDir = CamComp->GetComponentToWorld().GetRotation().Vector();
				CamDir.Normalize();
				FVector CamLocation = CamComp->GetComponentToWorld().GetTranslation();
				FVector CamTarget = CamLocation + CamDir * 100.f;
				FRotator CamRot = CamComp->GetComponentToWorld().GetRotation().Rotator();

				CamLocation *= MeshVertexPositionScale;
				CamTarget *= MeshVertexPositionScale;

				TArray< TSharedPtr<FJsonValue> > JLocation, JTarget, JRotator;
				ConvertToJsonArray(CamLocation, JLocation);
				ConvertToJsonArray(CamTarget, JTarget);
				ConvertToJsonArray(CamRot, JRotator);
				JCamera->SetArrayField(TEXT("location"), JLocation);
				JCamera->SetArrayField(TEXT("target"), JTarget);
				JCamera->SetArrayField(TEXT("rotator"), JRotator);
				JCamera->SetNumberField(TEXT("fov"), CamComp->FieldOfView);
				JCamera->SetNumberField(TEXT("aspect"), CamComp->AspectRatio);

				TSharedRef< FJsonValueObject > JsonCamera = MakeShareable(new FJsonValueObject(JCamera));
				JCameras.Add(JsonCamera);
			}
			JsonObject->SetArrayField(TEXT("cameras"), JCameras);
		}

		// output env
		TSharedPtr<FJsonObject> JEnvironment = MakeShareable(new FJsonObject);
		TArray<AActor*> SunLights;
		UGameplayStatics::GetAllActorsOfClass(Actor, ADirectionalLight::StaticClass(), SunLights);
		if (SunLights.Num() > 0)
		{
			//for (auto A : SunLights)
			// Only export 1 sun light
			TSharedPtr<FJsonObject> JSunLight = MakeShareable(new FJsonObject);
			auto A = SunLights[0];
			{
				ADirectionalLight * SunLight = static_cast<ADirectionalLight *>(A);
				ULightComponent * LightComponent = SunLight->GetLightComponent();

				JSunLight->SetStringField(TEXT("name"), SunLight->GetName());

				TArray< TSharedPtr<FJsonValue> > JDirection, JColor;
				ConvertToJsonArray(LightComponent->GetDirection(), JDirection);
				ConvertToJsonArray(LightComponent->GetLightColor(), JColor);
				JSunLight->SetArrayField(TEXT("direction"), JDirection);
				JSunLight->SetArrayField(TEXT("color"), JColor);
				JSunLight->SetNumberField(TEXT("intensity"), LightComponent->Intensity);
			}
			JEnvironment->SetObjectField(TEXT("sun_light"), JSunLight);
		}
		JsonObject->SetObjectField(TEXT("environment"), JEnvironment);

		// output landscapes
		if (ContainComponent(SceneComponents, TEXT("LANDSCAPE")))
		{
			UE_LOG(LogTiXExporter, Log, TEXT(" Landscapes..."));
			TArray<AActor*> LandscapeActors;
			UGameplayStatics::GetAllActorsOfClass(Actor, ALandscape::StaticClass(), LandscapeActors);
			if (LandscapeActors.Num() > 0)
			{
				TArray< TSharedPtr<FJsonValue> > JsonLandscapes;
				for (auto A : LandscapeActors)
				{
					ALandscape * LandscapeActor = static_cast<ALandscape *>(A);
					ULandscapeInfo * LandscapeInfo = LandscapeActor->GetLandscapeInfo();
					TSharedPtr<FJsonObject> JLandscape = MakeShareable(new FJsonObject);
					FString LandscapeName = CurrentWorld->GetName() + TEXT("-") + LandscapeActor->GetName();
					JLandscape->SetStringField(TEXT("name"), LandscapeName);

					TArray< TSharedPtr<FJsonValue> > JPosition, JRotation, JScale;
					ConvertToJsonArray(LandscapeActor->GetTransform().GetLocation() * MeshVertexPositionScale, JPosition);
					ConvertToJsonArray(LandscapeActor->GetTransform().GetRotation(), JRotation);
					ConvertToJsonArray(LandscapeActor->GetTransform().GetScale3D(), JScale);
					JLandscape->SetArrayField(TEXT("position"), JPosition);
					JLandscape->SetArrayField(TEXT("rotation"), JRotation);
					JLandscape->SetArrayField(TEXT("scale"), JScale);

					TArray<UTexture2D*> HeightmapTextures;
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
						}
					}
					FString ExportPathLocal = ExportPath;
					VerifyOrCreateDirectory(ExportPathLocal);
					FString LandscapeHeightmapPath = ExportPathLocal + LandscapeName + "_sections/";
					TArray< TSharedPtr<FJsonValue> > JHeightmaps;
					for (int32 TexIndex = 0; TexIndex < HeightmapTextures.Num(); ++TexIndex)
					{
						FString HeightTextureName = HeightmapTextures[TexIndex]->GetName();
						HeightTextureName += TEXT(".hdr");
						SaveUTextureToHDR(HeightmapTextures[TexIndex], HeightTextureName, LandscapeHeightmapPath);

						TSharedRef< FJsonValueString > HeightmapName = MakeShareable(new FJsonValueString(LandscapeName + "_sections/" + HeightTextureName));
						JHeightmaps.Add(HeightmapName);
					}
					JLandscape->SetArrayField(TEXT("heightmaps"), JHeightmaps);

					TSharedRef< FJsonValueObject > JsonLandscape = MakeShareable(new FJsonValueObject(JLandscape));
					JsonLandscapes.Add(JsonLandscape);
				}
				JsonObject->SetArrayField(TEXT("landscape"), JsonLandscapes);
			}
		}

		SaveJsonToFile(JsonObject, CurrentWorld->GetName(), ExportPath);
	}
	ActorInstances.Empty();
}

void UTiXExporterBPLibrary::ExportStaticMeshActor(AStaticMeshActor * StaticMeshActor, FString ExportPath, const TArray<FString>& Components, float MeshVertexPositionScale)
{
	ExportStaticMesh(StaticMeshActor->GetStaticMeshComponent()->GetStaticMesh(), ExportPath, Components, MeshVertexPositionScale);
}

void UTiXExporterBPLibrary::ExportStaticMesh(UStaticMesh * StaticMesh, FString ExportPath, const TArray<FString>& Components, float MeshVertexPositionScale)
{
	FDependency Dependency;
	ExportStaticMeshInternal(StaticMesh, ExportPath, Components, MeshVertexPositionScale, Dependency);
}

void UTiXExporterBPLibrary::ExportStaticMeshInternal(UStaticMesh * StaticMesh, FString ExportPath, const TArray<FString>& Components, float MeshVertexPositionScale, FDependency& Dependency)
{
	if (StaticMesh->bAllowCPUAccess)
	{
		UE_LOG(LogTiXExporter, Log, TEXT("Exporting Static Mesh: %s to %s."), *StaticMesh->GetName(), *ExportPath);
		ExportStaticMeshFromRenderData(StaticMesh, ExportPath, Components, MeshVertexPositionScale, Dependency);
	}
	else
	{
		UE_LOG(LogTiXExporter, Error, TEXT("Exporting Static Mesh: %s to %s. Mesh do not have CPU Access, export from RawMesh"), *StaticMesh->GetName(), *ExportPath);
		//ExportStaticMeshFromRawMesh(StaticMesh, ExportFullPath, Components, MeshVertexPositionScale, Dependency);
	}
}
void UTiXExporterBPLibrary::ExportStaticMeshFromRenderData(UStaticMesh* StaticMesh, const FString& InExportPath, const TArray<FString>& Components, float MeshVertexPositionScale, FDependency& Dependency)
{
	FString SMPath = GetResourcePath(StaticMesh);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + SMPath;

	FString FullPathName = SMPath + StaticMesh->GetName();
	Dependency.DependenciesMeshes.AddUnique(FullPathName);

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
	if (PositionVertexBuffer.GetNumVertices() > 0 && ContainComponent(Components, (TEXT("POSITION"))))
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
		if (ContainComponent(Components, (TEXT("NORMAL"))))
			VsFormat |= EVSSEG_NORMAL;
		if (ContainComponent(Components, (TEXT("TANGENT"))))
			VsFormat |= EVSSEG_TANGENT;
	}
	if (ColorVertexBuffer.GetNumVertices() > 0 && ContainComponent(Components, (TEXT("COLOR"))))
	{
		VsFormat |= EVSSEG_COLOR;
	}
	if (TotalNumTexCoords > 0 && ContainComponent(Components, (TEXT("TEXCOORD0"))))
	{
		VsFormat |= EVSSEG_TEXCOORD0;
	}
	if (TotalNumTexCoords > 1 && ContainComponent(Components, (TEXT("TEXCOORD1"))))
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
		FString MaterialInstancePathName = GetResourcePath(StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface);
		MaterialInstancePathName += StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface->GetName();
		FString MaterialSlotName = StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialSlotName.ToString();

		ExportMaterialInstance(StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface, InExportPath, Dependency);

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
			if ((VsFormat & EVSSEG_NORMAL) != 0)
			{
				Vertex.Normal = StaticMeshVertexBuffer.VertexTangentZ(Index).GetSafeNormal();
			}
			if ((VsFormat & EVSSEG_TANGENT) != 0)
			{
				Vertex.TangentX = StaticMeshVertexBuffer.VertexTangentX(Index).GetSafeNormal();
			}
			if ((VsFormat & EVSSEG_TEXCOORD0) != 0)
			{
				Vertex.TexCoords[0] = StaticMeshVertexBuffer.GetVertexUV(Index, 0);
			}
			if ((VsFormat & EVSSEG_TEXCOORD1) != 0)
			{
				Vertex.TexCoords[1] = StaticMeshVertexBuffer.GetVertexUV(Index, 1);
			}
			if ((VsFormat & EVSSEG_COLOR) != 0)
			{
				FColor C = ColorVertexBuffer.VertexColor(Index);
				const float OneOver255 = 1.f / 255.f;
				Vertex.Color.X = C.R * OneOver255;
				Vertex.Color.Y = C.G * OneOver255;
				Vertex.Color.Z = C.B * OneOver255;
				Vertex.Color.W = C.A * OneOver255;
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

		TSharedPtr<FJsonObject> JSection = SaveMeshSectionToJson(VertexSection, IndexSection, MaterialSlotName, MaterialInstancePathName + ExtName, VsFormat);

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

		SaveJsonToFile(JsonObject, StaticMesh->GetName(), ExportFullPath);
	}
}

void UTiXExporterBPLibrary::ExportStaticMeshFromRawMesh(UStaticMesh* StaticMesh, const FString& Path, const TArray<FString>& Components, float MeshVertexPositionScale, FDependency& Dependency)
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
		if (MeshData.VertexPositions.Num() > 0 && ContainComponent(Components, (TEXT("POSITION"))))
		{
			VsFormat |= EVSSEG_POSITION;
		}
		else
		{
			UE_LOG(LogTiXExporter, Error, TEXT("Static mesh [%s] do not have position stream."), *StaticMesh->GetPathName());
			return;
		}
		if (MeshData.WedgeTangentZ.Num() > 0 && ContainComponent(Components, (TEXT("NORMAL"))))
		{
			VsFormat |= EVSSEG_NORMAL;
		}
		if (MeshData.WedgeColors.Num() > 0 && ContainComponent(Components, (TEXT("COLOR"))))
		{
			VsFormat |= EVSSEG_COLOR;
		}
		if (MeshData.WedgeTexCoords[0].Num() > 0 && ContainComponent(Components, (TEXT("TEXCOORD0"))))
		{
			VsFormat |= EVSSEG_TEXCOORD0;
		}
		if (MeshData.WedgeTexCoords[1].Num() > 0 && ContainComponent(Components, (TEXT("TEXCOORD1"))))
		{
			VsFormat |= EVSSEG_TEXCOORD1;
		}
		if (MeshData.WedgeTangentX.Num() > 0 && ContainComponent(Components, (TEXT("TANGENT"))))
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
				TSharedPtr<FJsonObject> JSection = SaveMeshSectionToJson(Vertices[section], Indices[section], Materials[section]->MaterialSlotName.ToString(), Materials[section]->MaterialInterface->GetName() + ExtName, VsFormat);

				TSharedRef< FJsonValueObject > JsonSectionValue = MakeShareable(new FJsonValueObject(JSection));
				JsonSections.Add(JsonSectionValue);
			}
			JsonObject->SetArrayField(TEXT("sections"), JsonSections);

			SaveJsonToFile(JsonObject, StaticMesh->GetName(), Path);
		}
	}
}

void UTiXExporterBPLibrary::ExportMaterialInstance(UMaterialInterface* InMaterial, const FString& InExportPath, FDependency& Dependency)
{
	if (InMaterial->IsA(UMaterial::StaticClass()))
	{
		ExportMaterial(InMaterial, InExportPath, Dependency);
	}
	else
	{
		check(InMaterial->IsA(UMaterialInstance::StaticClass()));
		UMaterialInstance * MaterialInstance = Cast<UMaterialInstance>(InMaterial);

		FString Path = GetResourcePath(MaterialInstance);
		FString ExportPath = InExportPath;
		ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (ExportPath[ExportPath.Len() - 1] != '/')
			ExportPath.AppendChar('/');
		FString ExportFullPath = ExportPath + Path;

		FString FullPathName = Path + InMaterial->GetName();

		// Use Dependency to check if this already exported.
		if (Dependency.DependenciesMaterialInstances.Find(FullPathName) != INDEX_NONE)
		{
			return;
		}

		Dependency.DependenciesMaterialInstances.AddUnique(FullPathName);

		// Linked Material
		UMaterialInterface * ParentMaterial = MaterialInstance->Parent;
		check(ParentMaterial && ParentMaterial->IsA(UMaterial::StaticClass()));
		ExportMaterial(ParentMaterial, InExportPath, Dependency);
		FString MaterialPathName = GetResourcePath(ParentMaterial);
		MaterialPathName += ParentMaterial->GetName();

		// Parameters
		// Scalar parameters
		TArray<FVector4> ScalarVectorParams;
		TArray<FString> ScalarVectorNames;
		for (int32 i = 0; i < MaterialInstance->ScalarParameterValues.Num(); ++i)
		{
			const FScalarParameterValue& ScalarValue = MaterialInstance->ScalarParameterValues[i];

			int32 CombinedIndex = i / 4;
			int32 IndexInVector4 = i % 4;
			if (ScalarVectorParams.Num() <= CombinedIndex)
			{
				ScalarVectorParams.Add(FVector4());
				FString Name = FString::Printf(TEXT("CombinedScalar%d"), CombinedIndex);
				ScalarVectorNames.Add(Name);
			}
			ScalarVectorParams[CombinedIndex][IndexInVector4] = ScalarValue.ParameterValue;
		}

		// Vector parameters.
		for (int32 i = 0; i < MaterialInstance->VectorParameterValues.Num(); ++i)
		{
			const FVectorParameterValue& VectorValue = MaterialInstance->VectorParameterValues[i];

			ScalarVectorParams.Add(FVector4(VectorValue.ParameterValue));
			ScalarVectorNames.Add(VectorValue.ParameterInfo.Name.ToString());
		}

		// Texture parameters.
		TArray<FString> TextureParams;
		TArray<FString> TextureParamNames;
		TArray<UTexture*> Textures;
		for (int32 i = 0; i < MaterialInstance->TextureParameterValues.Num(); ++i)
		{
			const FTextureParameterValue& TextureValue = MaterialInstance->TextureParameterValues[i];

			FString TexturePath = GetResourcePath(TextureValue.ParameterValue);
			TexturePath += TextureValue.ParameterValue->GetName();
			TextureParams.Add(TexturePath);
			TextureParamNames.Add(TextureValue.ParameterInfo.Name.ToString());
			Textures.Add(TextureValue.ParameterValue);

			ExportTexture(TextureValue.ParameterValue, InExportPath, Dependency);
		}

		// output json
		{
			TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

			// output basic info
			JsonObject->SetStringField(TEXT("name"), InMaterial->GetName());
			JsonObject->SetStringField(TEXT("type"), TEXT("material_instance"));
			JsonObject->SetNumberField(TEXT("version"), 1);
			JsonObject->SetStringField(TEXT("desc"), TEXT("Material instance from TiX exporter."));
			JsonObject->SetStringField(TEXT("linked_material"), MaterialPathName + ExtName);

			// output parameters
			TSharedPtr<FJsonObject> JParameters = MakeShareable(new FJsonObject);
			check(ScalarVectorParams.Num() == ScalarVectorNames.Num() && TextureParams.Num() == TextureParamNames.Num());
			for (int32 SVParam = 0; SVParam < ScalarVectorParams.Num(); ++SVParam)
			{
				TSharedPtr<FJsonObject> JParameter = MakeShareable(new FJsonObject);
				JParameter->SetStringField(TEXT("type"), TEXT("float4"));
				
				TArray< TSharedPtr<FJsonValue> > JSVParam;
				ConvertToJsonArray(ScalarVectorParams[SVParam], JSVParam);
				JParameter->SetArrayField(TEXT("value"), JSVParam);

				JParameters->SetObjectField(ScalarVectorNames[SVParam], JParameter);
			}
			for (int32 TexParam = 0; TexParam < TextureParams.Num(); ++TexParam)
			{
				TSharedPtr<FJsonObject> JParameter = MakeShareable(new FJsonObject);
				// Texture type
				FVector2D Resolution;
				if (Textures[TexParam]->IsA(UTexture2D::StaticClass()))
				{
					JParameter->SetStringField(TEXT("type"), TEXT("texture2d"));
					UTexture2D * Tex2D = Cast<UTexture2D>(Textures[TexParam]);
					Resolution.X = Tex2D->GetSizeX() >> Tex2D->LODBias;
					Resolution.Y = Tex2D->GetSizeY() >> Tex2D->LODBias;
					if (Resolution.X > MaxTextureSize)
						Resolution.X = MaxTextureSize;
					if (Resolution.Y > MaxTextureSize)
						Resolution.Y = MaxTextureSize;
				}
				else if (Textures[TexParam]->IsA(UTextureCube::StaticClass()))
				{
					JParameter->SetStringField(TEXT("type"), TEXT("texturecube"));
					UTextureCube * TexCube = Cast<UTextureCube>(Textures[TexParam]);
					Resolution.X = TexCube->GetSizeX();
					Resolution.Y = TexCube->GetSizeY();
					Resolution.X = TexCube->GetSizeX() >> TexCube->LODBias;
					Resolution.Y = TexCube->GetSizeY() >> TexCube->LODBias;
					if (Resolution.X > MaxTextureSize)
						Resolution.X = MaxTextureSize;
					if (Resolution.Y > MaxTextureSize)
						Resolution.Y = MaxTextureSize;
				}
				else
				{
					UE_LOG(LogTiXExporter, Error, TEXT("Unsupport texture type other than 2D and Cube. %s"), *TextureParams[TexParam]);
				}
				// Texture name
				JParameter->SetStringField(TEXT("value"), TextureParams[TexParam] + ExtName);
				// Texture resolution for virtual texture usage
				TArray< TSharedPtr<FJsonValue> > JResolution;
				ConvertToJsonArray(Resolution, JResolution);
				JParameter->SetArrayField(TEXT("size"), JResolution);

				JParameters->SetObjectField(TextureParamNames[TexParam], JParameter);
			}
			JsonObject->SetObjectField(TEXT("parameters"), JParameters);
			SaveJsonToFile(JsonObject, InMaterial->GetName(), ExportFullPath);
		}
	}
}

void UTiXExporterBPLibrary::ExportMaterial(UMaterialInterface* InMaterial, const FString& InExportPath, FDependency& Dependency)
{
	check(InMaterial->IsA(UMaterial::StaticClass()));
	UMaterial * Material = Cast<UMaterial>(InMaterial);

	FString Path = GetResourcePath(Material);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + Path;

	FString FullPathName = Path + InMaterial->GetName();
	// Use Dependency to check if this already exported.
	if (Dependency.DependenciesMaterials.Find(FullPathName) != INDEX_NONE)
	{
		return;
	}
	Dependency.DependenciesMaterials.AddUnique(FullPathName);

	// Material infos
	const FString ShaderPrefix = TEXT("S_");
	FString ShaderName = InMaterial->GetName();
	if (ShaderName.Left(2) == TEXT("M_"))
		ShaderName = ShaderName.Right(ShaderName.Len() - 2);
	ShaderName = ShaderPrefix + ShaderName;
	TArray<FString> Shaders;
	Shaders.Add(ShaderName + TEXT("VS"));
	Shaders.Add(ShaderName + TEXT("PS"));
	Shaders.Add(TEXT(""));
	Shaders.Add(TEXT(""));
	Shaders.Add(TEXT(""));

	// Fixed vertex format temp.
	TArray<FString> VSFormats;
	VSFormats.Add(TEXT("EVSSEG_POSITION"));
	VSFormats.Add(TEXT("EVSSEG_NORMAL"));
	VSFormats.Add(TEXT("EVSSEG_TEXCOORD0"));
	VSFormats.Add(TEXT("EVSSEG_TANGENT"));

	// Fixed instance format temp.
	TArray<FString> InsFormats;
	InsFormats.Add(TEXT("EINSSEG_TRANSITION"));	// Transition
	InsFormats.Add(TEXT("EINSSEG_ROT_SCALE_MAT0"));	// Rot and Scale Mat Row0
	InsFormats.Add(TEXT("EINSSEG_ROT_SCALE_MAT1"));	// Rot and Scale Mat Row1
	InsFormats.Add(TEXT("EINSSEG_ROT_SCALE_MAT2"));	// Rot and Scale Mat Row2

	TArray<FString> RTColors;
	RTColors.Add(TEXT("EPF_RGBA16F"));

	FString RTDepth = TEXT("EPF_DEPTH24_STENCIL8");
	FString BlendMode;
	switch (Material->BlendMode)
	{
	case BLEND_Opaque:
		BlendMode = TEXT("BLEND_MODE_OPAQUE");
		break;
	case BLEND_Masked:
		BlendMode = TEXT("BLEND_MODE_MASK");
		break;
	case BLEND_Translucent:
		BlendMode = TEXT("BLEND_MODE_TRANSLUCENT");
		break;
	case BLEND_Additive:
		BlendMode = TEXT("BLEND_MODE_ADDITIVE");
		break;
	case BLEND_Modulate:
	case BLEND_AlphaComposite:
		UE_LOG(LogTiXExporter, Error, TEXT("  Blend Mode Modulate/AlphaComposite NOT supported."));
		BlendMode = TEXT("BLEND_MODE_TRANSLUCENTs");
		break;
	}
	bool bDepthWrite = Material->BlendMode == BLEND_Opaque || Material->BlendMode == BLEND_Masked;
	bool bDepthTest = true;
	bool bTwoSides = Material->IsTwoSided();

	// output json
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		// output basic info
		JsonObject->SetStringField(TEXT("name"), InMaterial->GetName());
		JsonObject->SetStringField(TEXT("type"), TEXT("material"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Material from TiX exporter."));

		// material info
		TArray< TSharedPtr<FJsonValue> > JShaders, JVSFormats, JInsFormats, JRTColors;
		ConvertToJsonArray(Shaders, JShaders);
		ConvertToJsonArray(VSFormats, JVSFormats);
		ConvertToJsonArray(InsFormats, JInsFormats);
		ConvertToJsonArray(RTColors, JRTColors);
		JsonObject->SetArrayField(TEXT("shaders"), JShaders);
		JsonObject->SetArrayField(TEXT("vs_format"), JVSFormats);
		JsonObject->SetArrayField(TEXT("ins_format"), JInsFormats);
		JsonObject->SetArrayField(TEXT("rt_colors"), JRTColors);

		JsonObject->SetStringField(TEXT("rt_depth"), RTDepth);
		JsonObject->SetStringField(TEXT("blend_mode"), BlendMode);
		JsonObject->SetBoolField(TEXT("depth_write"), bDepthWrite);
		JsonObject->SetBoolField(TEXT("depth_test"), bDepthTest);
		JsonObject->SetBoolField(TEXT("two_sides"), bTwoSides);
		SaveJsonToFile(JsonObject, InMaterial->GetName(), ExportFullPath);
	}
}

void UTiXExporterBPLibrary::ExportTexture(UTexture* InTexture, const FString& InExportPath, FDependency& Dependency)
{
	FString Path = GetResourcePath(InTexture);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + Path;

	FString FullPathName = Path + InTexture->GetName();

	if (!InTexture->IsA(UTexture2D::StaticClass()))
	{
		UE_LOG(LogTiXExporter, Error, TEXT("  Texture other than UTexture2D NOT supported yet."));
		return;
	}

	FString ExtName = TEXT("tga");
	// Use Dependency to check if this already exported.
	if (Dependency.DependenciesTextures.Find(FullPathName) != INDEX_NONE)
	{
		return;
	}

	Dependency.DependenciesTextures.AddUnique(FullPathName);

	UTexture2D * InTexture2D = Cast<UTexture2D>(InTexture);
	FBufferArchive Buffer;
	UExporter::ExportToArchive(InTexture, nullptr, Buffer, *ExtName, 0);

	VerifyOrCreateDirectory(ExportFullPath);
	FString ExportFullPathName = ExportFullPath + InTexture->GetName() + TEXT(".") + ExtName;
	if (Buffer.Num() == 0 || !FFileHelper::SaveArrayToFile(Buffer, *ExportFullPathName))
	{
		UE_LOG(LogTiXExporter, Error, TEXT("Fail to save texture %s"), *FullPathName);
		return;
	}

	// output json
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		// output basic info
		JsonObject->SetStringField(TEXT("name"), InTexture->GetName());
		JsonObject->SetStringField(TEXT("type"), TEXT("texture"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Texture from TiX exporter."));
		JsonObject->SetStringField(TEXT("source"), InTexture->GetName() + TEXT(".") + ExtName);
		JsonObject->SetStringField(TEXT("texture_type"), TEXT("ETT_TEXTURE_2D"));
		JsonObject->SetNumberField(TEXT("srgb"), InTexture->SRGB ? 1 : 0);
		JsonObject->SetNumberField(TEXT("is_normalmap"), InTexture->LODGroup == TEXTUREGROUP_WorldNormalMap ? 1 : 0);
		JsonObject->SetNumberField(TEXT("has_mips"), InTexture->MipGenSettings != TMGS_NoMipmaps ? 1 : 0);

		FString AddressMode;
		switch (InTexture2D->AddressX)
		{
		case TA_Wrap:
			AddressMode = TEXT("ETC_REPEAT");
			break;
		case TA_Clamp:
			AddressMode = TEXT("ETC_CLAMP_TO_EDGE");
			break;
		case TA_Mirror:
			AddressMode = TEXT("ETC_MIRROR");
			break;
		}
		JsonObject->SetStringField(TEXT("address_mode"), AddressMode);

		if (!FMath::IsPowerOfTwo(InTexture2D->GetSizeX()) ||
			!FMath::IsPowerOfTwo(InTexture2D->GetSizeY()))
		{
			UE_LOG(LogTiXExporter, Warning, TEXT("%s size is not Power of Two. %d, %d."), *InTexture->GetName(), InTexture2D->GetSizeX(), InTexture2D->GetSizeY());
		}

		int32 LodBias = InTexture->LODBias;
		JsonObject->SetNumberField(TEXT("lod_bias"), LodBias);
		SaveJsonToFile(JsonObject, InTexture->GetName(), ExportFullPath);
	}
}

void UTiXExporterBPLibrary::ExportInstances(const UStaticMesh * InMesh, const TArray<FTiXInstance>& Instances, const FString& InExportPath, const FString& InLevelName)
{
	FString ExportPath = InExportPath;
	VerifyOrCreateDirectory(ExportPath);
	ExportPath += InLevelName;
	FString MeshPathName = GetResourcePathName(InMesh);

	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	// output basic info
	JsonObject->SetStringField(TEXT("name"), InMesh->GetName() + TEXT("_Ins"));
	JsonObject->SetStringField(TEXT("type"), TEXT("instances"));
	JsonObject->SetNumberField(TEXT("version"), 1);
	FString DescStr = FString::Printf(TEXT("Mesh instances in %s from TiX exporter."), *InLevelName);
	JsonObject->SetStringField(TEXT("desc"), DescStr);
	JsonObject->SetStringField(TEXT("linked_mesh"), MeshPathName + ExtName);
	JsonObject->SetNumberField(TEXT("count"), Instances.Num());


	TArray< TSharedPtr<FJsonValue> > FormatArray;
	FormatArray.Add(MakeShareable(new FJsonValueString(TEXT("EINSSEG_TRANSITION"))));
	FormatArray.Add(MakeShareable(new FJsonValueString(TEXT("EINSSEG_ROT_SCALE_MAT0"))));
	FormatArray.Add(MakeShareable(new FJsonValueString(TEXT("EINSSEG_ROT_SCALE_MAT1"))));
	FormatArray.Add(MakeShareable(new FJsonValueString(TEXT("EINSSEG_ROT_SCALE_MAT2"))));
	JsonObject->SetArrayField(TEXT("ins_format"), FormatArray);

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
	JsonObject->SetArrayField(TEXT("instances"), JMeshInstances);
	
	SaveJsonToFile(JsonObject, InMesh->GetName() + TEXT("_Ins"), ExportPath);
}