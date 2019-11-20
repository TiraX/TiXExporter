#pragma once

#include "CoreMinimal.h"
#include "TiXExporterDefines.h"

class FTiXBoundingSphere
{
public:
	static FSphere GetBoundingSphere(const TArray<FVector>& Points);
};