// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "TiXExporterDefines.h"
#include "TiXExporterBPLibrary.generated.h"

/* 
*	Function library class.
*	Each function in it is expected to be static and represents blueprint node that can be called in any blueprint.
*
*	When declaring function you can define metadata for the node. Key function specifiers will be BlueprintPure and BlueprintCallable.
*	BlueprintPure - means the function does not affect the owning object in any way and thus creates a node without Exec pins.
*	BlueprintCallable - makes a function which can be executed in Blueprints - Thus it has Exec pins.
*	DisplayName - full name of the node, shown when you mouse over the node and in the blueprint drop down menu.
*				Its lets you name the node using characters not allowed in C++ function names.
*	CompactNodeTitle - the word(s) that appear on the node.
*	Keywords -	the list of keywords that helps you to find node when you search for it using Blueprint drop-down menu. 
*				Good example is "Print String" node which you can find also by using keyword "log".
*	Category -	the category your node will be under in the Blueprint drop-down menu.
*
*	For more info on custom blueprint nodes visit documentation:
*	https://wiki.unrealengine.com/Custom_Blueprint_Node_Creation
*/

DECLARE_LOG_CATEGORY_EXTERN(LogTiXExporter, Log, All);


USTRUCT()
struct FTiXBoneInfo
{
    GENERATED_BODY()

    UPROPERTY()
    int32 index;

    UPROPERTY()
    FString bone_name;

    UPROPERTY()
    int32 parent_index;
    
    UPROPERTY()
    FVector translation;
    
    UPROPERTY()
    FQuat rotation;
    
    UPROPERTY()
    FVector scale;
};

USTRUCT()
struct FTiXSkeletonInfo
{
    GENERATED_BODY()
		
    UPROPERTY()
    FString name;
		
    UPROPERTY()
    FString type;
	
    UPROPERTY()
    int32 version;
		
    UPROPERTY()
    FString desc;

    UPROPERTY()
    TArray<FTiXBoneInfo> bones;
};

UCLASS()
class UTiXExporterBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Export Current Scene", Keywords = "TiX Export Current Scene"), Category = "TiXExporter")
	static void ExportCurrentScene(AActor * Actor, const FString& ExportPath, const TArray<FString>& SceneComponents, const TArray<FString>& MeshComponents);

	/** Export static mesh.
	Param: Components should be combine of one or more in "POSITION, NORMAL, COLOR, TEXCOORD0, TEXCOORD1, TANGENT, BLENDINDEX, BLENDWEIGHT".
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Export Static Mesh Actor", Keywords = "TiX Export Static Mesh Actor"), Category = "TiXExporter")
	static void ExportStaticMeshActor(AStaticMeshActor * StaticMeshActor, FString ExportPath, const TArray<FString>& Components);

	/** Export static mesh. 
		Param: Components should be combine of one or more in "POSITION, NORMAL, COLOR, TEXCOORD0, TEXCOORD1, TANGENT, BLENDINDEX, BLENDWEIGHT".
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Export Static Mesh", Keywords = "TiX Export Static Mesh"), Category = "TiXExporter")
	static void ExportStaticMesh(UStaticMesh * StaticMesh, FString ExportPath, const TArray<FString>& Components);


	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Tile Size", Keywords = "TiX Set Tile Size"), Category = "TiXExporter")
	static void SetTileSize(float TileSize);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Mesh Vertex Position Scale", Keywords = "TiX Set Mesh Vertex Position Scale"), Category = "TiXExporter")
	static void SetMeshVertexPositionScale(float MeshVertexPositionScale);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Ignore Material", Keywords = "TiX Set Ignore Material"), Category = "TiXExporter")
	static void SetIgnoreMaterial(bool bIgnore);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Enable Mesh Cluster", Keywords = "TiX Set Enable Mesh Cluster"), Category = "TiXExporter")
	static void SetEnableMeshCluster(bool bEnable);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Mesh Cluster Size", Keywords = "TiX Set Mesh Cluster Size"), Category = "TiXExporter")
	static void SetMeshClusterSize(int32 Triangles);

private:
	static void ExportStaticMeshFromRenderData(UStaticMesh* StaticMesh, const FString& Path, const TArray<FString>& Components);
	static void ExportSkeletalMeshFromRenderData(USkeletalMesh* SkeletalMesh, FString ExportPath, const TArray<FString>& Components);
	static void ExportStaticMeshFromRawMesh(UStaticMesh* StaticMesh, const FString& Path, const TArray<FString>& Components);
	static void ExportMaterialInstance(UMaterialInterface* InMaterial, const FString& Path);
	static void ExportMaterial(UMaterialInterface* InMaterial, const FString& Path);
	static void ExportTexture(UTexture* InTexture, const FString& Path, bool UsedAsIBL = false);
	static void ExportReflectionCapture(AReflectionCapture* RCActor, const FString& Path);
	static void ExportSkeleton(USkeleton* InSkeleton, const FString& Path);

	static TSharedPtr<FJsonObject> ExportMeshInstances(const UStaticMesh * InMesh, const TArray<FTiXInstance>& Instances);
	static TSharedPtr<FJsonObject> ExportMeshCollisions(const UStaticMesh * InMesh);

	static void ExportSceneTile(const FTiXSceneTile& SceneTile, const FString& WorldName, const FString& InExportName);

	static void GetStaticMeshDependency(const UStaticMesh * StaticMesh, const FString& InExportPath, FDependency& Dependency);
};
