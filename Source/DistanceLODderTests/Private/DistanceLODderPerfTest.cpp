// Copyright (c) 2026 groatse. Licensed under the MIT License.

#include "DistanceLODderPerfTest.h"

#include "DistanceLODderSubsystem.h"
#include "DistanceLODderTestPawn.h"
#include "DistanceLODderTestUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderTimer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DistanceLODderPerfTest)

namespace
{
	const TCHAR* const RunNames[] = { TEXT("On"), TEXT("Off") };
	constexpr int32 NumRuns = UE_ARRAY_COUNT(RunNames);

	/**
	 * Frames to wait after ending a CSV capture before toggling the plugin or finishing the test. The capture
	 * still records the frame EndCapture is called in, so toggling then would put the toggle hitch (every
	 * forced LOD reset or applied at once) into the capture.
	 */
	constexpr int32 CooldownFrames = 2;

	IConsoleVariable* EnabledCVar()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("DistanceLODder.Enabled"));
	}
}

ADistanceLODderPerfTest::ADistanceLODderPerfTest()
{
	TimeLimit = 180.f;
	Author = TEXT("groatse");
	Description = TEXT("Moves an HMD-camera pawn through a grid of LOD meshes with DistanceLODder on and off and compares the cost.");
}

void ADistanceLODderPerfTest::StartTest()
{
	Super::StartTest();

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: needs a renderer (run with -RenderOffscreen or windowed, not -nullrhi)."));
		FinishTest(EFunctionalTestResult::Succeeded, TEXT("Skipped without a renderer"));
		return;
	}

	if (!AssertTrue(Mesh != nullptr, TEXT("Mesh is set")) || !AssertTrue(GetWorld()->GetSubsystem<UDistanceLODderSubsystem>() != nullptr, TEXT("Subsystem exists")))
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("Setup failed"));
		return;
	}

	for (int32 X = 0; X < GridSize; ++X)
	{
		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			DistanceLODderTests::SpawnStaticMeshActor(GetWorld(), Mesh, FVector(X * GridSpacing, Y * GridSpacing, 0.f));
		}
	}

	Pawn = GetWorld()->SpawnActor<ADistanceLODderTestPawn>(PathStart(), FRotator::ZeroRotator);
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->Possess(Pawn);
	}

	SavedEnabledCVar = EnabledCVar()->GetInt();
	for (FRunStats& Run : Runs)
	{
		Run = FRunStats();
	}
	RunIndex = 0;
	BeginRun();
}

void ADistanceLODderPerfTest::CleanUp()
{
	if (bOwnsCsvCapture)
	{
#if CSV_PROFILER
		FCsvProfiler::Get()->EndCapture();
#endif
		bOwnsCsvCapture = false;
	}
	EnabledCVar()->Set(SavedEnabledCVar, ECVF_SetByCode);

	Super::CleanUp();
}

void ADistanceLODderPerfTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsRunning() || !Pawn || Phase == EPhase::Done)
	{
		return;
	}

	++PhaseFrames;
	if (Phase == EPhase::Cooldown)
	{
		if (PhaseFrames >= CooldownFrames)
		{
			if (++RunIndex < NumRuns)
			{
				BeginRun();
			}
			else
			{
				Phase = EPhase::Done;
				Report();
			}
		}
		return;
	}

	if (Phase == EPhase::Settle)
	{
		// Also wait for queued LOD changes, e.g. the 2500 meshes spawned at the start, which go through the per-frame budget.
		if (PhaseFrames >= SettleFrames && GetWorld()->GetSubsystem<UDistanceLODderSubsystem>()->GetLastFrameStats().Queued == 0)
		{
			Phase = EPhase::Run;
			PhaseFrames = 0;
			PathTime = 0.f;
#if CSV_PROFILER
			if (bCaptureCsv && !FCsvProfiler::Get()->IsCapturing())
			{
				FCsvProfiler::Get()->BeginCapture(-1, FPaths::ProfilingDir() / TEXT("CSV"), FString::Printf(TEXT("DistanceLODderPerf_%s.csv"), RunNames[RunIndex]));
				bOwnsCsvCapture = true;
			}
#endif
		}
		return;
	}

	// Run: follow the path by elapsed time, so both runs cover the same ground whatever the frame rate.
	PathTime += DeltaSeconds;
	Pawn->SetActorLocation(PathStart() + PathDirection() * PathSpeed * FMath::Min(PathTime, PathDuration));
	Sample();

	if (PathTime >= PathDuration)
	{
		EndRun();
		Phase = EPhase::Cooldown;
		PhaseFrames = 0;
	}
}

