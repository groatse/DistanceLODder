// Copyright (c) 2026 groatse. Licensed under the MIT License.

#include "DistanceLODderFunctionalTest.h"

#include "Components/StaticMeshComponent.h"
#include "DistanceLODderSettings.h"
#include "DistanceLODderSubsystem.h"
#include "DistanceLODderTestPawn.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DistanceLODderFunctionalTest)

namespace
{
	const FName MainTag(TEXT("DLTest_Main"));
	const FName OptOutTag(TEXT("DLTest_OptOut"));
	const FName MovableTag(TEXT("DLTest_Movable"));
	const FName StreamedTag(TEXT("DLTest_Streamed"));

	/** Frames to wait after a step is ready, so the subsystem has ticked at least once. */
	constexpr int32 SettleFrames = 3;
}

ADistanceLODderFunctionalTest::ADistanceLODderFunctionalTest()
{
	TimeLimit = 30.f;
	Author = TEXT("groatse");
	Description = TEXT("Walks an HMD-camera pawn around a mesh and streams a sublevel, checking DistanceLODder's forced LODs.");
}

void ADistanceLODderFunctionalTest::StartTest()
{
	Super::StartTest();

	// Known settings for the test; restored in CleanUp.
	UDistanceLODderSettings* Settings = GetMutableDefault<UDistanceLODderSettings>();
	SavedSettings = NewObject<UDistanceLODderSettings>(this);
	for (TFieldIterator<FProperty> It(UDistanceLODderSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		It->CopyCompleteValue_InContainer(SavedSettings, Settings);
	}
	Settings->ReferenceVerticalFOV = 90.f;
	Settings->DistanceScale = 1.f;
	Settings->HysteresisPercent = 10.f;
	Settings->bUseGlobalScreenSizes = false;
	Settings->MovementThreshold = 25.f;
	Settings->MaxLODChangesPerFrame = 32;
	Settings->MaxComponentsEvaluatedPerFrame = 4096;
	Settings->OptOutTag = TEXT("NoDistanceLOD");

	StepIndex = 0;
	FramesInStep = 0;
	FramesSinceReady = INDEX_NONE;
	BuildSteps();
}

void ADistanceLODderFunctionalTest::CleanUp()
{
	if (SavedSettings)
	{
		UDistanceLODderSettings* Settings = GetMutableDefault<UDistanceLODderSettings>();
		for (TFieldIterator<FProperty> It(UDistanceLODderSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			It->CopyCompleteValue_InContainer(Settings, SavedSettings);
		}
		SavedSettings = nullptr;
	}

	Super::CleanUp();
}

void ADistanceLODderFunctionalTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsRunning() || !Steps.IsValidIndex(StepIndex))
	{
		return;
	}

	FStep& Step = Steps[StepIndex];
	if (FramesInStep++ == 0)
	{
		LogStep(ELogVerbosity::Log, Step.Name);
		if (Step.Enter)
		{
			Step.Enter();
		}
	}

	if (FramesSinceReady == INDEX_NONE)
	{
		if (Step.Ready && !Step.Ready())
		{
			return;
		}
		FramesSinceReady = 0;
	}

	if (++FramesSinceReady < SettleFrames)
	{
		return;
	}

	if (Step.Verify && !Step.Verify())
	{
		FinishTest(EFunctionalTestResult::Failed, FString::Printf(TEXT("Step '%s' failed"), *Step.Name));
		return;
	}

	++StepIndex;
	FramesInStep = 0;
	FramesSinceReady = INDEX_NONE;

	if (!Steps.IsValidIndex(StepIndex))
	{
		FinishTest(EFunctionalTestResult::Succeeded, TEXT("All steps passed"));
	}
}

