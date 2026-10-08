// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "FunctionalTest.h"

#include "DistanceLODderPerfTest.generated.h"

class ADistanceLODderTestPawn;
class UStaticMesh;

/**
 * Cost check: spawns a grid of LOD meshes, then moves an HMD-camera pawn along the same path twice, with
 * DistanceLODder on and off, and compares frame, game thread and render thread times. Fails when the
 * subsystem's own tick goes over the budgets below.
 *
 * Uses the project's DistanceLODder settings as they are. Needs a renderer: the cost of LOD changes (render
 * state rebuilds) only shows up with real rendering, so under -nullrhi it skips. Run with -RenderOffscreen or
 * windowed. Measures relative cost on this machine, not headset performance.
 *
 * With CSV profiling available, each run is also captured to Saved/Profiling/CSV/DistanceLODderPerf_On/Off.csv
 * (unless a capture is already running).
 */
UCLASS()
class ADistanceLODderPerfTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	ADistanceLODderPerfTest();

	/** Mesh with several LODs to fill the grid with. */
	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf")
	TObjectPtr<UStaticMesh> Mesh;

	/** The grid is GridSize x GridSize meshes. */
	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "1"))
	int32 GridSize = 50;

	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "1.0", Units = "Centimeters"))
	float GridSpacing = 200.f;

	/** The pawn moves diagonally across the grid at this speed for PathDuration. */
	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "1.0", Units = "CentimetersPerSecond"))
	float PathSpeed = 500.f;

	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "1.0", Units = "Seconds"))
	float PathDuration = 20.f;

	/** Frames to wait at the start of the path before measuring (and until no LOD changes are queued). */
	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "1"))
	int32 SettleFrames = 60;

	/** Fails if the subsystem's average tick time with the plugin on is above this. */
	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "0.0", Units = "Milliseconds"))
	float MaxAverageTickMs = 0.25f;

	/** Fails if the subsystem's slowest tick with the plugin on is above this. */
	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "0.0", Units = "Milliseconds"))
	float MaxPeakTickMs = 2.f;

	/** Fails if the average frame time with the plugin on is more than this much higher than off. 0 = report only (frame times are noisy). */
	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf", meta = (ClampMin = "0.0", Units = "Percent"))
	float MaxFrameTimeIncreasePercent = 0.f;

	UPROPERTY(EditAnywhere, Category = "DistanceLODder Perf")
	bool bCaptureCsv = true;

protected:
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CleanUp() override;

private:
	struct FRunStats
	{
		int32 Frames = 0;
		double FrameMs = 0.0;
		double GameThreadMs = 0.0;
		double RenderThreadMs = 0.0;
		double TickMs = 0.0;
		double PeakFrameMs = 0.0;
		double PeakTickMs = 0.0;
		int32 Changes = 0;
		int32 PeakChanges = 0;
		int32 PeakQueued = 0;

		double Average(double Sum) const { return Frames > 0 ? Sum / Frames : 0.0; }
	};

	enum class EPhase : uint8 { Settle, Run, Cooldown, Done };

	void BeginRun();
	void EndRun();
	void Sample();
	void Report();
	FVector PathStart() const;
	FVector PathDirection() const;

	/** Runs[0] = plugin on, Runs[1] = plugin off. */
	FRunStats Runs[2];
	int32 RunIndex = 0;
	EPhase Phase = EPhase::Settle;
	int32 PhaseFrames = 0;
	float PathTime = 0.f;
	int32 SavedEnabledCVar = 1;
	bool bOwnsCsvCapture = false;

	UPROPERTY(Transient)
	TObjectPtr<ADistanceLODderTestPawn> Pawn;
};
