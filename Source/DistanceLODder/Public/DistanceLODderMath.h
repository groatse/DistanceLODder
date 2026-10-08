// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/** The LOD selection math, kept free of UObjects so it can be unit tested. */
namespace DistanceLODder
{
	inline constexpr int32 MaxLODs = 8; // MAX_STATIC_MESH_LODS

	/** One mesh's switch distances. */
	struct FLODThresholds
	{
		/**
		 * (Radius / ScreenSize[i])^2, i.e. the squared distance beyond which LOD i is used, before the
		 * reference magnification, distance scale and hysteresis are applied. Index 0 unused.
		 */
		float SwitchDistSq[MaxLODs] = {};
		int8 MinLOD = 0;
		int8 MaxLOD = 0;
	};

	/** Multipliers on FLODThresholds::SwitchDistSq: plain, going coarser (+hysteresis) and going finer (-hysteresis). */
	struct FDistanceFactors
	{
		float SwitchSq = 1.f;
		float CoarserSq = 1.f;
		float FinerSq = 1.f;

		/**
		 * UE uses LOD i while the bounds' screen size (M * R / D) is below ScreenSize[i], with M = max(M00, M11) of the
		 * projection, i.e. the narrower axis. That makes LOD i start at D = DistanceScale * M * R / ScreenSize[i].
		 */
		static DISTANCELODDER_API FDistanceFactors Make(float ReferenceVerticalFOV, float DistanceScale, float HysteresisPercent);
	};

	/**
	 * Builds the thresholds from per-LOD screen sizes (ScreenSizes[i] is LOD i's; the array length is the number of LODs used).
	 * Returns false when there's nothing to choose from: fewer than 2 usable LODs, or no radius.
	 */
	DISTANCELODDER_API bool BuildThresholds(float Radius, TConstArrayView<float> ScreenSizes, int32 MinLOD, FLODThresholds& OutThresholds);

	/** Picks the LOD for a squared distance. CurrentLOD INDEX_NONE means the first pick, which uses plain thresholds without hysteresis. */
	DISTANCELODDER_API int32 ComputeLOD(const FLODThresholds& Thresholds, int32 CurrentLOD, float DistSq, const FDistanceFactors& Factors);
}