void ADistanceLODderFunctionalTest::BuildSteps()
{
	Steps.Reset();

	Steps.Add({
		TEXT("Possess an HMD-camera pawn at 500"),
		[this]()
		{
			APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
			Pawn = GetWorld()->SpawnActor<ADistanceLODderTestPawn>(FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator);
			if (PlayerController && Pawn)
			{
				PlayerController->Possess(Pawn);
			}
		},
		nullptr,
		[this]()
		{
			const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
			bool bOk = AssertTrue(PlayerController && Pawn && PlayerController->GetPawn() == Pawn, TEXT("Player controller possesses the test pawn"));
			bOk &= AssertTrue(GetSubsystem() != nullptr, TEXT("Subsystem exists"));
			if (!bOk)
			{
				return false;
			}

			const UStaticMeshComponent* OptOut = FindMesh(OptOutTag);
			const UStaticMeshComponent* Movable = FindMesh(MovableTag);
			bOk &= AssertTrue(OptOut && !GetSubsystem()->IsComponentTracked(OptOut) && OptOut->ForcedLodModel == 0, TEXT("Opt-out tagged mesh is left alone"));
			bOk &= AssertTrue(Movable && !GetSubsystem()->IsComponentTracked(Movable) && Movable->ForcedLodModel == 0, TEXT("Movable mesh is left alone"));
			bOk &= CheckLOD(MainTag, 0, TEXT("Main mesh at 500"));
			return bOk;
		},
	});

	// Out and back again; the way back crosses the hysteresis bands (finer below 3600 / 1800 / 900).
	Steps.Add(MoveStep(1500.f, 1));
	Steps.Add(MoveStep(3000.f, 2));
	Steps.Add(MoveStep(5000.f, 3));
	Steps.Add(MoveStep(1050.f, 1));
	Steps.Add(MoveStep(500.f, 0));

	Steps.Add({
		TEXT("Stream the sublevel in"),
		[this]()
		{
			TrackedBeforeStreaming = GetSubsystem()->GetNumTrackedComponents();
			if (ULevelStreaming* Streaming = GetStreamingLevel())
			{
				Streaming->SetShouldBeLoaded(true);
				Streaming->SetShouldBeVisible(true);
			}
		},
		[this]()
		{
			const ULevelStreaming* Streaming = GetStreamingLevel();
			return Streaming && Streaming->IsLevelVisible();
		},
		[this]()
		{
			// (0, 3000, 0) is about 3041 from the pawn at (500, 0, 0).
			const UStaticMeshComponent* Streamed = FindMesh(StreamedTag);
			return AssertTrue(Streamed && GetSubsystem()->IsComponentTracked(Streamed), TEXT("Streamed mesh is tracked"))
				&& CheckLOD(StreamedTag, 2, TEXT("Streamed mesh"));
		},
	});

	Steps.Add({
		TEXT("Hide the sublevel (still loaded)"),
		[this]()
		{
			GetStreamingLevel()->SetShouldBeVisible(false);
		},
		[this]()
		{
			return !GetStreamingLevel()->IsLevelVisible();
		},
		[this]()
		{
			const UStaticMeshComponent* Streamed = FindMesh(StreamedTag);
			return AssertTrue(Streamed != nullptr, TEXT("Hidden mesh still exists"))
				&& AssertFalse(GetSubsystem()->IsComponentTracked(Streamed), TEXT("Hidden mesh is no longer tracked"))
				&& AssertEqual_Int(Streamed->ForcedLodModel, 0, TEXT("Hidden mesh's ForcedLodModel"));
		},
	});

	Steps.Add({
		TEXT("Show the sublevel again"),
		[this]()
		{
			GetStreamingLevel()->SetShouldBeVisible(true);
		},
		[this]()
		{
			return GetStreamingLevel()->IsLevelVisible();
		},
		[this]()
		{
			return CheckLOD(StreamedTag, 2, TEXT("Re-shown streamed mesh"));
		},
	});

	Steps.Add({
		TEXT("Unload the sublevel"),
		[this]()
		{
			ULevelStreaming* Streaming = GetStreamingLevel();
			Streaming->SetShouldBeVisible(false);
			Streaming->SetShouldBeLoaded(false);
		},
		[this]()
		{
			return !GetStreamingLevel()->IsLevelLoaded();
		},
		[this]()
		{
			return AssertEqual_Int(GetSubsystem()->GetNumTrackedComponents(), TrackedBeforeStreaming, TEXT("Tracked count after unloading"))
				&& CheckLOD(MainTag, 0, TEXT("Main mesh after streaming"));
		},
	});
}

ADistanceLODderFunctionalTest::FStep ADistanceLODderFunctionalTest::MoveStep(float X, int32 ExpectedLOD)
{
	return {
		FString::Printf(TEXT("Move the pawn to %.0f"), X),
		[this, X]()
		{
			Pawn->SetActorLocation(FVector(X, 0.f, 0.f));
		},
		nullptr,
		[this, X, ExpectedLOD]()
		{
			return CheckLOD(MainTag, ExpectedLOD, FString::Printf(TEXT("Main mesh at %.0f"), X));
		},
	};
}

bool ADistanceLODderFunctionalTest::CheckLOD(FName Tag, int32 ExpectedLOD, const FString& What)
{
	const UStaticMeshComponent* Component = FindMesh(Tag);
	if (!AssertTrue(Component != nullptr, FString::Printf(TEXT("%s exists"), *What)))
	{
		return false;
	}
	return AssertEqual_Int(Component->ForcedLodModel - 1, ExpectedLOD, FString::Printf(TEXT("%s forced LOD"), *What));
}

UStaticMeshComponent* ADistanceLODderFunctionalTest::FindMesh(FName Tag) const
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(Tag))
		{
			return It->FindComponentByClass<UStaticMeshComponent>();
		}
	}

	// A hidden but loaded sublevel is no longer in the world's level list.
	for (const ULevelStreaming* Streaming : GetWorld()->GetStreamingLevels())
	{
		if (const ULevel* Level = Streaming ? Streaming->GetLoadedLevel() : nullptr)
		{
			for (const AActor* Actor : Level->Actors)
			{
				if (Actor && Actor->ActorHasTag(Tag))
				{
					return Actor->FindComponentByClass<UStaticMeshComponent>();
				}
			}
		}
	}
	return nullptr;
}

UDistanceLODderSubsystem* ADistanceLODderFunctionalTest::GetSubsystem() const
{
	return GetWorld()->GetSubsystem<UDistanceLODderSubsystem>();
}

ULevelStreaming* ADistanceLODderFunctionalTest::GetStreamingLevel() const
{
	const TArray<ULevelStreaming*>& StreamingLevels = GetWorld()->GetStreamingLevels();
	return StreamingLevels.Num() > 0 ? StreamingLevels[0] : nullptr;
}
