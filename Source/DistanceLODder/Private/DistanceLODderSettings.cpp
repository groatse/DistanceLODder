// Copyright (c) 2026 groatse. Licensed under the MIT License.

#include "DistanceLODderSettings.h"

UDistanceLODderSettings::UDistanceLODderSettings()
{
	SectionName = TEXT("DistanceLODder");
}

float UDistanceLODderSettings::GetGlobalScreenSize(int32 LODIndex) const
{
	switch (LODIndex)
	{
	case 1: return GlobalScreenSizeLOD1;
	case 2: return GlobalScreenSizeLOD2;
	case 3: return GlobalScreenSizeLOD3;
	default: return 0.f;
	}
}
