// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "Containers/SparseArray.h"
#include "DistanceLODderMath.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"

#include "DistanceLODderSubsystem.generated.h"

class AActor;
class FDistanceLODderWorldSpec;
class APawn;
class UCameraComponent;
class ULevel;
class UStaticMeshComponent;

/** What the subsystem did in its last tick. */
struct FDistanceLODderFrameStats
{
	/**
	 * Time spent in the subsystem's tick. Doesn't include the render state rebuilds the LOD changes cause,
	 * which happen later in the frame (end-of-frame updates and the render thread).
	 */
	double TickMs = 0.0;
	int32 Evaluated = 0;
	int32 Changed = 0;
	int32 Queued = 0;
	int32 Tracked = 0;
};

/**
 * Forces static mesh LODs from the distance to the player pawn's HMD camera, with hysteresis,
 * instead of letting UE pick them from the (gaze-dependent) LOD view every frame.
 *
 * Tracks Static and Stationary UStaticMeshComponents with 2+ LODs. Skips instanced meshes, Nanite meshes,
 * components that already have a forced LOD and anything tagged with the opt-out tag.
 * Runs in game and PIE worlds only, so forced LODs never get saved into levels.
 */
UCLASS()
class DISTANCELODDER_API UDistanceLODderSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Re-checks the actor's static mesh components, e.g. after its tags or meshes changed at runtime. */
	UFUNCTION(BlueprintCallable, Category = "DistanceLODder")
	void RefreshActor(AActor* Actor);

	/** Re-evaluates every tracked mesh on the next tick even if the HMD hasn't moved. */
	UFUNCTION(BlueprintCallable, Category = "DistanceLODder")
	void RequestFullUpdate() { bPassRequested = true; }

	UFUNCTION(BlueprintPure, Category = "DistanceLODder")
	int32 GetNumTrackedComponents() const { return Entries.Num(); }

	const FDistanceLODderFrameStats& GetLastFrameStats() const { return LastFrameStats; }

	UFUNCTION(BlueprintPure, Category = "DistanceLODder")
	bool IsComponentTracked(const UStaticMeshComponent* Component) const { return ComponentToEntry.Contains(Component); }

	/** Measures distances from this location instead of the player pawn's HMD camera. Used by the tests. */
	void SetViewpointOverride(const FVector& Location) { ViewpointOverride = Location; }
	void ClearViewpointOverride() { ViewpointOverride.Reset(); }

protected:
	friend class FDistanceLODderWorldSpec; // Calls the level handlers directly instead of broadcasting the global delegates.

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FTrackedMesh
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		TObjectKey<UStaticMeshComponent> Key;
		FVector Origin = FVector::ZeroVector;
		DistanceLODder::FLODThresholds Thresholds;
		float LastDistSq = 0.f;
		/** LOD we've forced, INDEX_NONE until first applied. */
		int8 CurrentLOD = INDEX_NONE;
		/** LOD waiting in the change queue, INDEX_NONE when not queued. */
		int8 PendingLOD = INDEX_NONE;
	};

	bool IsActive() const;
	void UpdateLODs();

	void ScanLevel(ULevel* Level);
	void RegisterActor(AActor* Actor);
	void RegisterComponent(UStaticMeshComponent* Component);
	void UnregisterEntry(int32 Index, bool bResetForcedLOD);
	void UnregisterAll(bool bResetForcedLOD);
	bool PassesFilter(const UStaticMeshComponent* Component) const;
	bool BuildEntry(UStaticMeshComponent* Component, FTrackedMesh& OutEntry) const;

	bool GetViewpoint(FVector& OutLocation);

	/** Evaluates up to MaxCount entries starting at the pass cursor. Returns true when the pass is finished. */
	bool EvaluateBatch(const FVector& Viewpoint, int32 MaxCount, int32& NumEvaluated);
	/** Applies up to MaxCount queued changes, nearest first. */
	int32 ApplyQueuedChanges(int32 MaxCount);
	/** Forces every tracked component back to UE's own LOD selection, keeps tracking them. */
	void ResetForcedLODs();

	void OnLevelAdded(ULevel* Level, UWorld* World);
	void OnLevelRemoved(ULevel* Level, UWorld* World);
	void OnActorSpawned(AActor* Actor);

#if ENABLE_DRAW_DEBUG
	void DrawDebug(const FVector& Viewpoint) const;
#endif

	TSparseArray<FTrackedMesh> Entries;
	TMap<TObjectKey<UStaticMeshComponent>, int32> ComponentToEntry;

	/** Entry indices with a PendingLOD. May hold stale or duplicate indices; PendingLOD is the source of truth. */
	TArray<int32> ChangeQueue;
	bool bChangeQueueDirty = false;

	TWeakObjectPtr<APawn> CachedPawn;
	TWeakObjectPtr<UCameraComponent> CachedCamera;
	TOptional<FVector> ViewpointOverride;

	/** Refreshed every tick from the settings, so they can be tuned live. */
	DistanceLODder::FDistanceFactors DistanceFactors;

	FVector LastPassViewpoint = FVector(UE_BIG_NUMBER);
	int32 PassCursor = INDEX_NONE; // INDEX_NONE = no pass in progress
	bool bPassRequested = true;
	/** The first pass after begin play or re-enabling applies everything at once, without the per-frame budgets. */
	bool bInitialPass = true;
	bool bHasBegunPlay = false;
	bool bWasActive = false;

	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;
	FDelegateHandle ActorSpawnedHandle;

	FDistanceLODderFrameStats LastFrameStats;
};
