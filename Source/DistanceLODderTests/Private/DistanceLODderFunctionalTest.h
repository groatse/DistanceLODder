// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "FunctionalTest.h"

#include "DistanceLODderFunctionalTest.generated.h"

class ADistanceLODderTestPawn;
class UDistanceLODderSettings;
class UDistanceLODderSubsystem;
class ULevelStreaming;
class UStaticMeshComponent;

/**
 * Runs in PIE on the map made by Scripts/CreateFunctionalTestMap.py. Possesses a pawn with an HMD-locked camera,
 * walks it away from and back to a mesh, and streams a sublevel in, hidden, visible and out, checking the forced LODs.
 *
 * Meshes are found by tag:
 * - DLTest_Main: Static, at the origin; switches at 1000 / 2000 / 4000.
 * - DLTest_OptOut: also tagged NoDistanceLOD.
 * - DLTest_Movable: Movable.
 * - DLTest_Streamed: in the streaming sublevel, at (0, 3000, 0).
 */
UCLASS()
class ADistanceLODderFunctionalTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	ADistanceLODderFunctionalTest();

protected:
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CleanUp() override;

private:
	struct FStep
	{
		FString Name;
		/** Runs once when the step starts. */
		TFunction<void()> Enter;
		/** The step waits until this is true (if set), then a few more frames so the subsystem has ticked. */
		TFunction<bool()> Ready;
		/** Checks; returns false to stop the test. */
		TFunction<bool()> Verify;
	};

	void BuildSteps();
	FStep MoveStep(float X, int32 ExpectedLOD);

	UStaticMeshComponent* FindMesh(FName Tag) const;
	UDistanceLODderSubsystem* GetSubsystem() const;
	ULevelStreaming* GetStreamingLevel() const;
	bool CheckLOD(FName Tag, int32 ExpectedLOD, const FString& What);

	TArray<FStep> Steps;
	int32 StepIndex = 0;
	int32 FramesInStep = 0;
	int32 FramesSinceReady = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<ADistanceLODderTestPawn> Pawn;

	UPROPERTY(Transient)
	TObjectPtr<UDistanceLODderSettings> SavedSettings;

	int32 TrackedBeforeStreaming = 0;
};
