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
#include "Engine/ReflectionCapture.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Runtime/Engine/Classes/Animation/AnimSingleNodeInstance.h"
#include "Runtime/Engine/Classes/Engine/SkyLight.h"
#include "Runtime/Engine/Classes/Components/SkyLightComponent.h"
#include "Editor/UnrealEd/Classes/Factories/TextureFactory.h"
#include "Engine/Classes/Engine/MapBuildDataRegistry.h"
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
#include "Rendering/SkeletalMeshRenderData.h"
#include "RawMesh.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include "ImageUtils.h"
#include "Runtime/Engine/Classes/Exporters/Exporter.h"
#include "TiXExporterHelper.h"
#include "FTiXMeshCluster.h"

DEFINE_LOG_CATEGORY(LogTiXExporter);


static FTiXExporterSetting TiXExporterSetting;


void UTiXExporterBPLibrary::SetTileSize(float TileSize)
{
	TiXExporterSetting.TileSize = TileSize;
}

void UTiXExporterBPLibrary::SetMeshVertexPositionScale(float MeshVertexPositionScale)
{
	TiXExporterSetting.MeshVertexPositionScale = MeshVertexPositionScale;
}

void UTiXExporterBPLibrary::SetIgnoreMaterial(bool bIgnore)
{
	TiXExporterSetting.bIgnoreMaterial = bIgnore;
}

void UTiXExporterBPLibrary::SetEnableMeshCluster(bool bEnable)
{
	TiXExporterSetting.bEnableMeshCluster = bEnable;
}

void UTiXExporterBPLibrary::SetMeshClusterSize(int32 Triangles)
{
	TiXExporterSetting.MeshClusterSize = Triangles;
}


const FString ExtName = TEXT(".tasset");
const int32 MaxTextureSize = 1024;

inline FIntPoint GetPointByPosition(const FVector& Position, float TileSize)
{
	float X = Position.X / TileSize;
	float Y = Position.Y / TileSize;
	if (X < 0.f)
	{
		X -= 1.f;
	}
	if (Y < 0.f)
	{
		Y -= 1.f;
	}
	return FIntPoint(int32(X), int32(Y));
}

