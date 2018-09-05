// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "TiXExporterBPLibrary.h"
#include "TiXExporter.h"

DEFINE_LOG_CATEGORY(LogTiXExporter);

UTiXExporterBPLibrary::UTiXExporterBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

void UTiXExporterBPLibrary::ExportCurrentScene()
{
	UE_LOG(LogTiXExporter, Log, TEXT("Export tix scene ..."));
}

