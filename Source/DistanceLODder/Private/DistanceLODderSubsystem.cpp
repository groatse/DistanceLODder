// Copyright (c) 2026 groatse. Licensed under the MIT License.

#include "DistanceLODderSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DistanceLODderSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "StaticMeshResources.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DistanceLODderSubsystem)

DEFINE_LOG_CATEGORY_STATIC(LogDistanceLODder, Log, All);

DECLARE_STATS_GROUP(TEXT("DistanceLODder"), STATGROUP_DistanceLODder, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Tick"), STAT_DistanceLODder_Tick, STATGROUP_DistanceLODder);
DECLARE_DWORD_COUNTER_STAT(TEXT("Tracked components"), STAT_DistanceLODder_Tracked, STATGROUP_DistanceLODder);
DECLARE_DWORD_COUNTER_STAT(TEXT("Evaluated this frame"), STAT_DistanceLODder_Evaluated, STATGROUP_DistanceLODder);
DECLARE_DWORD_COUNTER_STAT(TEXT("LOD changes this frame"), STAT_DistanceLODder_Changes, STATGROUP_DistanceLODder);
DECLARE_DWORD_COUNTER_STAT(TEXT("Queued changes"), STAT_DistanceLODder_Queued, STATGROUP_DistanceLODder);

CSV_DEFINE_CATEGORY(DistanceLODder, true);

static TAutoConsoleVariable<int32> CVarDistanceLODderEnabled(
	TEXT("DistanceLODder.Enabled"),
	1,
	TEXT("0: hand all tracked meshes back to UE's own LOD selection. 1: force LODs by distance (default).\n")
	TEXT("Only has an effect when the plugin is enabled in Project Settings."),
	ECVF_Default);

#if ENABLE_DRAW_DEBUG
static TAutoConsoleVariable<int32> CVarDistanceLODderShowDebug(
	TEXT("DistanceLODder.ShowDebug"),
	0,
	TEXT("Draw each tracked mesh's forced LOD. 1: colored point (visible in the HMD). 2: point and text.\n")
	TEXT("Colors: LOD0 white, LOD1 green, LOD2 yellow, LOD3 orange, LOD4+ red, not yet forced grey."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarDistanceLODderDebugDistance(
	TEXT("DistanceLODder.DebugDistance"),
	3000.f,
	TEXT("Only meshes within this distance (cm) of the HMD are drawn by DistanceLODder.ShowDebug."),
	ECVF_Cheat);
#endif

bool UDistanceLODderSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && GetDefault<UDistanceLODderSettings>()->bEnabled;
}

bool UDistanceLODderSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Never editor worlds: the forced LODs must not end up saved into levels.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDistanceLODderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::OnLevelAdded);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &ThisClass::OnLevelRemoved);
	ActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::OnActorSpawned));
}

void UDistanceLODderSubsystem::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	UnregisterAll(/*bResetForcedLOD*/ true);

	Super::Deinitialize();
}

void UDistanceLODderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bHasBegunPlay = true;
	for (ULevel* Level : InWorld.GetLevels())
	{
		if (Level && Level->bIsVisible)
		{
			ScanLevel(Level);
		}
	}

	UE_LOG(LogDistanceLODder, Log, TEXT("Tracking %d static mesh components in %s."), Entries.Num(), *InWorld.GetName());
}

TStatId UDistanceLODderSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDistanceLODderSubsystem, STATGROUP_Tickables);
}

bool UDistanceLODderSubsystem::IsActive() const
{
	return bHasBegunPlay && CVarDistanceLODderEnabled.GetValueOnGameThread() != 0 && GetDefault<UDistanceLODderSettings>()->bEnabled;
}

void UDistanceLODderSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	SCOPE_CYCLE_COUNTER(STAT_DistanceLODder_Tick);
	CSV_SCOPED_TIMING_STAT(DistanceLODder, Tick);

	const uint64 StartCycles = FPlatformTime::Cycles64();
	LastFrameStats = FDistanceLODderFrameStats();
	UpdateLODs();
	LastFrameStats.TickMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
	LastFrameStats.Tracked = Entries.Num();
	LastFrameStats.Queued = ChangeQueue.Num();

	SET_DWORD_STAT(STAT_DistanceLODder_Tracked, LastFrameStats.Tracked);
	SET_DWORD_STAT(STAT_DistanceLODder_Evaluated, LastFrameStats.Evaluated);
	SET_DWORD_STAT(STAT_DistanceLODder_Changes, LastFrameStats.Changed);
	SET_DWORD_STAT(STAT_DistanceLODder_Queued, LastFrameStats.Queued);

	CSV_CUSTOM_STAT(DistanceLODder, Tracked, LastFrameStats.Tracked, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(DistanceLODder, Evaluated, LastFrameStats.Evaluated, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(DistanceLODder, Changed, LastFrameStats.Changed, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(DistanceLODder, Queued, LastFrameStats.Queued, ECsvCustomStatOp::Set);
}

void UDistanceLODderSubsystem::UpdateLODs()
{
	if (!IsActive())
	{
		if (bWasActive)
		{
			ResetForcedLODs();
			bWasActive = false;
		}
		return;
	}

	if (!bWasActive)
	{
		bWasActive = true;
		bInitialPass = true;
		bPassRequested = true;
	}

	FVector Viewpoint;
	if (!GetViewpoint(Viewpoint))
	{
		return;
	}

	const UDistanceLODderSettings* Settings = GetDefault<UDistanceLODderSettings>();
	DistanceFactors = DistanceLODder::FDistanceFactors::Make(Settings->ReferenceVerticalFOV, Settings->DistanceScale, Settings->HysteresisPercent);

	if (PassCursor == INDEX_NONE
		&& (bPassRequested || FVector::DistSquared(Viewpoint, LastPassViewpoint) > FMath::Square(Settings->MovementThreshold)))
	{
		PassCursor = 0;
		bPassRequested = false;
		LastPassViewpoint = Viewpoint;
	}

	int32 NumEvaluated = 0;
	if (PassCursor != INDEX_NONE
		&& EvaluateBatch(Viewpoint, bInitialPass ? MAX_int32 : Settings->MaxComponentsEvaluatedPerFrame, NumEvaluated))
	{
		PassCursor = INDEX_NONE;
	}

	// During loading a hitch doesn't matter, so the first pass applies everything at once.
	const int32 NumChanged = ApplyQueuedChanges(bInitialPass ? MAX_int32 : Settings->MaxLODChangesPerFrame);
	if (bInitialPass && PassCursor == INDEX_NONE)
	{
		bInitialPass = false;
	}

	LastFrameStats.Evaluated = NumEvaluated;
	LastFrameStats.Changed = NumChanged;

#if ENABLE_DRAW_DEBUG
	if (CVarDistanceLODderShowDebug.GetValueOnGameThread() > 0)
	{
		DrawDebug(Viewpoint);
	}
#endif
}

void UDistanceLODderSubsystem::RefreshActor(AActor* Actor)
{
	if (!Actor || Actor->GetWorld() != GetWorld())
	{
		return;
	}

	TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
	for (UStaticMeshComponent* Component : Components)
	{
		if (const int32* Index = ComponentToEntry.Find(Component))
		{
			UnregisterEntry(*Index, /*bResetForcedLOD*/ true);
		}
		RegisterComponent(Component);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Registration

void UDistanceLODderSubsystem::ScanLevel(ULevel* Level)
{
	for (AActor* Actor : Level->Actors)
	{
		RegisterActor(Actor);
	}
}

void UDistanceLODderSubsystem::RegisterActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
	for (UStaticMeshComponent* Component : Components)
	{
		RegisterComponent(Component);
	}
}

void UDistanceLODderSubsystem::RegisterComponent(UStaticMeshComponent* Component)
{
	if (!PassesFilter(Component) || ComponentToEntry.Contains(Component))
	{
		return;
	}

	FTrackedMesh Entry;
	if (!BuildEntry(Component, Entry))
	{
		return;
	}

	const int32 Index = Entries.Add(MoveTemp(Entry));
	ComponentToEntry.Add(Component, Index);
	bPassRequested = true;
}

bool UDistanceLODderSubsystem::PassesFilter(const UStaticMeshComponent* Component) const
{
	if (!IsValid(Component) || !Component->IsRegistered())
	{
		return false;
	}

	// Instanced meshes pick LODs per instance/cluster; a forced LOD would apply to all of them.
	if (Component->IsA<UInstancedStaticMeshComponent>())
	{
		return false;
	}

	// Bounds are cached once, so only meshes that can't move.
	if (Component->GetMobility() == EComponentMobility::Movable)
	{
		return false;
	}

	// Someone else already forces a LOD on it.
	if (Component->ForcedLodModel > 0)
	{
		return false;
	}

	if (Component->HasValidNaniteData())
	{
		return false;
	}

	const FName OptOutTag = GetDefault<UDistanceLODderSettings>()->OptOutTag;
	if (!OptOutTag.IsNone())
	{
		const AActor* Owner = Component->GetOwner();
		if (Component->ComponentHasTag(OptOutTag) || (Owner && Owner->ActorHasTag(OptOutTag)))
		{
			return false;
		}
	}

	return true;
}

bool UDistanceLODderSubsystem::BuildEntry(UStaticMeshComponent* Component, FTrackedMesh& OutEntry) const
{
	const UStaticMesh* Mesh = Component->GetStaticMesh();
	const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
	if (!RenderData)
	{
		return false;
	}

	const UDistanceLODderSettings* Settings = GetDefault<UDistanceLODderSettings>();
	const bool bGlobal = Settings->bUseGlobalScreenSizes;

	int32 NumLODs = FMath::Min(RenderData->LODResources.Num(), DistanceLODder::MaxLODs);
	if (bGlobal)
	{
		NumLODs = FMath::Min(NumLODs, UDistanceLODderSettings::NumGlobalLODs);
	}

	TArray<float, TInlineAllocator<DistanceLODder::MaxLODs>> ScreenSizes;
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		ScreenSizes.Add(bGlobal ? Settings->GetGlobalScreenSize(LODIndex) : RenderData->ScreenSize[LODIndex].GetValue());
	}

	const int32 MinLOD = Component->GetOverrideMinLOD() ? Component->GetMinLOD() : Mesh->GetMinLODIdx();
	if (!DistanceLODder::BuildThresholds(Component->Bounds.SphereRadius, ScreenSizes, MinLOD, OutEntry.Thresholds))
	{
		return false;
	}

	OutEntry.Component = Component;
	OutEntry.Key = Component;
	OutEntry.Origin = Component->Bounds.Origin;
	return true;
}

void UDistanceLODderSubsystem::UnregisterEntry(int32 Index, bool bResetForcedLOD)
{
	FTrackedMesh& Entry = Entries[Index];
	if (bResetForcedLOD && Entry.CurrentLOD != INDEX_NONE)
	{
		// Only undo our own value.
		UStaticMeshComponent* Component = Entry.Component.Get();
		if (Component && Component->ForcedLodModel == Entry.CurrentLOD + 1)
		{
			Component->SetForcedLodModel(0);
		}
	}

	ComponentToEntry.Remove(Entry.Key);
	Entries.RemoveAt(Index);
}

void UDistanceLODderSubsystem::UnregisterAll(bool bResetForcedLOD)
{
	if (bResetForcedLOD)
	{
		ResetForcedLODs();
	}

	Entries.Empty();
	ComponentToEntry.Empty();
	ChangeQueue.Empty();
	PassCursor = INDEX_NONE;
}

void UDistanceLODderSubsystem::OnLevelAdded(ULevel* Level, UWorld* World)
{
	if (World == GetWorld() && Level && bHasBegunPlay)
	{
		ScanLevel(Level);
	}
}

void UDistanceLODderSubsystem::OnLevelRemoved(ULevel* Level, UWorld* World)
{
	if (World != GetWorld())
	{
		return;
	}

	// Reset forced LODs too: a hidden (not unloaded) level keeps its components, and they'd be skipped on re-show otherwise.
	TArray<int32, TInlineAllocator<256>> ToRemove;
	for (auto It = Entries.CreateConstIterator(); It; ++It)
	{
		const UStaticMeshComponent* Component = It->Component.Get();
		if (!Level || !Component || Component->GetComponentLevel() == Level)
		{
			ToRemove.Add(It.GetIndex());
		}
	}

	for (const int32 Index : ToRemove)
	{
		UnregisterEntry(Index, /*bResetForcedLOD*/ true);
	}
}

void UDistanceLODderSubsystem::OnActorSpawned(AActor* Actor)
{
	if (bHasBegunPlay)
	{
		RegisterActor(Actor);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Update

bool UDistanceLODderSubsystem::GetViewpoint(FVector& OutLocation)
{
	if (ViewpointOverride.IsSet())
	{
		OutLocation = ViewpointOverride.GetValue();
		return true;
	}

	const APlayerController* PlayerController = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	if (Pawn != CachedPawn.Get())
	{
		CachedPawn = Pawn;
		CachedCamera = nullptr;

		TInlineComponentArray<UCameraComponent*> Cameras(Pawn);
		for (UCameraComponent* Camera : Cameras)
		{
			if (Camera->bLockToHmd)
			{
				CachedCamera = Camera;
				break;
			}
		}
	}

	// The HMD camera sits between the eyes. Its game-thread pose is about a frame older than the rendered one, which doesn't matter here.
	if (const UCameraComponent* Camera = CachedCamera.Get())
	{
		OutLocation = Camera->GetComponentLocation();
		return true;
	}

	FRotator Rotation;
	Pawn->GetActorEyesViewPoint(OutLocation, Rotation);
	return true;
}

bool UDistanceLODderSubsystem::EvaluateBatch(const FVector& Viewpoint, int32 MaxCount, int32& NumEvaluated)
{
	const int32 MaxIndex = Entries.GetMaxIndex();

	while (PassCursor < MaxIndex && NumEvaluated < MaxCount)
	{
		const int32 Index = PassCursor++;
		if (!Entries.IsAllocated(Index))
		{
			continue;
		}

		FTrackedMesh& Entry = Entries[Index];
		const UStaticMeshComponent* Component = Entry.Component.Get();
		if (!Component || !Component->IsRegistered())
		{
			UnregisterEntry(Index, /*bResetForcedLOD*/ true);
			continue;
		}

		// Someone else changed the forced LOD since we set it: hand the component over to them.
		if (Entry.CurrentLOD != INDEX_NONE && Component->ForcedLodModel != Entry.CurrentLOD + 1)
		{
			UnregisterEntry(Index, /*bResetForcedLOD*/ false);
			continue;
		}

		++NumEvaluated;
		Entry.LastDistSq = static_cast<float>(FVector::DistSquared(Viewpoint, Entry.Origin));

		const int32 NewLOD = DistanceLODder::ComputeLOD(Entry.Thresholds, Entry.CurrentLOD, Entry.LastDistSq, DistanceFactors);
		if (NewLOD != Entry.CurrentLOD)
		{
			if (Entry.PendingLOD == INDEX_NONE)
			{
				ChangeQueue.Add(Index);
			}
			Entry.PendingLOD = static_cast<int8>(NewLOD);
			bChangeQueueDirty = true;
		}
		else
		{
			// Moved back before the queued change was applied; the stale queue slot gets skipped.
			Entry.PendingLOD = INDEX_NONE;
		}
	}

	return PassCursor >= MaxIndex;
}

int32 UDistanceLODderSubsystem::ApplyQueuedChanges(int32 MaxCount)
{
	if (bChangeQueueDirty)
	{
		ChangeQueue.RemoveAllSwap([this](int32 Index)
		{
			return !Entries.IsAllocated(Index) || Entries[Index].PendingLOD == INDEX_NONE;
		}, EAllowShrinking::No);

		// Farthest first, so the nearest pop off the back.
		ChangeQueue.Sort([this](int32 A, int32 B)
		{
			return Entries[A].LastDistSq > Entries[B].LastDistSq;
		});

		bChangeQueueDirty = false;
	}

	int32 NumApplied = 0;
	while (NumApplied < MaxCount && ChangeQueue.Num() > 0)
	{
		const int32 Index = ChangeQueue.Pop(EAllowShrinking::No);
		if (!Entries.IsAllocated(Index))
		{
			continue;
		}

		FTrackedMesh& Entry = Entries[Index];
		if (Entry.PendingLOD == INDEX_NONE)
		{
			continue;
		}

		UStaticMeshComponent* Component = Entry.Component.Get();
		if (!Component)
		{
			UnregisterEntry(Index, /*bResetForcedLOD*/ false);
			continue;
		}

		// Rebuilds the component's render state (CPU cost), hence the budget.
		Component->SetForcedLodModel(Entry.PendingLOD + 1);
		Entry.CurrentLOD = Entry.PendingLOD;
		Entry.PendingLOD = INDEX_NONE;
		++NumApplied;
	}

	return NumApplied;
}

void UDistanceLODderSubsystem::ResetForcedLODs()
{
	for (FTrackedMesh& Entry : Entries)
	{
		UStaticMeshComponent* Component = Entry.Component.Get();
		if (Component && Entry.CurrentLOD != INDEX_NONE && Component->ForcedLodModel == Entry.CurrentLOD + 1)
		{
			Component->SetForcedLodModel(0);
		}
		Entry.CurrentLOD = INDEX_NONE;
		Entry.PendingLOD = INDEX_NONE;
	}

	ChangeQueue.Reset();
	PassCursor = INDEX_NONE;
}

// ---------------------------------------------------------------------------------------------------------------------
// Debug

#if ENABLE_DRAW_DEBUG
void UDistanceLODderSubsystem::DrawDebug(const FVector& Viewpoint) const
{
	static const FColor LODColors[] = { FColor::White, FColor::Green, FColor::Yellow, FColor::Orange, FColor::Red };

	UWorld* World = GetWorld();
	const bool bDrawText = CVarDistanceLODderShowDebug.GetValueOnGameThread() >= 2;
	const float MaxDistSq = FMath::Square(CVarDistanceLODderDebugDistance.GetValueOnGameThread());

	for (const FTrackedMesh& Entry : Entries)
	{
		if (FVector::DistSquared(Viewpoint, Entry.Origin) > MaxDistSq)
		{
			continue;
		}

		const FColor Color = Entry.CurrentLOD == INDEX_NONE ? FColor(128, 128, 128) : LODColors[FMath::Min<int32>(Entry.CurrentLOD, UE_ARRAY_COUNT(LODColors) - 1)];
		DrawDebugPoint(World, Entry.Origin, 12.f, Color);

		if (bDrawText)
		{
			const FString Text = Entry.CurrentLOD == INDEX_NONE ? FString(TEXT("-")) : FString::Printf(TEXT("LOD%d"), Entry.CurrentLOD);
			DrawDebugString(World, Entry.Origin, Text, nullptr, Color, 0.f, false, 1.f);
		}
	}
}
#endif