UTiXExporterBPLibrary::UTiXExporterBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UTiXExporterBPLibrary::ExportCurrentScene(
	AActor * Actor, 
	const FString& ExportPath, 
	const TArray<FString>& SceneComponents, 
	const TArray<FString>& MeshComponents)
{
	UWorld * CurrentWorld = Actor->GetWorld();
	ULevel * CurrentLevel = CurrentWorld->GetCurrentLevel();

	TMap<UStaticMesh *, TArray<FTiXInstance> > SMInstances;
	TMap<USkeletalMesh*, TArray<ASkeletalMeshActor*> > SKMActors;
	TMap<USkeletalMesh*, UAnimationAsset* > RelatedAnimations;

	TArray<AActor*> Actors;
	int32 a = 0;
	UE_LOG(LogTiXExporter, Log, TEXT("Export tix scene ..."));

	// Collect Static Meshes
	if (ContainComponent(SceneComponents, TEXT("STATIC_MESH")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT("  Static mesh actors..."));
		UGameplayStatics::GetAllActorsOfClass(Actor, AStaticMeshActor::StaticClass(), Actors);
		for (auto A : Actors)
		{
			if (A->IsHidden())
				continue;
			UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
			AStaticMeshActor * SMActor = static_cast<AStaticMeshActor*>(A);
			UStaticMesh * StaticMesh = SMActor->GetStaticMeshComponent()->GetStaticMesh();

			TArray<FTiXInstance>& Instances = SMInstances.FindOrAdd(StaticMesh);
			FTiXInstance InstanceInfo;
			InstanceInfo.Position = SMActor->GetTransform().GetLocation() * TiXExporterSetting.MeshVertexPositionScale;
			InstanceInfo.Rotation = SMActor->GetTransform().GetRotation();
			InstanceInfo.Scale = SMActor->GetTransform().GetScale3D();
			InstanceInfo.Transform = SMActor->GetTransform();
			Instances.Add(InstanceInfo);
		}
	}

	// Collect Skeletal Meshes
	if (ContainComponent(SceneComponents, TEXT("SKELETAL_MESH")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Skeletal mesh actors..."));
		Actors.Empty();
		UGameplayStatics::GetAllActorsOfClass(Actor, ASkeletalMeshActor::StaticClass(), Actors);
		for (auto A : Actors)
		{
			if (A->IsHidden())
				continue;
			UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());

			ASkeletalMeshActor* SKMActor = static_cast<ASkeletalMeshActor*>(A);
			USkeletalMesh* SkeletalMesh = SKMActor->GetSkeletalMeshComponent()->SkeletalMesh;

			if (SKMActor->GetSkeletalMeshComponent()->GetAnimationMode() == EAnimationMode::AnimationSingleNode)
			{
				// If Use Animation Asset, Export UAnimationAsset
				UAnimSingleNodeInstance* SingleNodeInstance = SKMActor->GetSkeletalMeshComponent()->GetSingleNodeInstance();
				UAnimationAsset* AnimAsset = SingleNodeInstance->CurrentAsset;
				if (AnimAsset->IsA<UAnimSequence>())
				{
					RelatedAnimations.FindOrAdd(SkeletalMesh) = AnimAsset;
				}
			}

			TArray<ASkeletalMeshActor*>& TileActors = SKMActors.FindOrAdd(SkeletalMesh);
			TileActors.Add(SKMActor);
		}
	}

	// Collect Foliages
	if (ContainComponent(SceneComponents, TEXT("FOLIAGE_AND_GRASS")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Foliage and grass  actors..."));
		Actors.Empty();
		UGameplayStatics::GetAllActorsOfClass(Actor, AInstancedFoliageActor::StaticClass(), Actors);
		for (auto A : Actors)
		{
			if (A->IsHidden())
				continue;
			UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
			AInstancedFoliageActor * FoliageActor = (AInstancedFoliageActor*)A;
			for (const auto& FoliagePair : FoliageActor->FoliageInfos)
			{
				const FFoliageInfo& FoliageInfo = *FoliagePair.Value;

				UHierarchicalInstancedStaticMeshComponent* MeshComponent = FoliageInfo.GetComponent();
				TArray<FInstancedStaticMeshInstanceData> MeshDataArray = MeshComponent->PerInstanceSMData;

				UStaticMesh * StaticMesh = MeshComponent->GetStaticMesh();
				TArray<FTiXInstance>& Instances = SMInstances.FindOrAdd(StaticMesh);

				for (auto& MeshMatrix : MeshDataArray)
				{
					FTransform MeshTransform = FTransform(MeshMatrix.Transform);
					FTiXInstance InstanceInfo;
					InstanceInfo.Position = MeshTransform.GetLocation() * TiXExporterSetting.MeshVertexPositionScale;
					InstanceInfo.Rotation = MeshTransform.GetRotation();
					InstanceInfo.Scale = MeshTransform.GetScale3D();
					InstanceInfo.Transform = MeshTransform;
					Instances.Add(InstanceInfo);
				}
			}
		}
	}

	// Collect Sky light
	TArray< ASkyLight* > SkyLightActors;
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Sky light actors..."));
		Actors.Empty();
		UGameplayStatics::GetAllActorsOfClass(Actor, ASkyLight::StaticClass(), Actors);
		for (auto A : Actors)
		{
			if (A->IsHidden())
				continue;
			UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
			ASkyLight* SkyLightActor = static_cast<ASkyLight*>(A);
			SkyLightActors.Add(SkyLightActor);
		}
	}

	// Collect Reflection Captures
	TArray< AReflectionCapture* > RCActors;
	{
		UE_LOG(LogTiXExporter, Log, TEXT(" Reflection capture actors..."));
		Actors.Empty();
		UGameplayStatics::GetAllActorsOfClass(Actor, AReflectionCapture::StaticClass(), Actors);
		for (auto A : Actors)
		{
			if (A->IsHidden())
				continue;
			UE_LOG(LogTiXExporter, Log, TEXT(" Actor %d : %s."), a++, *A->GetName());
			AReflectionCapture* RCActor = static_cast<AReflectionCapture*>(A);
			RCActors.Add(RCActor);
		}
	}

	// Export mesh resources
	if (ContainComponent(SceneComponents, TEXT("STATIC_MESH")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT("  Static meshes..."));

		for (auto& MeshPair : SMInstances)
		{
			UStaticMesh * Mesh = MeshPair.Key;
			ExportStaticMeshFromRenderData(Mesh, ExportPath, MeshComponents);
		}
	}
	if (ContainComponent(SceneComponents, TEXT("SKELETAL_MESH")))
	{
		UE_LOG(LogTiXExporter, Log, TEXT("  Skeletal meshes..."));
		for (auto& MeshPair : SKMActors)
		{
			USkeletalMesh* SkeletalMesh = MeshPair.Key;
			ExportSkeletalMeshFromRenderData(SkeletalMesh, ExportPath, MeshComponents);
		}

		UE_LOG(LogTiXExporter, Log, TEXT("  Related Animations..."));
		for (auto& AnimPair : RelatedAnimations)
		{
			USkeletalMesh* SkeletalMesh = AnimPair.Key;
			UAnimationAsset* AnimAsset = AnimPair.Value;
			ExportAnimationAsset(AnimAsset, ExportPath);
		}
	}
	
	UE_LOG(LogTiXExporter, Log, TEXT("Scene structure: "));
	// Calc total static mesh instances
	int32 NumSMInstances = 0;
	for (const auto& MeshPair : SMInstances)
	{
		const UStaticMesh * Mesh = MeshPair.Key;
		FString MeshName = Mesh->GetName();
		const TArray<FTiXInstance>& Instances = MeshPair.Value;

		UE_LOG(LogTiXExporter, Log, TEXT("  %s : %d instances."), *MeshName, Instances.Num());
		NumSMInstances += Instances.Num();
	}
	// Calc total skeletal mesh actors
	int32 NumSKMActors = 0;
	for (const auto& MeshPair : SKMActors)
	{
		const USkeletalMesh* Mesh = MeshPair.Key;
		FString MeshName = Mesh->GetName();
		const TArray<ASkeletalMeshActor*>& _Actors = MeshPair.Value;

		UE_LOG(LogTiXExporter, Log, TEXT("  %s : %d actors."), *MeshName, _Actors.Num());
		NumSKMActors += _Actors.Num();
	}


	TMap< FIntPoint, FTiXSceneTile> Tiles;
	// Sort static mesh into scene tiles
	for (const auto& MeshPair : SMInstances)
	{
		UStaticMesh * Mesh = MeshPair.Key;
		const TArray<FTiXInstance>& Instances = MeshPair.Value;

		for (const auto& Ins : Instances)
		{
			if (FMath::IsNaN(Ins.Position.X) ||
				FMath::IsNaN(Ins.Position.Y) ||
				FMath::IsNaN(Ins.Position.Z))
			{
				continue;
			}
			FIntPoint InsPoint = GetPointByPosition(Ins.Position, TiXExporterSetting.TileSize);
			FTiXSceneTile& Tile = Tiles.FindOrAdd(InsPoint);

			Tile.Position = InsPoint;
			Tile.TileSize = TiXExporterSetting.TileSize;

			// Add instances
			TArray<FTiXInstance>& TileInstances = Tile.TileSMInstances.FindOrAdd(Mesh);
			TileInstances.Add(Ins);

			// Add instances count
			++Tile.SMInstanceCount;

			// Recalc bounding box of this tile
			FBox MeshBBox = Mesh->GetBoundingBox();

			FBox TranslatedBox = MeshBBox.TransformBy(Ins.Transform);
			TranslatedBox.Min *= TiXExporterSetting.MeshVertexPositionScale;
			TranslatedBox.Max *= TiXExporterSetting.MeshVertexPositionScale;

			if (Tile.BBox.Min == FVector::ZeroVector && Tile.BBox.Max == FVector::ZeroVector)
			{
				Tile.BBox = TranslatedBox;
			}
			else
			{
				Tile.BBox += TranslatedBox;
			}
		}
	}
	// Sort skeletal mesh into scene tiles
	for (const auto& MeshPair : SKMActors)
	{
		USkeletalMesh* Mesh = MeshPair.Key;
		const TArray<ASkeletalMeshActor*>& _Actors = MeshPair.Value;

		for (const auto& A : _Actors)
		{
			FVector Position = A->GetTransform().GetLocation() * TiXExporterSetting.MeshVertexPositionScale;
			if (FMath::IsNaN(Position.X) ||
				FMath::IsNaN(Position.Y) ||
				FMath::IsNaN(Position.Z))
			{
				continue;
			}
			FIntPoint InsPoint = GetPointByPosition(Position, TiXExporterSetting.TileSize);
			FTiXSceneTile& Tile = Tiles.FindOrAdd(InsPoint);

			Tile.Position = InsPoint;
			Tile.TileSize = TiXExporterSetting.TileSize;

			// Add instances
			TArray<ASkeletalMeshActor*>& TileActors = Tile.TileSKMActors.FindOrAdd(Mesh);
			TileActors.Add(A);

			// Add instances count
			++Tile.SKMActorCount;

			// Recalc bounding box of this tile
			FBox MeshBBox = Mesh->GetImportedBounds().GetBox();
			FBox TranslatedBox = MeshBBox.TransformBy(A->GetTransform());
			TranslatedBox.Min *= TiXExporterSetting.MeshVertexPositionScale;
			TranslatedBox.Max *= TiXExporterSetting.MeshVertexPositionScale;

			if (Tile.BBox.Min == FVector::ZeroVector && Tile.BBox.Max == FVector::ZeroVector)
			{
				Tile.BBox = TranslatedBox;
			}
			else
			{
				Tile.BBox += TranslatedBox;
			}
		}
	}

	// Export reflection captures's ibl cube maps
	FString UpdateReason = TEXT("all levels");
	UReflectionCaptureComponent::UpdateReflectionCaptureContents(CurrentWorld, *UpdateReason, true);
	for (auto RCActor : RCActors)
	{
		FString ActorName = RCActor->GetName();
		ExportReflectionCapture(RCActor, ExportPath);
	}

	// Sort reflection capture actors into scene tiles
	for (auto RCActor : RCActors)
	{
		FVector Position = RCActor->GetTransform().GetLocation()* TiXExporterSetting.MeshVertexPositionScale;

		FIntPoint InsPoint = GetPointByPosition(Position, TiXExporterSetting.TileSize);
		FTiXSceneTile& Tile = Tiles.FindOrAdd(InsPoint);

		Tile.Position = InsPoint;
		Tile.TileSize = TiXExporterSetting.TileSize;

		// Add reflection capture actor
		Tile.ReflectionCaptures.Add(RCActor);
	}

	// output json
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		// output basic info
		JsonObject->SetStringField(TEXT("name"), CurrentWorld->GetName());
		JsonObject->SetStringField(TEXT("type"), TEXT("scene"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Scene tiles information from TiX exporter."));
		JsonObject->SetNumberField(TEXT("static_mesh_total"), SMInstances.Num());
		JsonObject->SetNumberField(TEXT("sm_instances_total"), NumSMInstances);
		JsonObject->SetNumberField(TEXT("skm_actors_total"), NumSKMActors);

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

				CamLocation *= TiXExporterSetting.MeshVertexPositionScale;
				CamTarget *= TiXExporterSetting.MeshVertexPositionScale;

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
		//TODO: Export mainlight and skylight to tiles
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
		TArray<AActor*> SkyLights;
		UGameplayStatics::GetAllActorsOfClass(Actor, ASkyLight::StaticClass(), SkyLights);
		if (SkyLights.Num() > 0)
		{
			USkyLightComponent::UpdateSkyCaptureContents(CurrentWorld);
			// Only export 1 sky light
			TSharedPtr<FJsonObject> JSkyLight = MakeShareable(new FJsonObject);
			auto A = SkyLights[0];
			{
				ASkyLight* SkyLight = static_cast<ASkyLight*>(A);
				USkyLightComponent* LightComponent = SkyLight->GetLightComponent();

				JSkyLight->SetStringField(TEXT("name"), SkyLight->GetName());

				FSHVectorRGB3 IrradianceEnvironmentMap = LightComponent->GetIrradianceEnvironmentMap();

				TArray< TSharedPtr<FJsonValue> > JIrrEnvMap;
				ConvertToJsonArray(IrradianceEnvironmentMap, JIrrEnvMap);

				JSkyLight->SetArrayField(TEXT("irradiance_sh3"), JIrrEnvMap);
			}
			JEnvironment->SetObjectField(TEXT("sky_light"), JSkyLight);
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
					ConvertToJsonArray(LandscapeActor->GetTransform().GetLocation() * TiXExporterSetting.MeshVertexPositionScale, JPosition);
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
						UTexture2D * HeightmapTexture = LandscapeComponent->GetHeightmap();
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

		// Output tiles
		{
			// Output tile refs to scene
			TArray< TSharedPtr<FJsonValue> > JTiles;
			for (const auto& Tile : Tiles)
			{
				const FIntPoint& TilePos = Tile.Key;
				const FTiXSceneTile& SceneTile = Tile.Value;

				ExportSceneTile(SceneTile, CurrentWorld->GetName(), ExportPath);

				// Export tile point position
				TArray< TSharedPtr<FJsonValue> > JPosition;
				ConvertToJsonArray(TilePos, JPosition);
				//FString TilePathName = FString::Printf(TEXT("%s/t%d_%d.tasset"), *CurrentWorld->GetName(), TilePos.X, TilePos.Y);
				TSharedRef< FJsonValueArray > JsonValue = MakeShareable(new FJsonValueArray(JPosition));
				JTiles.Add(JsonValue);
			}
			JsonObject->SetArrayField(TEXT("tiles"), JTiles);
		}


		SaveJsonToFile(JsonObject, CurrentWorld->GetName(), ExportPath);
	}
	SMInstances.Empty();
}

void UTiXExporterBPLibrary::ExportStaticMeshActor(AStaticMeshActor * StaticMeshActor, FString ExportPath, const TArray<FString>& Components)
{
	ExportStaticMesh(StaticMeshActor->GetStaticMeshComponent()->GetStaticMesh(), ExportPath, Components);
}

void UTiXExporterBPLibrary::ExportStaticMesh(UStaticMesh * StaticMesh, FString ExportPath, const TArray<FString>& Components)
{
	ExportStaticMeshFromRenderData(StaticMesh, ExportPath, Components);
}

void GenerateMeshCluster(const TArray<FTiXVertex>& InVertices, const TArray<int32>& InIndices, TArray< TSharedPtr<FJsonValue> >& OutJClusters)
{
	FTiXMeshCluster MeshCluster(InVertices, InIndices, 1.f / TiXExporterSetting.MeshVertexPositionScale);
	MeshCluster.GenerateCluster(TiXExporterSetting.MeshClusterSize);

	TSharedPtr<FJsonObject> JClusters = MakeShareable(new FJsonObject);
	JClusters->SetNumberField(TEXT("cluster_count"), MeshCluster.Clusters.Num());
	JClusters->SetNumberField(TEXT("cluster_size"), TiXExporterSetting.MeshClusterSize);

	for (const auto& C : MeshCluster.Clusters)
	{
		TArray< TSharedPtr<FJsonValue> > ClusterIndicesArray;
		ConvertToJsonArray(C, ClusterIndicesArray);

		TSharedRef< FJsonValueArray > JsonValueArray = MakeShareable(new FJsonValueArray(ClusterIndicesArray));
		OutJClusters.Add(JsonValueArray);
	}
}

void UTiXExporterBPLibrary::ExportStaticMeshFromRenderData(UStaticMesh* StaticMesh, const FString& InExportPath, const TArray<FString>& Components)
{
	FString SMPath = GetResourcePath(StaticMesh);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + SMPath;

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

#define RE_GATHER_VERTEX 0
	// data container
#if RE_GATHER_VERTEX
	TArray<FTiXVertex> VertexData;
	TArray<int32> IndexData;
	TMap<FTiXVertex, int32> IndexMap;

	VertexData.Empty(LODResource.VertexBuffers.PositionVertexBuffer.GetNumVertices());
#else
	TArray<FTiXVertex> VertexData;
	TArray<uint32> IndexData = MeshIndices;

	VertexData.AddZeroed(LODResource.VertexBuffers.PositionVertexBuffer.GetNumVertices());
#endif
	TArray<FTiXMeshSection> MeshSections;

	TArray< TSharedPtr<FJsonValue> > JsonSections;
	for (int32 Section = 0; Section < LODResource.Sections.Num(); ++Section)
	{
		FStaticMeshSection& MeshSection = LODResource.Sections[Section];

		const int32 TotalFaces = MeshSection.NumTriangles;
		const int32 MinVertexIndex = MeshSection.MinVertexIndex;
		const int32 MaxVertexIndex = MeshSection.MaxVertexIndex;
		const int32 FirstIndex = MeshSection.FirstIndex;

		// Remember this section
		FTiXMeshSection TiXSection;
		TiXSection.NumTriangles = MeshSection.NumTriangles;
#if RE_GATHER_VERTEX
		TiXSection.IndexStart = IndexData.Num();
#else
		TiXSection.IndexStart = FirstIndex;
#endif
		MeshSections.Add(TiXSection);

		// Dump section name and material
		FString MaterialInstancePathName, MaterialSlotName;

		if (TiXExporterSetting.bIgnoreMaterial)
		{
			MaterialInstancePathName = TEXT("DebugMaterial");
			MaterialSlotName = TEXT("DebugMaterialName");
		}
		else
		{
			MaterialInstancePathName = GetResourcePath(StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface);
			MaterialInstancePathName += StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface->GetName();
			MaterialSlotName = StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialSlotName.ToString();
			ExportMaterialInstance(StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface, InExportPath);
		}

		// Collect vertices and indices
		const int32 MaxIndex = FirstIndex + TotalFaces * 3;
		for (int32 ii = FirstIndex; ii < MaxIndex; ++ii)
		{
			uint32 Index = MeshIndices[ii];
			check(Index < (uint32)VertexData.Num());

			FTiXVertex Vertex;
			Vertex.Position = PositionVertexBuffer.VertexPosition(Index) * TiXExporterSetting.MeshVertexPositionScale;
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

#if RE_GATHER_VERTEX
			// gather vertices and indices
			int32 * VertexIndex = IndexMap.Find(Vertex);
			if (VertexIndex == nullptr)
			{
				// Add a new vertex to vertex buffer
				VertexData.Add(Vertex);
				int32 CurrentIndex = VertexData.Num() - 1;
				IndexData.Add(CurrentIndex);
				IndexMap.Add(Vertex, CurrentIndex);
			}
			else
			{
				// Add an exist vertex's index
				IndexData.Add(*VertexIndex);
			}
#else
			VertexData[Index] = Vertex;
#endif
		}

		TSharedPtr<FJsonObject> JSection = SaveMeshSectionToJson(TiXSection, MaterialSlotName, MaterialInstancePathName + ExtName);

		// Disable mesh cluster generate in UE4. Make this happen in converter.
		if (false && TiXExporterSetting.bEnableMeshCluster)
		{
			//UE_LOG(LogTiXExporter, Log, TEXT("Generate clusters for [%s]."), *StaticMesh->GetName());
			//TArray< TSharedPtr<FJsonValue> > JClusters;
			//GenerateMeshCluster(VertexData, IndexData, JClusters);
			//JSection->SetArrayField("clusters", JClusters);
		}

		TSharedRef< FJsonValueObject > JsonSectionValue = MakeShareable(new FJsonValueObject(JSection));
		JsonSections.Add(JsonSectionValue);
	}

	// Export mesh data
	TSharedPtr<FJsonObject> JMeshData = SaveMeshDataToJson(VertexData, IndexData, VsFormat);

	// Export collision infos
	TSharedPtr<FJsonObject> JCollisions = ExportMeshCollisions(StaticMesh);

	// output json
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		// output basic info
		JsonObject->SetStringField(TEXT("name"), StaticMesh->GetName());
		JsonObject->SetStringField(TEXT("type"), TEXT("static_mesh"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Static mesh (Render Resource) from TiX exporter."));
		JsonObject->SetNumberField(TEXT("vertex_count_total"), VertexData.Num());
			//LODResource.VertexBuffers.PositionVertexBuffer.GetNumVertices());
		JsonObject->SetNumberField(TEXT("index_count_total"), IndexData.Num());
			//MeshIndices.Num());
		JsonObject->SetNumberField(TEXT("texcoord_count"), TotalNumTexCoords);
		JsonObject->SetNumberField(TEXT("total_lod"), 1);

		// output mesh data
		JsonObject->SetObjectField(TEXT("data"), JMeshData);

		// output mesh sections
		JsonObject->SetArrayField(TEXT("sections"), JsonSections);

		// output mesh collisions
		JsonObject->SetObjectField(TEXT("collisions"), JCollisions);

		SaveJsonToFile(JsonObject, StaticMesh->GetName(), ExportFullPath);
	}
}

void UTiXExporterBPLibrary::ExportSkeletalMeshFromRenderData(USkeletalMesh* SkeletalMesh, FString InExportPath, const TArray<FString>& Components)
{
	FString SMPath = GetResourcePath(SkeletalMesh);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + SMPath;

	FSkeletalMeshRenderData* SKMRenderData = SkeletalMesh->GetResourceForRendering();
	const int32 TotalLODs = SKMRenderData->LODRenderData.Num();

	USkeleton* Skeleton = SkeletalMesh->Skeleton;
	FString SkeletonPath = GetResourcePath(Skeleton) + Skeleton->GetName() + TEXT(".tasset");
	ExportSkeleton(Skeleton, InExportPath);

	// Export LOD0 only for now.
	int32 CurrentLOD = 0;
	FSkeletalMeshLODRenderData& LODResource = SKMRenderData->LODRenderData[CurrentLOD];

	const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LODResource.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FPositionVertexBuffer& PositionVertexBuffer = LODResource.StaticVertexBuffers.PositionVertexBuffer;
	const FColorVertexBuffer& ColorVertexBuffer = LODResource.StaticVertexBuffers.ColorVertexBuffer;
	const FSkinWeightVertexBuffer& SkinWeightVertexBuffer = LODResource.SkinWeightVertexBuffer;
	const int32 TotalNumTexCoords = LODResource.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();

	// Get Vertex format
	uint32 VsFormat = 0;
	if (PositionVertexBuffer.GetNumVertices() > 0 && ContainComponent(Components, (TEXT("POSITION"))))
	{
		VsFormat |= EVSSEG_POSITION;
	}
	else
	{
		UE_LOG(LogTiXExporter, Error, TEXT("Skeletal mesh [%s] do not have position stream."), *SkeletalMesh->GetPathName());
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
	if (SkinWeightVertexBuffer.GetNumVertices() > 0)
	{
		VsFormat |= EVSSEG_BLENDINDEX;
		VsFormat |= EVSSEG_BLENDWEIGHT;

		if (SkinWeightVertexBuffer.GetMaxBoneInfluences() > 4)
		{
			UE_LOG(LogTiXExporter, Warning, TEXT("Skeletal mesh [%s] have max bone influences > 4."), *SkeletalMesh->GetPathName());
		}
	}
	else
	{
		UE_LOG(LogTiXExporter, Error, TEXT("Skeletal mesh [%s] do not have Bone Index & Weight stream."), *SkeletalMesh->GetPathName());
		return;
	}

	TArray<uint32> MeshIndices;
	LODResource.MultiSizeIndexContainer.GetIndexBuffer(MeshIndices);

	// data container
	TArray<FTiXVertex> VertexData;
	TArray<uint32> IndexData = MeshIndices;

	VertexData.AddZeroed(LODResource.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());

	TArray<FTiXMeshSection> MeshSections;

	TArray< TSharedPtr<FJsonValue> > JsonSections;
	for (int32 Section = 0; Section < LODResource.RenderSections.Num(); ++Section)
	{
		FSkelMeshRenderSection& MeshSection = LODResource.RenderSections[Section];

		const int32 TotalFaces = MeshSection.NumTriangles;
		//const int32 MinVertexIndex = MeshSection.MinVertexIndex;
		//const int32 MaxVertexIndex = MeshSection.MaxVertexIndex;
		const int32 FirstIndex = MeshSection.BaseIndex;

		// Remember this section
		FTiXMeshSection TiXSection;
		TiXSection.NumTriangles = MeshSection.NumTriangles;
		TiXSection.IndexStart = FirstIndex;
		MeshSections.Add(TiXSection);

		// Dump section name and material
		FString MaterialInstancePathName, MaterialSlotName;

		if (TiXExporterSetting.bIgnoreMaterial)
		{
			MaterialInstancePathName = TEXT("DebugMaterial");
			MaterialSlotName = TEXT("DebugMaterialName");
		}
		else
		{
			MaterialInstancePathName = GetResourcePath(SkeletalMesh->Materials[MeshSection.MaterialIndex].MaterialInterface);
			MaterialInstancePathName += SkeletalMesh->Materials[MeshSection.MaterialIndex].MaterialInterface->GetName();
			MaterialSlotName = SkeletalMesh->Materials[MeshSection.MaterialIndex].MaterialSlotName.ToString();
			ExportMaterialInstance(SkeletalMesh->Materials[MeshSection.MaterialIndex].MaterialInterface, InExportPath);
		}

		// Collect vertices and indices
		const int32 MaxIndex = FirstIndex + TotalFaces * 3;
		for (int32 ii = FirstIndex; ii < MaxIndex; ++ii)
		{
			uint32 Index = MeshIndices[ii];
			check(Index < (uint32)VertexData.Num());

			FTiXVertex Vertex;
			Vertex.Position = PositionVertexBuffer.VertexPosition(Index) * TiXExporterSetting.MeshVertexPositionScale;
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
			if ((VsFormat & EVSSEG_BLENDINDEX) != 0)
			{
				FSkinWeightInfo Info = SkinWeightVertexBuffer.GetVertexSkinWeights(Index);
				Vertex.BlendIndex[0] = Info.InfluenceBones[0];
				Vertex.BlendIndex[1] = Info.InfluenceBones[1];
				Vertex.BlendIndex[2] = Info.InfluenceBones[2];
				Vertex.BlendIndex[3] = Info.InfluenceBones[3];
				Vertex.BlendWeight[0] = Info.InfluenceWeights[0] / 255.f;
				Vertex.BlendWeight[1] = Info.InfluenceWeights[1] / 255.f;
				Vertex.BlendWeight[2] = Info.InfluenceWeights[2] / 255.f;
				Vertex.BlendWeight[3] = Info.InfluenceWeights[3] / 255.f;
			}

			VertexData[Index] = Vertex;
		}

		TSharedPtr<FJsonObject> JSection = SaveMeshSectionToJson(TiXSection, MaterialSlotName, MaterialInstancePathName + ExtName);

		// Disable mesh cluster generate in UE4. Make this happen in converter.
		if (false && TiXExporterSetting.bEnableMeshCluster)
		{
			//UE_LOG(LogTiXExporter, Log, TEXT("Generate clusters for [%s]."), *StaticMesh->GetName());
			//TArray< TSharedPtr<FJsonValue> > JClusters;
			//GenerateMeshCluster(VertexData, IndexData, JClusters);
			//JSection->SetArrayField("clusters", JClusters);
		}

		TSharedRef< FJsonValueObject > JsonSectionValue = MakeShareable(new FJsonValueObject(JSection));
		JsonSections.Add(JsonSectionValue);
	}

	// Export mesh data
	TSharedPtr<FJsonObject> JMeshData = SaveMeshDataToJson(VertexData, IndexData, VsFormat);

	// Export collision infos
	//TSharedPtr<FJsonObject> JCollisions = ExportMeshCollisions(StaticMesh);

	// output json
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		// output basic info
		JsonObject->SetStringField(TEXT("name"), SkeletalMesh->GetName());
		JsonObject->SetStringField(TEXT("type"), TEXT("skeletal_mesh"));
		JsonObject->SetNumberField(TEXT("version"), 1);
		JsonObject->SetStringField(TEXT("desc"), TEXT("Skeletal mesh (Render Resource) from TiX exporter."));
		JsonObject->SetNumberField(TEXT("vertex_count_total"), VertexData.Num());
		//LODResource.VertexBuffers.PositionVertexBuffer.GetNumVertices());
		JsonObject->SetNumberField(TEXT("index_count_total"), IndexData.Num());
		//MeshIndices.Num());
		JsonObject->SetNumberField(TEXT("texcoord_count"), TotalNumTexCoords);
		JsonObject->SetNumberField(TEXT("total_lod"), 1);
		JsonObject->SetStringField(TEXT("skeleton"), SkeletonPath);

		// output mesh data
		JsonObject->SetObjectField(TEXT("data"), JMeshData);

		// output mesh sections
		JsonObject->SetArrayField(TEXT("sections"), JsonSections);

		// output mesh collisions
		//JsonObject->SetObjectField(TEXT("collisions"), JCollisions);

		SaveJsonToFile(JsonObject, SkeletalMesh->GetName(), ExportFullPath);
	}
}

void UTiXExporterBPLibrary::ExportSkeleton(USkeleton* InSkeleton, const FString& InExportPath)
{
	FString Path = GetResourcePath(InSkeleton);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + Path;

	// Skeleton infos
	const FReferenceSkeleton& RefSkeleton = InSkeleton->GetReferenceSkeleton();
	const int32 RawBoneNum = RefSkeleton.GetRawBoneNum();
	const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRawRefBoneInfo();
	const TArray<FTransform>& BonePoses = RefSkeleton.GetRawRefBonePose();

	FTiXSkeletonAsset SkeletonAsset;
	SkeletonAsset.name = InSkeleton->GetName();
	SkeletonAsset.type = TEXT("skeleton");
	SkeletonAsset.version = 1;
	SkeletonAsset.desc = TEXT("Skeleton from TiX exporter.");
	SkeletonAsset.total_bones = RawBoneNum;
	SkeletonAsset.bones.Reserve(RawBoneNum);

	for (int32 i = 0; i < RawBoneNum; i++)
	{
		const FMeshBoneInfo& Info = BoneInfos[i];
		const FTransform& Trans = BonePoses[i];

		FTiXBoneInfo TiXBoneInfo;
		TiXBoneInfo.index = i;
		TiXBoneInfo.bone_name = Info.Name.ToString();
		TiXBoneInfo.parent_index = Info.ParentIndex;
		FVector Translation = Trans.GetTranslation();
		FQuat Rotation = Trans.GetRotation();
		FVector Scale = Trans.GetScale3D();
		TiXBoneInfo.translation.Add(Translation.X);
		TiXBoneInfo.translation.Add(Translation.Y);
		TiXBoneInfo.translation.Add(Translation.Z);
		TiXBoneInfo.rotation.Add(Rotation.X);
		TiXBoneInfo.rotation.Add(Rotation.Y);
		TiXBoneInfo.rotation.Add(Rotation.Z);
		TiXBoneInfo.rotation.Add(Rotation.W);
		TiXBoneInfo.scale.Add(Scale.X);
		TiXBoneInfo.scale.Add(Scale.Y);
		TiXBoneInfo.scale.Add(Scale.Z);

		SkeletonAsset.bones.Add(TiXBoneInfo);
	}

	FString JsonStr;
	FJsonObjectConverter::UStructToJsonObjectString(SkeletonAsset, JsonStr);
	SaveJsonToFile(JsonStr, InSkeleton->GetName(), *ExportFullPath);
}

void UTiXExporterBPLibrary::ExportAnimationAsset(UAnimationAsset* InAnimAsset, FString InExportPath)
{
	FString Path = GetResourcePath(InAnimAsset);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + Path;

	USkeleton* Skeleton = InAnimAsset->GetSkeleton();
	FString SkeletonPath = GetResourcePath(Skeleton) + Skeleton->GetName() + TEXT(".tasset");
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRawRefBoneInfo();

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(InAnimAsset);
	const int32 NumFrames = AnimSequence->GetRawNumberOfFrames();
	const float SequenceLength = AnimSequence->SequenceLength;
	const float RateScale = AnimSequence->RateScale;
	const TArray<FRawAnimSequenceTrack>& AnimData = AnimSequence->GetRawAnimationData();
	const TArray<FTrackToSkeletonMap>& TrackToSkeMap = AnimSequence->GetRawTrackToSkeletonMapTable();

	check(BoneInfos.Num() == AnimData.Num());
	check(BoneInfos.Num() == TrackToSkeMap.Num());

	FTiXAnimationAsset AnimAsset;
	AnimAsset.name = InAnimAsset->GetName();
	AnimAsset.type = TEXT("animation");
	AnimAsset.version = 1;
	AnimAsset.desc = TEXT("Anim Sequence from TiX exporter.");
	AnimAsset.total_frames = NumFrames;
	AnimAsset.sequence_length = SequenceLength;
	AnimAsset.rate_scale = RateScale;
	AnimAsset.total_tracks = AnimData.Num();
	AnimAsset.ref_skeleton = SkeletonPath;
	AnimAsset.tracks.Reserve(AnimData.Num());

	for (int32 i = 0; i < AnimData.Num(); i++)
	{
		FTiXTrackInfo TrackInfo;
		TrackInfo.index = i;
		int32 BoneIndex = TrackToSkeMap[i].BoneTreeIndex;
		TrackInfo.ref_bone_index = BoneIndex;
		TrackInfo.ref_bone = BoneInfos[BoneIndex].Name.ToString();

		check(AnimData[i].PosKeys.Num() == 0 || AnimData[i].PosKeys.Num() == 1 || AnimData[i].PosKeys.Num() == NumFrames);
		check(AnimData[i].RotKeys.Num() == 0 || AnimData[i].RotKeys.Num() == 1 || AnimData[i].RotKeys.Num() == NumFrames);
		check(AnimData[i].ScaleKeys.Num() == 0 || AnimData[i].ScaleKeys.Num() == 1 || AnimData[i].ScaleKeys.Num() == NumFrames);
		
		TrackInfo.pos_keys.Reserve(AnimData[i].PosKeys.Num() * 3);
		for (const auto& K : AnimData[i].PosKeys)
		{
			TrackInfo.pos_keys.Add(K.X);
			TrackInfo.pos_keys.Add(K.Y);
			TrackInfo.pos_keys.Add(K.Z);
		}
		TrackInfo.rot_keys.Reserve(AnimData[i].RotKeys.Num() * 4);
		for (const auto& K : AnimData[i].RotKeys)
		{
			TrackInfo.rot_keys.Add(K.X);
			TrackInfo.rot_keys.Add(K.Y);
			TrackInfo.rot_keys.Add(K.Z);
			TrackInfo.rot_keys.Add(K.W);
		}
		TrackInfo.scale_keys.Reserve(AnimData[i].ScaleKeys.Num() * 3);
		for (const auto& K : AnimData[i].ScaleKeys)
		{
			TrackInfo.scale_keys.Add(K.X);
			TrackInfo.scale_keys.Add(K.Y);
			TrackInfo.scale_keys.Add(K.Z);
		}

		AnimAsset.tracks.Add(TrackInfo);
	}

	FString JsonStr;
	FJsonObjectConverter::UStructToJsonObjectString(AnimAsset, JsonStr);
	SaveJsonToFile(JsonStr, InAnimAsset->GetName(), *ExportFullPath);
}

TSharedPtr<FJsonObject> UTiXExporterBPLibrary::ExportMeshCollisions(const UStaticMesh * InMesh)
{
	UBodySetup * BodySetup = InMesh->BodySetup;
	const FKAggregateGeom& AggregateGeom = BodySetup->AggGeom;

	TSharedPtr<FJsonObject> JCollisions = MakeShareable(new FJsonObject);
	TArray< TSharedPtr<FJsonValue> > JSpheres, JBoxes, JCapsules, JConvexes;

	// Spheres
	const TArray<FKSphereElem>& SphereElements = AggregateGeom.SphereElems;
	for (const auto& Sphere : SphereElements)
	{
		TSharedPtr<FJsonObject> JSphereCollision = MakeShareable(new FJsonObject);

		FVector SphereCenter = Sphere.Center * TiXExporterSetting.MeshVertexPositionScale;
		float Radius = Sphere.Radius * TiXExporterSetting.MeshVertexPositionScale;

		TArray< TSharedPtr<FJsonValue> > JCenter;
		ConvertToJsonArray(SphereCenter, JCenter);
		JSphereCollision->SetArrayField(TEXT("center"), JCenter);
		JSphereCollision->SetNumberField(TEXT("radius"), Radius);

		TSharedRef< FJsonValueObject > JSphereValue = MakeShareable(new FJsonValueObject(JSphereCollision));
		JSpheres.Add(JSphereValue);
	}

	// Boxes
	const TArray<FKBoxElem>& BoxElements = AggregateGeom.BoxElems;
	for (const auto& Box : BoxElements)
	{
		TSharedPtr<FJsonObject> JBoxCollision = MakeShareable(new FJsonObject);

		FVector BoxCenter = Box.Center * TiXExporterSetting.MeshVertexPositionScale;
		FRotator BoxRotation = Box.Rotation;
		FQuat BoxQuat = FQuat(Box.Rotation);
		float BoxX = Box.X * TiXExporterSetting.MeshVertexPositionScale;
		float BoxY = Box.Y * TiXExporterSetting.MeshVertexPositionScale;
		float BoxZ = Box.Z * TiXExporterSetting.MeshVertexPositionScale;

		TArray< TSharedPtr<FJsonValue> > JCenter, JRotator, JQuat;
		ConvertToJsonArray(BoxCenter, JCenter);
		ConvertToJsonArray(BoxRotation, JRotator);
		ConvertToJsonArray(BoxQuat, JQuat);
		JBoxCollision->SetArrayField(TEXT("center"), JCenter);
		JBoxCollision->SetArrayField(TEXT("rotator"), JRotator);
		JBoxCollision->SetArrayField(TEXT("quat"), JQuat);
		JBoxCollision->SetNumberField(TEXT("x"), BoxX);
		JBoxCollision->SetNumberField(TEXT("y"), BoxY);
		JBoxCollision->SetNumberField(TEXT("z"), BoxZ);

		TSharedRef< FJsonValueObject > JBoxValue = MakeShareable(new FJsonValueObject(JBoxCollision));
		JBoxes.Add(JBoxValue);
	}

	// Capsules
	const TArray<FKSphylElem>& CapsuleElements = AggregateGeom.SphylElems;
	for (const auto& Capsule : CapsuleElements)
	{
		TSharedPtr<FJsonObject> JCapsuleCollision = MakeShareable(new FJsonObject);

		FVector CapsuleCenter = Capsule.Center * TiXExporterSetting.MeshVertexPositionScale;
		FRotator CapsuleRotation = Capsule.Rotation;
		FQuat CapsuleQuat = FQuat(CapsuleRotation);
		float CapsuleRadius = Capsule.Radius * TiXExporterSetting.MeshVertexPositionScale;
		float CapsuleLength = Capsule.Length * TiXExporterSetting.MeshVertexPositionScale;

		TArray< TSharedPtr<FJsonValue> > JCenter, JRotator, JQuat;
		ConvertToJsonArray(CapsuleCenter, JCenter);
		ConvertToJsonArray(CapsuleRotation, JRotator);
		ConvertToJsonArray(CapsuleQuat, JQuat);
		JCapsuleCollision->SetArrayField(TEXT("center"), JCenter);
		JCapsuleCollision->SetArrayField(TEXT("rotator"), JRotator);
		JCapsuleCollision->SetArrayField(TEXT("quat"), JQuat);
		JCapsuleCollision->SetNumberField(TEXT("radius"), CapsuleRadius);
		JCapsuleCollision->SetNumberField(TEXT("length"), CapsuleLength);

		TSharedRef< FJsonValueObject > JCapsuleValue = MakeShareable(new FJsonValueObject(JCapsuleCollision));
		JCapsules.Add(JCapsuleValue);
	}

	// Convex
	const TArray<FKConvexElem>& ConvexElements = AggregateGeom.ConvexElems;
	for (const auto& Convex : ConvexElements)
	{
		TSharedPtr<FJsonObject> JConvexCollision = MakeShareable(new FJsonObject);

		FVector Translation = Convex.GetTransform().GetTranslation() * TiXExporterSetting.MeshVertexPositionScale;
		FQuat Rotation = Convex.GetTransform().GetRotation();
		FVector Scale3D = Convex.GetTransform().GetScale3D();

		// Origin convex data
		TArray<FVector> VertexData = Convex.VertexData;
		for (auto& V : VertexData)
		{
			V *= TiXExporterSetting.MeshVertexPositionScale;
		}
		FBox BBox = Convex.ElemBox;
		BBox.Min *= TiXExporterSetting.MeshVertexPositionScale;
		BBox.Max *= TiXExporterSetting.MeshVertexPositionScale;

		TArray< TSharedPtr<FJsonValue> > JVertexData, JBBox, JTranslation, JQuat, JScale;
		ConvertToJsonArray(VertexData, JVertexData);
		ConvertToJsonArray(BBox, JBBox);
		ConvertToJsonArray(Translation, JTranslation);
		ConvertToJsonArray(Rotation, JQuat);
		ConvertToJsonArray(Scale3D, JScale);
		JConvexCollision->SetArrayField(TEXT("vertex_data"), JVertexData);
		JConvexCollision->SetArrayField(TEXT("bbox"), JBBox);
		JConvexCollision->SetArrayField(TEXT("translation"), JTranslation);
		JConvexCollision->SetArrayField(TEXT("rotation"), JQuat);
		JConvexCollision->SetArrayField(TEXT("scale"), JScale);

		// Cooked physic collision data
		TArray<FDynamicMeshVertex> VertexBuffer;
		TArray<uint32> IndexBuffer;
		Convex.AddCachedSolidConvexGeom(VertexBuffer, IndexBuffer, FColor::White);
		TArray<FVector> VertexPositions;
		for (const auto& Vertex : VertexBuffer)
		{
			VertexPositions.Add(Vertex.Position * TiXExporterSetting.MeshVertexPositionScale);
		}
		TArray< TSharedPtr<FJsonValue> > JCookedVertexData, JCookedIndexData;
		ConvertToJsonArray(VertexPositions, JCookedVertexData);
		ConvertToJsonArray(IndexBuffer, JCookedIndexData);
		JConvexCollision->SetArrayField(TEXT("cooked_mesh_vertex_data"), JCookedVertexData);
		JConvexCollision->SetArrayField(TEXT("cooked_mesh_index_data"), JCookedIndexData);

		TSharedRef< FJsonValueObject > JConvexValue = MakeShareable(new FJsonValueObject(JConvexCollision));
		JConvexes.Add(JConvexValue);
	}

	JCollisions->SetArrayField(TEXT("sphere"), JSpheres);
	JCollisions->SetArrayField(TEXT("box"), JBoxes);
	JCollisions->SetArrayField(TEXT("capsule"), JCapsules);
	JCollisions->SetArrayField(TEXT("convex"), JConvexes);

	return JCollisions;
}

void UTiXExporterBPLibrary::ExportStaticMeshFromRawMesh(UStaticMesh* StaticMesh, const FString& Path, const TArray<FString>& Components)
{
	for (const auto& Model : StaticMesh->GetSourceModels())
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
				Vertex.Position = MeshData.VertexPositions[MeshData.WedgeIndices[IndexOffset + i]] * TiXExporterSetting.MeshVertexPositionScale;
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

			check(0);
			// output mesh sections
			//TArray< TSharedPtr<FJsonValue> > JsonSections;
			//for (int32 section = 0; section < MaterialSections.Num(); ++section)
			//{
			//	TSharedPtr<FJsonObject> JSection = SaveMeshSectionToJson(Vertices[section], Indices[section], Materials[section]->MaterialSlotName.ToString(), Materials[section]->MaterialInterface->GetName() + ExtName, VsFormat);

			//	TSharedRef< FJsonValueObject > JsonSectionValue = MakeShareable(new FJsonValueObject(JSection));
			//	JsonSections.Add(JsonSectionValue);
			//}
			//JsonObject->SetArrayField(TEXT("sections"), JsonSections);

			//SaveJsonToFile(JsonObject, StaticMesh->GetName(), Path);
		}
	}
}

void UTiXExporterBPLibrary::ExportMaterialInstance(UMaterialInterface* InMaterial, const FString& InExportPath)
{
	if (InMaterial->IsA(UMaterial::StaticClass()))
	{
		ExportMaterial(InMaterial, InExportPath);
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

		// Linked Material
		UMaterialInterface * ParentMaterial = MaterialInstance->Parent;
		check(ParentMaterial && ParentMaterial->IsA(UMaterial::StaticClass()));
		ExportMaterial(ParentMaterial, InExportPath);
		FString MaterialPathName = GetResourcePath(ParentMaterial);
		MaterialPathName += ParentMaterial->GetName();

		// Parameters
		// Scalar parameters
		TArray<FVector4> ScalarVectorParams;
		TArray<FString> ScalarVectorNames;
		TArray<FString> ScalarVectorComments;
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
				ScalarVectorComments.Add(FString());
			}
			// Remember value
			ScalarVectorParams[CombinedIndex][IndexInVector4] = ScalarValue.ParameterValue;
			// Remember scalar param name
			FString ScalarParamName = FString::Printf(TEXT("%d = %s; "), IndexInVector4, *ScalarValue.ParameterInfo.Name.ToString());
			ScalarVectorComments[CombinedIndex] += ScalarParamName;
		}

		// Vector parameters.
		for (int32 i = 0; i < MaterialInstance->VectorParameterValues.Num(); ++i)
		{
			const FVectorParameterValue& VectorValue = MaterialInstance->VectorParameterValues[i];

			ScalarVectorParams.Add(FVector4(VectorValue.ParameterValue));
			ScalarVectorNames.Add(VectorValue.ParameterInfo.Name.ToString());
			ScalarVectorComments.Add(VectorValue.ParameterInfo.Name.ToString());
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

			ExportTexture(TextureValue.ParameterValue, InExportPath);
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

				JParameter->SetStringField(TEXT("declare"), ScalarVectorComments[SVParam]);
				
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

void UTiXExporterBPLibrary::ExportMaterial(UMaterialInterface* InMaterial, const FString& InExportPath)
{
	check(InMaterial->IsA(UMaterial::StaticClass()));
	UMaterial * Material = Cast<UMaterial>(InMaterial);

	FString Path = GetResourcePath(Material);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath = ExportPath + Path;

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

void UTiXExporterBPLibrary::ExportTexture(UTexture* InTexture, const FString& InExportPath, bool UsedAsIBL)
{
	if (!InTexture->IsA(UTexture2D::StaticClass()) && !InTexture->IsA(UTextureCube::StaticClass()))
	{
		UE_LOG(LogTiXExporter, Error, TEXT("  Texture other than UTexture2D and UTextureCube are NOT supported yet."));
		return;
	}
	const bool IsTexture2D = InTexture->IsA(UTexture2D::StaticClass());
	UTexture2D* InTexture2D = Cast<UTexture2D>(InTexture);
	UTextureCube* InTextureCube = Cast<UTextureCube>(InTexture);

	FString Path = GetResourcePath(InTexture);
	FString ExportPath = InExportPath;
	ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (ExportPath[ExportPath.Len() - 1] != '/')
		ExportPath.AppendChar('/');
	FString ExportFullPath;
	if (!UsedAsIBL)
		ExportFullPath = ExportPath + Path;
	else
		ExportFullPath = ExportPath;

	// Save texture 2d with tga format and texture cube with hdr format
	FString ImageExtName = IsTexture2D ? TEXT("tga") : TEXT("hdr");
	FString FullPathName = Path + InTexture->GetName();

	FBufferArchive Buffer;
	if (IsTexture2D)
	{
		UExporter::ExportToArchive(InTexture2D, nullptr, Buffer, *ImageExtName, 0);
	}
	else
	{
		UExporter::ExportToArchive(InTextureCube, nullptr, Buffer, *ImageExtName, 0);
	}

	VerifyOrCreateDirectory(ExportFullPath);
	FString ExportFullPathName = ExportFullPath + InTexture->GetName() + TEXT(".") + ImageExtName;
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
		JsonObject->SetStringField(TEXT("source"), InTexture->GetName() + TEXT(".") + ImageExtName);
		JsonObject->SetStringField(TEXT("texture_type"), IsTexture2D ? TEXT("ETT_TEXTURE_2D") : TEXT("ETT_TEXTURE_CUBE"));
		JsonObject->SetNumberField(TEXT("srgb"), InTexture->SRGB ? 1 : 0);
		JsonObject->SetNumberField(TEXT("is_normalmap"), InTexture->LODGroup == TEXTUREGROUP_WorldNormalMap ? 1 : 0);
		JsonObject->SetNumberField(TEXT("has_mips"), InTexture->MipGenSettings != TMGS_NoMipmaps ? 1 : 0);
		JsonObject->SetNumberField(TEXT("ibl"), UsedAsIBL ? 1 : 0);

		// Size
		if (IsTexture2D)
		{
			JsonObject->SetNumberField(TEXT("width"), InTexture2D->GetSizeX());
			JsonObject->SetNumberField(TEXT("height"), InTexture2D->GetSizeY());
			JsonObject->SetNumberField(TEXT("mips"), InTexture2D->GetNumMips());
		}
		else
		{
			JsonObject->SetNumberField(TEXT("width"), InTextureCube->GetSizeX());
			JsonObject->SetNumberField(TEXT("height"), InTextureCube->GetSizeY());
			JsonObject->SetNumberField(TEXT("mips"), InTextureCube->GetNumMips());
		}

		if (IsTexture2D)
		{
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
		}

		int32 LodBias = InTexture->LODBias;
		JsonObject->SetNumberField(TEXT("lod_bias"), LodBias);
		SaveJsonToFile(JsonObject, InTexture->GetName(), ExportFullPath);
	}
}

void UTiXExporterBPLibrary::ExportReflectionCapture(AReflectionCapture* RCActor, const FString& Path)
{
	// Export cubemap data
	UWorld* CurrentWorld = RCActor->GetWorld();
	ULevel* CurrentLevel = CurrentWorld->GetCurrentLevel();
	UReflectionCaptureComponent * RCComponent = RCActor->GetCaptureComponent();

	FReflectionCaptureData ReadbackCaptureData;
	CurrentWorld->Scene->GetReflectionCaptureData(RCComponent, ReadbackCaptureData);
	if (ReadbackCaptureData.CubemapSize > 0)
	{
		UMapBuildDataRegistry* Registry = CurrentLevel->GetOrCreateMapBuildData();
		//if (!RCComponent->bModifyMaxValueRGBM)
		//{
		//	RCComponent->MaxValueRGBM = GetMaxValueRGBM(ReadbackCaptureData.FullHDRCapturedData, ReadbackCaptureData.CubemapSize, ReadbackCaptureData.Brightness);
		//}
		FString TextureName = TEXT("TC_") + RCActor->GetName();
		UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
		TextureFactory->SuppressImportOverwriteDialog();

		TextureFactory->CompressionSettings = TC_HDR;
		UTextureCube* TextureCube = TextureFactory->CreateTextureCube(Registry, FName(TextureName), RF_Standalone | RF_Public);

		if (TextureCube)
		{
			//TArray<uint8> TemporaryEncodedHDRCapturedData;
			//GenerateEncodedHDRData(ReadbackCaptureData.FullHDRCapturedData, ReadbackCaptureData.CubemapSize, ReadbackCaptureData.Brightness, TemporaryEncodedHDRCapturedData);
			const int32 NumMips = FMath::CeilLogTwo(ReadbackCaptureData.CubemapSize) + 1;
			TextureCube->Source.Init(
				ReadbackCaptureData.CubemapSize,
				ReadbackCaptureData.CubemapSize,
				6,
				NumMips,
				TSF_RGBA16F,
				ReadbackCaptureData.FullHDRCapturedData.GetData()
			);
			// the loader can suggest a compression setting
			TextureCube->LODGroup = TEXTUREGROUP_World;

			bool bIsCompressed = false;
			//if (RCComponent != nullptr)
			//{
			//	bIsCompressed = RCComponent->MobileReflectionCompression == EMobileReflectionCompression::Default ? bIsReflectionCaptureCompressionProjectSetting : CaptureComponent->MobileReflectionCompression == EMobileReflectionCompression::On;
			//}

			TextureCube->CompressionSettings = TC_HDR;
			TextureCube->CompressionNone = !bIsCompressed;
			TextureCube->CompressionQuality = TCQ_Highest;
			TextureCube->Filter = TF_Trilinear;
			TextureCube->SRGB = 0;

			// for now we don't support mip map generation on cubemaps
			TextureCube->MipGenSettings = TMGS_LeaveExistingMips;

			TextureCube->UpdateResource();
			TextureCube->MarkPackageDirty();

			FString ExportPath = Path;
			ExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
			if (ExportPath[ExportPath.Len() - 1] != '/')
				ExportPath.AppendChar('/');
			FString MapName = CurrentWorld->GetName();
			FString ExportFullPath = ExportPath + MapName + TEXT("/");
			ExportTexture(TextureCube, ExportFullPath, true);
		}
	}
}

TSharedPtr<FJsonObject> UTiXExporterBPLibrary::ExportStaticMeshInstances(const UStaticMesh * InMesh, const TArray<FTiXInstance>& Instances)
{
	FString MeshPathName = GetResourcePathName(InMesh);

	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	// output basic info
	JsonObject->SetStringField(TEXT("linked_mesh"), MeshPathName + ExtName);

	// only care about LOD 0 for now
	int32 CurrentLOD = 0;
	FStaticMeshLODResources& LODResource = InMesh->RenderData->LODResources[CurrentLOD];
	JsonObject->SetNumberField(TEXT("mesh_sections"), LODResource.Sections.Num());

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
	
	return JsonObject;
}

TSharedPtr<FJsonObject> UTiXExporterBPLibrary::ExportSkeletalMeshActors(const USkeletalMesh* InMesh, const TArray<ASkeletalMeshActor*>& Actors)
{
	FString MeshPathName = GetResourcePathName(InMesh);
	FString SkeletonPathName = GetResourcePathName(InMesh->Skeleton);

	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	// output basic info
	JsonObject->SetStringField(TEXT("linked_skm"), MeshPathName + ExtName);
	JsonObject->SetStringField(TEXT("linked_sk"), SkeletonPathName + ExtName);

	// only care about LOD 0 for now
	int32 CurrentLOD = 0;
	FSkeletalMeshLODRenderData& LODResource = InMesh->GetResourceForRendering()->LODRenderData[CurrentLOD];
	JsonObject->SetNumberField(TEXT("mesh_sections"), LODResource.RenderSections.Num());

	TArray< TSharedPtr<FJsonValue> > JSKMActors;
	for (const auto& A : Actors)
	{
		TSharedPtr<FJsonObject> JActorInfo = MakeShareable(new FJsonObject);
		// Actor animation
		UAnimSingleNodeInstance* SingleNodeInstance = A->GetSkeletalMeshComponent()->GetSingleNodeInstance();
		FString AnimPathName = GetResourcePathName(SingleNodeInstance->CurrentAsset);
		JActorInfo->SetStringField(TEXT("linked_anim"), AnimPathName + ExtName);

		// Actor transform
		const FTransform& Trans = A->GetTransform();
		FVector Position = Trans.GetTranslation() * TiXExporterSetting.MeshVertexPositionScale;
		FQuat Rotation = Trans.GetRotation();
		FVector Scale = Trans.GetScale3D();
		TArray< TSharedPtr<FJsonValue> > JPosition, JRotation, JScale;
		ConvertToJsonArray(Position, JPosition);
		ConvertToJsonArray(Rotation, JRotation);
		ConvertToJsonArray(Scale, JScale);
		JActorInfo->SetArrayField(TEXT("position"), JPosition);
		JActorInfo->SetArrayField(TEXT("rotation"), JRotation);
		JActorInfo->SetArrayField(TEXT("scale"), JScale);

		TSharedRef< FJsonValueObject > JsonActorInfo = MakeShareable(new FJsonValueObject(JActorInfo));
		JSKMActors.Add(JsonActorInfo);
	}
	JsonObject->SetArrayField(TEXT("actors"), JSKMActors);

	return JsonObject;
}

void UTiXExporterBPLibrary::ExportSceneTile(const FTiXSceneTile& SceneTile, const FString& WorldName, const FString& InExportPath)
{
	// Get dependencies
	FDependency Dependency;
	for (const auto& MeshIns : SceneTile.TileSMInstances)
	{
		const UStaticMesh * Mesh = MeshIns.Key;
		GetStaticMeshDependency(Mesh, InExportPath, Dependency);
	}
	for (const auto& MeshActors : SceneTile.TileSKMActors)
	{
		const USkeletalMesh * Mesh = MeshActors.Key;
		GetSkeletalMeshDependency(Mesh, InExportPath, Dependency);

		const TArray<ASkeletalMeshActor*>& TileActors = MeshActors.Value;
		for (const auto& A : TileActors)
		{
			GetAnimSequenceDependency(A, InExportPath, Dependency);
		}
	}

	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	// output basic info
	FString TileName = FString::Printf(TEXT("t%d_%d"), SceneTile.Position.X, SceneTile.Position.Y);
	JsonObject->SetStringField(TEXT("name"), WorldName + TEXT("_") + TileName);
	JsonObject->SetStringField(TEXT("level"), WorldName);
	JsonObject->SetStringField(TEXT("type"), TEXT("scene_tile"));
	JsonObject->SetNumberField(TEXT("version"), 1);
	JsonObject->SetStringField(TEXT("desc"), TEXT("Scene tiles contains mesh instance information from TiX exporter."));

	TArray< TSharedPtr<FJsonValue> > JPosition, JBBox;
	ConvertToJsonArray(SceneTile.Position, JPosition);
	ConvertToJsonArray(SceneTile.BBox, JBBox);
	JsonObject->SetArrayField(TEXT("position"), JPosition);
	JsonObject->SetArrayField(TEXT("bbox"), JBBox);

	// Calculate total mesh sections
	int32 TotalMeshSections = 0;
	const int32 CurrentLOD = 0;
	for (const auto& MeshIns : SceneTile.TileSMInstances)
	{
		const UStaticMesh * Mesh = MeshIns.Key;
		FStaticMeshLODResources& LODResource = Mesh->RenderData->LODResources[CurrentLOD];
		TotalMeshSections += LODResource.Sections.Num();
	}

	// static mesh and instances
	JsonObject->SetNumberField(TEXT("static_mesh_total"), SceneTile.TileSMInstances.Num());
	JsonObject->SetNumberField(TEXT("sm_sections_total"), TotalMeshSections);
	JsonObject->SetNumberField(TEXT("sm_instances_total"), SceneTile.SMInstanceCount);
	JsonObject->SetNumberField(TEXT("texture_total"), Dependency.DependenciesTextures.Num());

	// skeletal mesh and anims
	JsonObject->SetNumberField(TEXT("skeletal_meshes_total"), SceneTile.TileSKMActors.Num());
	JsonObject->SetNumberField(TEXT("skeletons_total"), Dependency.DependenciesSkeletons.Num());
	JsonObject->SetNumberField(TEXT("anims_total"), Dependency.DependenciesAnims.Num());
	JsonObject->SetNumberField(TEXT("skm_actors_total"), SceneTile.SKMActorCount);

	// reflection captures
	JsonObject->SetNumberField(TEXT("reflection_captures_total"), SceneTile.ReflectionCaptures.Num());

	// output reflection captures
	{
		TArray< TSharedPtr<FJsonValue> > JReflectionCaptures;
		for (const auto& RCActor : SceneTile.ReflectionCaptures)
		{
			TSharedPtr<FJsonObject> JRCActor = MakeShareable(new FJsonObject);
			JRCActor->SetStringField(TEXT("name"), RCActor->GetName());
			JRCActor->SetStringField(TEXT("linked_cubemap"), WorldName + TEXT("/TC_") + RCActor->GetName() + TEXT(".tasset"));

			UWorld* CurrentWorld = RCActor->GetWorld();
			UReflectionCaptureComponent* RCComponent = RCActor->GetCaptureComponent();
			FReflectionCaptureData ReadbackCaptureData;
			CurrentWorld->Scene->GetReflectionCaptureData(RCComponent, ReadbackCaptureData);
			JRCActor->SetNumberField(TEXT("cubemap_size"), ReadbackCaptureData.CubemapSize);
			JRCActor->SetNumberField(TEXT("average_brightness"), ReadbackCaptureData.AverageBrightness);
			JRCActor->SetNumberField(TEXT("brightness"), ReadbackCaptureData.Brightness);

			TArray< TSharedPtr<FJsonValue> > JRCPosition;
			ConvertToJsonArray(RCActor->GetTransform().GetLocation(), JRCPosition);
			JRCActor->SetArrayField(TEXT("position"), JRCPosition);

			TSharedRef< FJsonValueObject > JsonValue = MakeShareable(new FJsonValueObject(JRCActor));
			JReflectionCaptures.Add(JsonValue);
		}
		JsonObject->SetArrayField(TEXT("reflection_captures"), JReflectionCaptures);

	}

	// output dependencies
	{
		TSharedPtr<FJsonObject> JDependency = MakeShareable(new FJsonObject);
		TArray< TSharedPtr<FJsonValue> > JTextures, JMaterialInstances, JMaterials, JSMs, JSKMs;
		TArray< TSharedPtr<FJsonValue> > JAnims, JSkeletons;
		// textures
		for (const auto& Tex : Dependency.DependenciesTextures)
		{
			TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Tex + ExtName));
			JTextures.Add(JsonValue);
		}
		JDependency->SetArrayField(TEXT("textures"), JTextures);
		// Materials
		for (const auto& Material : Dependency.DependenciesMaterials)
		{
			TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Material + ExtName));
			JMaterials.Add(JsonValue);
		}
		JDependency->SetArrayField(TEXT("materials"), JMaterials);
		// Material instances
		for (const auto& MaterialInstance : Dependency.DependenciesMaterialInstances)
		{
			TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(MaterialInstance + ExtName));
			JMaterialInstances.Add(JsonValue);
		}
		JDependency->SetArrayField(TEXT("material_instances"), JMaterialInstances);

		// anims
		for (const auto& Anim : Dependency.DependenciesAnims)
		{
			TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Anim + ExtName));
			JAnims.Add(JsonValue);
		}
		JDependency->SetArrayField(TEXT("anims"), JAnims);
		// skeletons
		for (const auto& Sk : Dependency.DependenciesSkeletons)
		{
			TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Sk + ExtName));
			JSkeletons.Add(JsonValue);
		}
		JDependency->SetArrayField(TEXT("skeletons"), JSkeletons);

		// static meshes
		for (const auto& Mesh : Dependency.DependenciesStaticMeshes)
		{
			TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Mesh + ExtName));
			JSMs.Add(JsonValue);
		}
		JDependency->SetArrayField(TEXT("static_meshes"), JSMs);
		// skeletal meshes
		for (const auto& Mesh : Dependency.DependenciesSkeletalMeshes)
		{
			TSharedRef< FJsonValueString > JsonValue = MakeShareable(new FJsonValueString(Mesh + ExtName));
			JSKMs.Add(JsonValue);
		}
		JDependency->SetArrayField(TEXT("skeletal_meshes"), JSKMs);

		JsonObject->SetObjectField(TEXT("dependency"), JDependency);
	}

	// Export mesh instances
	{
		TArray< TSharedPtr<FJsonValue> > JSMInstances;
		for (const auto& MeshIns : SceneTile.TileSMInstances)
		{
			const UStaticMesh * Mesh = MeshIns.Key;
			const TArray< FTiXInstance>& Instances = MeshIns.Value;
			TSharedPtr<FJsonObject> JIns = ExportStaticMeshInstances(Mesh, Instances);
			TSharedRef< FJsonValueObject > JsonValue = MakeShareable(new FJsonValueObject(JIns));
			JSMInstances.Add(JsonValue);
		}
		JsonObject->SetArrayField(TEXT("static_mesh_instances"), JSMInstances);
	}

	// Export skeletal mesh actors
	{
		TArray< TSharedPtr<FJsonValue> > JSKMActors;
		for (const auto& MeshActor : SceneTile.TileSKMActors)
		{
			const USkeletalMesh* Mesh = MeshActor.Key;
			const TArray<ASkeletalMeshActor*>& _Actors = MeshActor.Value;
			TSharedPtr<FJsonObject> JActors = ExportSkeletalMeshActors(Mesh, _Actors);
			TSharedRef< FJsonValueObject > JsonValue = MakeShareable(new FJsonValueObject(JActors));
			JSKMActors.Add(JsonValue);
		}
		JsonObject->SetArrayField(TEXT("skeletal_mesh_actors"), JSKMActors);
	}

	FString FinalExportPath = InExportPath;
	FinalExportPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (FinalExportPath[FinalExportPath.Len() - 1] != '/')
		FinalExportPath.AppendChar('/');
	FinalExportPath += WorldName + TEXT("/");

	SaveJsonToFile(JsonObject, TileName, FinalExportPath);
}