void ADistanceLODderPerfTest::BeginRun()
{
	EnabledCVar()->Set(RunIndex == 0 ? 1 : 0, ECVF_SetByCode);
	Pawn->SetActorLocation(PathStart());
	Phase = EPhase::Settle;
	PhaseFrames = 0;
}

void ADistanceLODderPerfTest::EndRun()
{
#if CSV_PROFILER
	if (bOwnsCsvCapture)
	{
		FCsvProfiler::Get()->EndCapture();
		bOwnsCsvCapture = false;
	}
#endif
}

void ADistanceLODderPerfTest::Sample()
{
	FRunStats& Run = Runs[RunIndex];
	const FDistanceLODderFrameStats& Stats = GetWorld()->GetSubsystem<UDistanceLODderSubsystem>()->GetLastFrameStats();

	const double FrameMs = FApp::GetDeltaTime() * 1000.0;
	++Run.Frames;
	Run.FrameMs += FrameMs;
	Run.GameThreadMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
	Run.RenderThreadMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	Run.TickMs += Stats.TickMs;
	Run.PeakFrameMs = FMath::Max(Run.PeakFrameMs, FrameMs);
	Run.PeakTickMs = FMath::Max(Run.PeakTickMs, Stats.TickMs);
	Run.Changes += Stats.Changed;
	Run.PeakChanges = FMath::Max(Run.PeakChanges, Stats.Changed);
	Run.PeakQueued = FMath::Max(Run.PeakQueued, Stats.Queued);
}

void ADistanceLODderPerfTest::Report()
{
	for (int32 Index = 0; Index < NumRuns; ++Index)
	{
		const FRunStats& Run = Runs[Index];
		AddInfo(FString::Printf(
			TEXT("Plugin %s: %d frames, frame %.2f ms avg / %.2f peak, game thread %.2f ms, render thread %.2f ms, ")
			TEXT("subsystem tick %.3f ms avg / %.3f peak, %d LOD changes (peak %d/frame, peak queue %d)"),
			RunNames[Index], Run.Frames, Run.Average(Run.FrameMs), Run.PeakFrameMs, Run.Average(Run.GameThreadMs), Run.Average(Run.RenderThreadMs),
			Run.Average(Run.TickMs), Run.PeakTickMs, Run.Changes, Run.PeakChanges, Run.PeakQueued));
	}

	const FRunStats& On = Runs[0];
	const FRunStats& Off = Runs[1];
	const double OffFrameMs = Off.Average(Off.FrameMs);
	const double FrameIncreasePercent = OffFrameMs > 0.0 ? (On.Average(On.FrameMs) / OffFrameMs - 1.0) * 100.0 : 0.0;
	AddInfo(FString::Printf(TEXT("Frame time on vs off: %+.1f%%, game thread %+.2f ms, render thread %+.2f ms"),
		FrameIncreasePercent, On.Average(On.GameThreadMs) - Off.Average(Off.GameThreadMs), On.Average(On.RenderThreadMs) - Off.Average(Off.RenderThreadMs)));

	AssertTrue(On.Changes > 0, TEXT("The path caused LOD changes with the plugin on"));
	AssertTrue(On.Average(On.TickMs) <= MaxAverageTickMs, FString::Printf(TEXT("Average subsystem tick %.3f ms <= %.3f ms"), On.Average(On.TickMs), MaxAverageTickMs));
	AssertTrue(On.PeakTickMs <= MaxPeakTickMs, FString::Printf(TEXT("Peak subsystem tick %.3f ms <= %.3f ms"), On.PeakTickMs, MaxPeakTickMs));
	if (MaxFrameTimeIncreasePercent > 0.f)
	{
		AssertTrue(FrameIncreasePercent <= MaxFrameTimeIncreasePercent, FString::Printf(TEXT("Frame time increase %.1f%% <= %.1f%%"), FrameIncreasePercent, MaxFrameTimeIncreasePercent));
	}

	FinishTest(EFunctionalTestResult::Default, TEXT("Perf runs complete"));
}

FVector ADistanceLODderPerfTest::PathStart() const
{
	// Just outside the grid's near corner, at roughly eye height.
	return FVector(-GridSpacing * 5.f, -GridSpacing * 5.f, 170.f);
}

FVector ADistanceLODderPerfTest::PathDirection() const
{
	return FVector(1.f, 1.f, 0.f).GetSafeNormal();
}
