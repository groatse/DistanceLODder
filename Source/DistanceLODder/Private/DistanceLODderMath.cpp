// Copyright (c) 2026 groatse. Licensed under the MIT License.

#include "DistanceLODderMath.h"

namespace DistanceLODder
{
	FDistanceFactors FDistanceFactors::Make(float ReferenceVerticalFOV, float DistanceScale, float HysteresisPercent)
	{
		const float Magnification = 1.f / FMath::Tan(FMath::DegreesToRadians(ReferenceVerticalFOV * 0.5f));
		const float Hysteresis = HysteresisPercent * 0.01f;

		FDistanceFactors Factors;
		Factors.SwitchSq = FMath::Square(Magnification * DistanceScale);
		Factors.CoarserSq = Factors.SwitchSq * FMath::Square(1.f + Hysteresis);
		Factors.FinerSq = Factors.SwitchSq * FMath::Square(1.f - Hysteresis);
		return Factors;
	}

	bool BuildThresholds(float Radius, TConstArrayView<float> ScreenSizes, int32 MinLOD, FLODThresholds& OutThresholds)
	{
		const int32 NumLODs = FMath::Min(ScreenSizes.Num(), MaxLODs);
		if (NumLODs < 2 || Radius <= 0.f)
		{
			return false;
		}

		OutThresholds = FLODThresholds();
		OutThresholds.MinLOD = static_cast<int8>(FMath::Clamp(MinLOD, 0, NumLODs - 1));
		OutThresholds.MaxLOD = static_cast<int8>(NumLODs - 1);

		float PrevDist = 0.f;
		for (int32 LODIndex = 1; LODIndex < NumLODs; ++LODIndex)
		{
			const float ScreenSize = ScreenSizes[LODIndex];
			if (ScreenSize <= UE_KINDA_SMALL_NUMBER)
			{
				// LOD never reached.
				OutThresholds.MaxLOD = static_cast<int8>(LODIndex - 1);
				break;
			}

			// Keep distances increasing even if the screen sizes aren't decreasing.
			const float Dist = FMath::Max(Radius / ScreenSize, PrevDist);
			PrevDist = Dist;
			OutThresholds.SwitchDistSq[LODIndex] = FMath::Square(Dist);
		}

		return OutThresholds.MaxLOD > OutThresholds.MinLOD;
	}

	int32 ComputeLOD(const FLODThresholds& Thresholds, int32 CurrentLOD, float DistSq, const FDistanceFactors& Factors)
	{
		if (CurrentLOD == INDEX_NONE)
		{
			int32 LOD = Thresholds.MinLOD;
			while (LOD < Thresholds.MaxLOD && DistSq > Thresholds.SwitchDistSq[LOD + 1] * Factors.SwitchSq)
			{
				++LOD;
			}
			return LOD;
		}

		int32 LOD = FMath::Clamp<int32>(CurrentLOD, Thresholds.MinLOD, Thresholds.MaxLOD);
		const int32 StartLOD = LOD;
		while (LOD < Thresholds.MaxLOD && DistSq > Thresholds.SwitchDistSq[LOD + 1] * Factors.CoarserSq)
		{
			++LOD;
		}

		if (LOD == StartLOD)
		{
			while (LOD > Thresholds.MinLOD && DistSq < Thresholds.SwitchDistSq[LOD] * Factors.FinerSq)
			{
				--LOD;
			}
		}

		return LOD;
	}
}