void UTiXExporterBPLibrary::GetStaticMeshDependency(const UStaticMesh * StaticMesh, const FString& InExportPath, FDependency& Dependency)
{
	FString MeshPathName = CombineResourceExportPath(StaticMesh, InExportPath);
	Dependency.DependenciesStaticMeshes.AddUnique(MeshPathName);

	if (TiXExporterSetting.bIgnoreMaterial)
	{
		// Ignore materials, do not output dependency
		return;
	}

	// Add material instance
	const int32 CurrentLOD = 0;
	FStaticMeshLODResources& LODResource = StaticMesh->RenderData->LODResources[CurrentLOD];

	for (int32 Section = 0; Section < LODResource.Sections.Num(); ++Section)
	{
		FStaticMeshSection& MeshSection = LODResource.Sections[Section];
		UMaterialInterface* MaterialInterface = StaticMesh->StaticMaterials[MeshSection.MaterialIndex].MaterialInterface;

		if (MaterialInterface->IsA(UMaterial::StaticClass()))
		{
			// Materials
			UMaterial * Material = Cast<UMaterial>(MaterialInterface);
			FString MaterialPathName = CombineResourceExportPath(Material, InExportPath);
			Dependency.DependenciesMaterials.AddUnique(MaterialPathName);
		}
		else
		{
			// Material instances
			check(MaterialInterface->IsA(UMaterialInstance::StaticClass()));
			UMaterialInstance * MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);
			FString MIPathName = CombineResourceExportPath(MaterialInstance, InExportPath);
			Dependency.DependenciesMaterialInstances.AddUnique(MIPathName);

			// Parent Materials
			UMaterialInterface* ParentMaterial = MaterialInstance->Parent;
			while (ParentMaterial && !ParentMaterial->IsA(UMaterial::StaticClass()))
			{
				check(ParentMaterial->IsA(UMaterialInstance::StaticClass()));
				UMaterialInstance* ParentMaterialInstance = Cast<UMaterialInstance>(ParentMaterial);
				ParentMaterial = ParentMaterialInstance->Parent;
			}
			check(ParentMaterial != nullptr);
			UMaterial * Material = Cast<UMaterial>(ParentMaterial);
			FString MaterialPathName = CombineResourceExportPath(Material, InExportPath);
			Dependency.DependenciesMaterials.AddUnique(MaterialPathName);

			// Add textures
			for (int32 i = 0; i < MaterialInstance->TextureParameterValues.Num(); ++i)
			{
				const FTextureParameterValue& TextureValue = MaterialInstance->TextureParameterValues[i];

				UTexture * Texture = TextureValue.ParameterValue;
				FString TexturePathName = CombineResourceExportPath(Texture, InExportPath);
				if (!Texture->IsA(UTexture2D::StaticClass()))
				{
					continue;
				}
				Dependency.DependenciesTextures.AddUnique(TexturePathName);
			}
		}
	}
}

