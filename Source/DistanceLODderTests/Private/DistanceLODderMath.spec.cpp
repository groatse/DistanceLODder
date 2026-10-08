// Copyright (c) 2026 groatse. Licensed under the MIT License.

#include "DistanceLODderMath.h"
#include "Math/PerspectiveMatrix.h"
#include "Misc/AutomationTest.h"
#include "SceneManagement.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace DistanceLODder;

BEGIN_DEFINE_SPEC(FDistanceLODderMathSpec, "DistanceLODder.Math",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	// Radius 100 with screen sizes 1 / 0.5 / 0.25 / 0.125 gives base switch distances of 200 / 400 / 800.
	static constexpr float Radius = 100.f;
	TArray<float> ScreenSizes = { 1.f, 0.5f, 0.25f, 0.125f };

	FLODThresholds Thresholds;
	// 90 degrees gives a magnification of 1, so base distances are the real ones. 10% hysteresis.
	FDistanceFactors Factors = FDistanceFactors::Make(90.f, 1.f, 10.f);

	int32 Pick(float Dist, int32 CurrentLOD = INDEX_NONE) const
	{
		return ComputeLOD(Thresholds, CurrentLOD, FMath::Square(Dist), Factors);
	}

END_DEFINE_SPEC(FDistanceLODderMathSpec)

void FDistanceLODderMathSpec::Define()
{
	Describe("FDistanceFactors", [this]()
	{
		It("uses a magnification of 1 at 90 degrees", [this]()
		{
			TestNearlyEqual(TEXT("SwitchSq"), FDistanceFactors::Make(90.f, 1.f, 0.f).SwitchSq, 1.f, 1e-5f);
		});

		It("multiplies distances by the distance scale", [this]()
		{
			TestNearlyEqual(TEXT("SwitchSq"), FDistanceFactors::Make(90.f, 2.f, 0.f).SwitchSq, 4.f, 1e-4f);
		});

		It("widens and narrows by the hysteresis", [this]()
		{
			const FDistanceFactors F = FDistanceFactors::Make(90.f, 1.f, 10.f);
			TestNearlyEqual(TEXT("CoarserSq"), F.CoarserSq, 1.21f, 1e-5f);
			TestNearlyEqual(TEXT("FinerSq"), F.FinerSq, 0.81f, 1e-5f);
		});

		It("matches the engine's draw distance for an editor viewport (90 deg horizontal, 16:9)", [this]()
		{
			// The engine measures against max(M00, M11), i.e. the vertical FOV for a landscape view.
			const float HalfHorizontalFOV = FMath::DegreesToRadians(45.f);
			const FMatrix Projection = FReversedZPerspectiveMatrix(HalfHorizontalFOV, 1920.f, 1080.f, 10.f);
			const float VerticalFOV = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(HalfHorizontalFOV) * 1080.f / 1920.f));

			FLODThresholds T;
			TestTrue(TEXT("BuildThresholds"), BuildThresholds(Radius, ScreenSizes, 0, T));
			const FDistanceFactors F = FDistanceFactors::Make(VerticalFOV, 1.f, 0.f);

			for (int32 LODIndex = 1; LODIndex < ScreenSizes.Num(); ++LODIndex)
			{
				const float EngineDist = ComputeBoundsDrawDistance(ScreenSizes[LODIndex], Radius, Projection);
				const float PluginDist = FMath::Sqrt(T.SwitchDistSq[LODIndex] * F.SwitchSq);
				TestNearlyEqual(FString::Printf(TEXT("LOD%d switch distance"), LODIndex), PluginDist, EngineDist, EngineDist * 1e-4f);
			}
		});

		It("has a default reference FOV (59 deg) within 1% of the editor viewport", [this]()
		{
			const FMatrix Projection = FReversedZPerspectiveMatrix(FMath::DegreesToRadians(45.f), 1920.f, 1080.f, 10.f);
			const float EngineDist = ComputeBoundsDrawDistance(0.5f, Radius, Projection);
			const float PluginDist = FMath::Sqrt(FMath::Square(Radius / 0.5f) * FDistanceFactors::Make(59.f, 1.f, 0.f).SwitchSq);
			TestNearlyEqual(TEXT("Distance"), PluginDist, EngineDist, EngineDist * 0.01f);
		});
	});

	Describe("BuildThresholds", [this]()
	{
		It("turns screen sizes into squared distances R / S", [this]()
		{
			FLODThresholds T;
			TestTrue(TEXT("Result"), BuildThresholds(Radius, ScreenSizes, 0, T));
			TestEqual(TEXT("MinLOD"), (int32)T.MinLOD, 0);
			TestEqual(TEXT("MaxLOD"), (int32)T.MaxLOD, 3);
			TestNearlyEqual(TEXT("LOD1"), T.SwitchDistSq[1], 200.f * 200.f, 1.f);
			TestNearlyEqual(TEXT("LOD2"), T.SwitchDistSq[2], 400.f * 400.f, 1.f);
			TestNearlyEqual(TEXT("LOD3"), T.SwitchDistSq[3], 800.f * 800.f, 1.f);
		});

		It("caps the max LOD at the first zero screen size", [this]()
		{
			FLODThresholds T;
			TestTrue(TEXT("Result"), BuildThresholds(Radius, { 1.f, 0.5f, 0.f, 0.125f }, 0, T));
			TestEqual(TEXT("MaxLOD"), (int32)T.MaxLOD, 1);
		});

		It("keeps distances increasing when screen sizes aren't decreasing", [this]()
		{
			FLODThresholds T;
			TestTrue(TEXT("Result"), BuildThresholds(Radius, { 1.f, 0.25f, 0.5f }, 0, T));
			TestTrue(TEXT("Monotonic"), T.SwitchDistSq[2] >= T.SwitchDistSq[1]);
		});

		It("clamps the min LOD", [this]()
		{
			FLODThresholds T;
			TestTrue(TEXT("Result"), BuildThresholds(Radius, ScreenSizes, -3, T));
			TestEqual(TEXT("MinLOD"), (int32)T.MinLOD, 0);
			TestTrue(TEXT("Result"), BuildThresholds(Radius, ScreenSizes, 2, T));
			TestEqual(TEXT("MinLOD"), (int32)T.MinLOD, 2);
		});

		It("uses at most MaxLODs LODs", [this]()
		{
			TArray<float> Many;
			for (int32 i = 0; i < MaxLODs + 4; ++i)
			{
				Many.Add(1.f / (1 << i));
			}
			FLODThresholds T;
			TestTrue(TEXT("Result"), BuildThresholds(Radius, Many, 0, T));
			TestEqual(TEXT("MaxLOD"), (int32)T.MaxLOD, MaxLODs - 1);
		});

		It("rejects meshes with nothing to choose", [this]()
		{
			FLODThresholds T;
			TestFalse(TEXT("Single LOD"), BuildThresholds(Radius, { 1.f }, 0, T));
			TestFalse(TEXT("Zero radius"), BuildThresholds(0.f, ScreenSizes, 0, T));
			TestFalse(TEXT("Min LOD is the last LOD"), BuildThresholds(Radius, ScreenSizes, 3, T));
			TestFalse(TEXT("Only LOD0 reachable"), BuildThresholds(Radius, { 1.f, 0.f, 0.f }, 0, T));
		});
	});

	Describe("ComputeLOD", [this]()
	{
		BeforeEach([this]()
		{
			BuildThresholds(Radius, ScreenSizes, 0, Thresholds);
			Factors = FDistanceFactors::Make(90.f, 1.f, 10.f);
		});

		It("picks plain thresholds on the first pick", [this]()
		{
			TestEqual(TEXT("150"), Pick(150.f), 0);
			TestEqual(TEXT("199"), Pick(199.f), 0);
			TestEqual(TEXT("201"), Pick(201.f), 1);
			TestEqual(TEXT("450"), Pick(450.f), 2);
			TestEqual(TEXT("5000"), Pick(5000.f), 3);
		});

		It("doesn't go coarser inside the hysteresis band", [this]()
		{
			TestEqual(TEXT("210 from LOD0"), Pick(210.f, 0), 0);
			TestEqual(TEXT("219 from LOD0"), Pick(219.f, 0), 0);
			TestEqual(TEXT("221 from LOD0"), Pick(221.f, 0), 1);
		});

		It("doesn't go finer inside the hysteresis band", [this]()
		{
			TestEqual(TEXT("190 from LOD1"), Pick(190.f, 1), 1);
			TestEqual(TEXT("181 from LOD1"), Pick(181.f, 1), 1);
			TestEqual(TEXT("179 from LOD1"), Pick(179.f, 1), 0);
		});

		It("jumps several LODs at once", [this]()
		{
			TestEqual(TEXT("LOD0 -> far"), Pick(5000.f, 0), 3);
			TestEqual(TEXT("LOD3 -> near"), Pick(10.f, 3), 0);
		});

		It("switches at the expected distances walking out and back", [this]()
		{
			// Out: coarser beyond 220 / 440 / 880. Back: finer within 720 / 360 / 180.
			// Steps are offset by 1 so no sample lands exactly on a boundary.
			TArray<TPair<float, int32>> Switches;
			int32 LOD = INDEX_NONE;
			auto Step = [&](float Dist)
			{
				const int32 NewLOD = Pick(Dist, LOD);
				if (LOD != INDEX_NONE && NewLOD != LOD)
				{
					Switches.Emplace(Dist, NewLOD);
				}
				LOD = NewLOD;
			};

			for (float Dist = 1.f; Dist <= 1001.f; Dist += 5.f) { Step(Dist); }
			for (float Dist = 1001.f; Dist >= 1.f; Dist -= 5.f) { Step(Dist); }

			const TArray<TPair<float, int32>> Expected = {
				{ 221.f, 1 }, { 441.f, 2 }, { 881.f, 3 },
				{ 716.f, 2 }, { 356.f, 1 }, { 176.f, 0 },
			};
			if (TestEqual(TEXT("Number of switches"), Switches.Num(), Expected.Num()))
			{
				for (int32 i = 0; i < Expected.Num(); ++i)
				{
					TestEqual(FString::Printf(TEXT("Switch %d distance"), i), Switches[i].Key, Expected[i].Key);
					TestEqual(FString::Printf(TEXT("Switch %d LOD"), i), Switches[i].Value, Expected[i].Value);
				}
			}
		});

		It("never goes below the min LOD", [this]()
		{
			BuildThresholds(Radius, ScreenSizes, 1, Thresholds);
			TestEqual(TEXT("First pick"), Pick(10.f), 1);
			TestEqual(TEXT("From LOD1"), Pick(10.f, 1), 1);
			TestEqual(TEXT("From LOD2"), Pick(10.f, 2), 1);
		});

		It("never goes above the max LOD", [this]()
		{
			BuildThresholds(Radius, { 1.f, 0.5f, 0.25f }, 0, Thresholds);
			TestEqual(TEXT("First pick"), Pick(1e6f), 2);
			TestEqual(TEXT("From LOD2"), Pick(1e6f, 2), 2);
		});

		It("recovers from a current LOD outside the valid range", [this]()
		{
			TestEqual(TEXT("Current 7, near"), Pick(10.f, 7), 0);
			TestEqual(TEXT("Current 7, far"), Pick(5000.f, 7), 3);
		});

		It("scales switch distances with the distance scale", [this]()
		{
			Factors = FDistanceFactors::Make(90.f, 2.f, 0.f);
			TestEqual(TEXT("350"), Pick(350.f), 0);
			TestEqual(TEXT("450"), Pick(450.f), 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