void UTiXExporterBPLibrary::GetSkeletalMeshDependency(const USkeletalMesh* SkeletalMesh, const FString& InExportPath, FDependency& Dependency)
{
	FString MeshPathName = CombineResourceExportPath(SkeletalMesh, InExportPath);
	Dependency.DependenciesSkeletalMeshes.AddUnique(MeshPathName);

	// Skeleton and Anim dependencies
	USkeleton* Skeleton = SkeletalMesh->Skeleton;
	FString SkeletonPathName = CombineResourceExportPath(Skeleton, InExportPath);
	Dependency.DependenciesSkeletons.AddUnique(SkeletonPathName);

	// Material dependencies
	if (!TiXExporterSetting.bIgnoreMaterial)
	{
		FSkeletalMeshRenderData* SKMRenderData = SkeletalMesh->GetResourceForRendering();

		// Add material instance
		const int32 CurrentLOD = 0;
		FSkeletalMeshLODRenderData& LODResource = SKMRenderData->LODRenderData[CurrentLOD];
		for (int32 Section = 0; Section < LODResource.RenderSections.Num(); ++Section)
		{
			FSkelMeshRenderSection& MeshSection = LODResource.RenderSections[Section];
			FSkeletalMaterial SkeletalMaterial = SkeletalMesh->Materials[MeshSection.MaterialIndex];
			UMaterialInterface* MaterialInterface = SkeletalMaterial.MaterialInterface;

			if (MaterialInterface->IsA(UMaterial::StaticClass()))
			{
				// Materials
				UMaterial* Material = Cast<UMaterial>(MaterialInterface);
				FString MaterialPathName = CombineResourceExportPath(Material, InExportPath);
				Dependency.DependenciesMaterials.AddUnique(MaterialPathName);
			}
			else
			{
				// Material instances
				check(MaterialInterface->IsA(UMaterialInstance::StaticClass()));
				UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);
				FString MIPathName = CombineResourceExportPath(MaterialInstance, InExportPath);
				Dependency.DependenciesMaterialInstances.AddUnique(MIPathName);

				// Parent Materials
				UMaterialInterface* ParentMaterial = MaterialInstance->Parent;
				while (ParentMaterial && !ParentMaterial->IsA(UMaterial::StaticClass()))
				{
					check(ParentMaterial->IsA(UMaterialInstance::StaticClass()));
					UMaterialInstance* ParentMaterialInstance = Cast<UMaterialInstance>(ParentMaterial);
					ParentMaterial = ParentMaterialInstance->Parent;
				}
				check(ParentMaterial != nullptr);
				UMaterial* Material = Cast<UMaterial>(ParentMaterial);
				FString MaterialPathName = CombineResourceExportPath(Material, InExportPath);
				Dependency.DependenciesMaterials.AddUnique(MaterialPathName);

				// Add textures
				for (int32 i = 0; i < MaterialInstance->TextureParameterValues.Num(); ++i)
				{
					const FTextureParameterValue& TextureValue = MaterialInstance->TextureParameterValues[i];

					UTexture* Texture = TextureValue.ParameterValue;
					FString TexturePathName = CombineResourceExportPath(Texture, InExportPath);
					if (!Texture->IsA(UTexture2D::StaticClass()))
					{
						continue;
					}
					Dependency.DependenciesTextures.AddUnique(TexturePathName);
				}
			}
		}
	}
}

void UTiXExporterBPLibrary::GetAnimSequenceDependency(const ASkeletalMeshActor* SKMActor, const FString& InExportPath, FDependency& Dependency)
{
	USkeletalMesh* SkeletalMesh = SKMActor->GetSkeletalMeshComponent()->SkeletalMesh;

	if (SKMActor->GetSkeletalMeshComponent()->GetAnimationMode() == EAnimationMode::AnimationSingleNode)
	{
		// If Use Animation Asset, Export UAnimationAsset
		UAnimSingleNodeInstance* SingleNodeInstance = SKMActor->GetSkeletalMeshComponent()->GetSingleNodeInstance();
		UAnimationAsset* AnimAsset = SingleNodeInstance->CurrentAsset;
		if (AnimAsset->IsA<UAnimSequence>())
		{
			FString AnimPathName = CombineResourceExportPath(AnimAsset, InExportPath);
			Dependency.DependenciesAnims.AddUnique(AnimPathName);
		}
	}
}