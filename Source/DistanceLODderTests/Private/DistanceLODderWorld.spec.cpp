// Copyright (c) 2026 groatse. Licensed under the MIT License.

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DistanceLODderSettings.h"
#include "DistanceLODderSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DistanceLODderTests
{
	/** Builds a transient mesh with NumLODs identical LODs. LOD i (i >= 1) starts at SwitchDistances[i - 1] with a 90 degree reference FOV. */
	UStaticMesh* CreateTestMesh(int32 NumLODs, TConstArrayView<float> SwitchDistances)
	{
		FMeshDescription MeshDescription;
		FStaticMeshAttributes Attributes(MeshDescription);
		Attributes.Register();

		const FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();
		const FVector3f Positions[] = { { -50.f, -50.f, 0.f }, { 50.f, -50.f, 0.f }, { 0.f, 50.f, 0.f } };
		TArray<FVertexInstanceID, TInlineAllocator<3>> VertexInstances;
		for (const FVector3f& Position : Positions)
		{
			const FVertexID Vertex = MeshDescription.CreateVertex();
			Attributes.GetVertexPositions()[Vertex] = Position;
			VertexInstances.Add(MeshDescription.CreateVertexInstance(Vertex));
		}
		MeshDescription.CreateTriangle(PolygonGroup, VertexInstances);

		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial());

		TArray<const FMeshDescription*> LODs;
		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			LODs.Add(&MeshDescription);
		}

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bCommitMeshDescription = false;
		Params.bMarkPackageDirty = false;
		Params.bAllowCpuAccess = true; // Collision cooking needs the CPU copy.
		Mesh->BuildFromMeshDescriptions(LODs, Params);

		// Screen size R / D puts the switch at distance D (magnification 1 at 90 degrees).
		FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		const float Radius = RenderData->Bounds.SphereRadius;
		RenderData->ScreenSize[0].Default = 1.f;
		for (int32 LODIndex = 1; LODIndex < NumLODs; ++LODIndex)
		{
			RenderData->ScreenSize[LODIndex].Default = SwitchDistances.IsValidIndex(LODIndex - 1) ? Radius / SwitchDistances[LODIndex - 1] : 0.f;
		}

		return Mesh;
	}
}

BEGIN_DEFINE_SPEC(FDistanceLODderWorldSpec, "DistanceLODder.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	TUniquePtr<FTestWorldWrapper> WorldWrapper;
	UWorld* World = nullptr;

	/** 4 LODs switching at 1000 / 2000 / 4000 (coarser at 1100 / 2200 / 4400, finer at 900 / 1800 / 3600 with 10% hysteresis). */
	TStrongObjectPtr<UStaticMesh> Mesh;
	TStrongObjectPtr<UDistanceLODderSettings> SavedSettings;
	int32 SavedEnabledCVar = 1;

	UDistanceLODderSubsystem* Subsystem() const
	{
		return World ? World->GetSubsystem<UDistanceLODderSubsystem>() : nullptr;
	}

	UStaticMeshComponent* SpawnMesh(const FVector& Location, EComponentMobility::Type Mobility = EComponentMobility::Static, UStaticMesh* InMesh = nullptr)
	{
		// SetStaticMesh refuses registered Static components after begin play, and deferred spawning already
		// registers native components, so set up the component while it's unregistered.
		const FTransform Transform(Location);
		AStaticMeshActor* Actor = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		const bool bWasRegistered = Component->IsRegistered();
		if (bWasRegistered)
		{
			Component->UnregisterComponent();
		}
		Component->SetMobility(Mobility);
		Component->SetStaticMesh(InMesh ? InMesh : Mesh.Get());
		if (bWasRegistered)
		{
			Component->RegisterComponent();
		}
		Actor->FinishSpawning(Transform);
		TestNotNull(TEXT("Spawned mesh"), Component->GetStaticMesh().Get());
		return Component;
	}

	void StartPlay()
	{
		WorldWrapper->BeginPlayInTestWorld();
		WorldWrapper->ForwardErrorMessages(this);
	}

	void TickAt(const FVector& Viewpoint)
	{
		Subsystem()->SetViewpointOverride(Viewpoint);
		Subsystem()->Tick(0.01f);
	}

	static int32 ForcedLOD(const UStaticMeshComponent* Component)
	{
		return Component->ForcedLodModel - 1;
	}

	static void SetEnabledCVar(int32 Value)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("DistanceLODder.Enabled"))->Set(Value, ECVF_SetByCode);
	}

	static UDistanceLODderSettings* Settings()
	{
		return GetMutableDefault<UDistanceLODderSettings>();
	}

END_DEFINE_SPEC(FDistanceLODderWorldSpec)

void FDistanceLODderWorldSpec::Define()
{
	BeforeEach([this]()
	{
		// Snapshot the project settings and set known values.
		SavedSettings.Reset(NewObject<UDistanceLODderSettings>(GetTransientPackage()));
		for (TFieldIterator<FProperty> It(UDistanceLODderSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			It->CopyCompleteValue_InContainer(SavedSettings.Get(), Settings());
		}

		UDistanceLODderSettings* S = Settings();
		S->bEnabled = true;
		S->OptOutTag = TEXT("NoDistanceLOD");
		S->ReferenceVerticalFOV = 90.f;
		S->DistanceScale = 1.f;
		S->HysteresisPercent = 10.f;
		S->bUseGlobalScreenSizes = false;
		S->MovementThreshold = 25.f;
		S->MaxLODChangesPerFrame = 32;
		S->MaxComponentsEvaluatedPerFrame = 4096;

		SavedEnabledCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("DistanceLODder.Enabled"))->GetInt();
		SetEnabledCVar(1);

		const float Distances[] = { 1000.f, 2000.f, 4000.f };
		Mesh.Reset(DistanceLODderTests::CreateTestMesh(4, Distances));

		WorldWrapper = MakeUnique<FTestWorldWrapper>();
		WorldWrapper->CreateTestWorld(EWorldType::Game);
		WorldWrapper->ForwardErrorMessages(this);
		World = WorldWrapper->GetTestWorld();
		TestNotNull(TEXT("Subsystem"), Subsystem());
	});

	AfterEach([this]()
	{
		if (WorldWrapper)
		{
			if (World)
			{
				WorldWrapper->DestroyTestWorld(false);
			}
			WorldWrapper.Reset();
		}
		World = nullptr;
		Mesh.Reset();

		for (TFieldIterator<FProperty> It(UDistanceLODderSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			It->CopyCompleteValue_InContainer(Settings(), SavedSettings.Get());
		}
		SavedSettings.Reset();
		SetEnabledCVar(SavedEnabledCVar);
	});

	Describe("Registration", [this]()
	{
		It("tracks Static and Stationary meshes placed before begin play", [this]()
		{
			UStaticMeshComponent* Static = SpawnMesh(FVector(0, 0, 0));
			UStaticMeshComponent* Stationary = SpawnMesh(FVector(0, 500, 0), EComponentMobility::Stationary);
			StartPlay();
			TestTrue(TEXT("Static tracked"), Subsystem()->IsComponentTracked(Static));
			TestTrue(TEXT("Stationary tracked"), Subsystem()->IsComponentTracked(Stationary));
			TestEqual(TEXT("Tracked count"), Subsystem()->GetNumTrackedComponents(), 2);
		});

		It("tracks meshes spawned after begin play", [this]()
		{
			StartPlay();
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			TestTrue(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
		});

		It("skips Movable meshes", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector, EComponentMobility::Movable);
			StartPlay();
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
		});

		It("skips actors with the opt-out tag", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			Component->GetOwner()->Tags.Add(TEXT("NoDistanceLOD"));
			StartPlay();
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
		});

		It("skips components with the opt-out tag", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			Component->ComponentTags.Add(TEXT("NoDistanceLOD"));
			StartPlay();
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
		});

		It("skips components whose LOD is already forced", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			Component->ForcedLodModel = 2;
			StartPlay();
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
			TickAt(FVector(5000, 0, 0));
			TestEqual(TEXT("ForcedLodModel untouched"), Component->ForcedLodModel, 2);
		});

		It("skips meshes with a single LOD", [this]()
		{
			UStaticMesh* SingleLOD = DistanceLODderTests::CreateTestMesh(1, {});
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector, EComponentMobility::Static, SingleLOD);
			StartPlay();
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
		});

		It("skips instanced static meshes", [this]()
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UInstancedStaticMeshComponent* Instanced = NewObject<UInstancedStaticMeshComponent>(Actor);
			Instanced->SetMobility(EComponentMobility::Static);
			Instanced->SetStaticMesh(Mesh.Get());
			Actor->SetRootComponent(Instanced);
			Instanced->RegisterComponent();
			Instanced->AddInstance(FTransform::Identity);
			StartPlay();
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Instanced));
		});

		It("caps meshes at LOD3 with global screen sizes", [this]()
		{
			Settings()->bUseGlobalScreenSizes = true;
			const float Distances[] = { 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector, EComponentMobility::Static, DistanceLODderTests::CreateTestMesh(6, Distances));
			StartPlay();
			TickAt(FVector(1e6, 0, 0));
			TestEqual(TEXT("LOD"), ForcedLOD(Component), 3);
		});

		It("uses the global screen sizes instead of the mesh's", [this]()
		{
			Settings()->bUseGlobalScreenSizes = true;
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();

			// LOD1 starts at R / 0.5 = 2R, far closer than the mesh's own 1000.
			const float Radius = Component->Bounds.SphereRadius;
			TickAt(FVector(2.f * Radius * 1.05f, 0, 0));
			TestEqual(TEXT("LOD just beyond 2R"), ForcedLOD(Component), 1);
		});
	});

	Describe("LOD selection", [this]()
	{
		It("does nothing without a player pawn or viewpoint override", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();
			Subsystem()->Tick(0.01f);
			TestEqual(TEXT("ForcedLodModel"), Component->ForcedLodModel, 0);
		});

		It("forces every mesh on the first pass, ignoring the change budget", [this]()
		{
			Settings()->MaxLODChangesPerFrame = 1;
			TArray<UStaticMeshComponent*> Components = {
				SpawnMesh(FVector(500, 0, 0)), SpawnMesh(FVector(1500, 0, 0)),
				SpawnMesh(FVector(3000, 0, 0)), SpawnMesh(FVector(5000, 0, 0)),
			};
			StartPlay();
			TickAt(FVector::ZeroVector);
			TestEqual(TEXT("500"), ForcedLOD(Components[0]), 0);
			TestEqual(TEXT("1500"), ForcedLOD(Components[1]), 1);
			TestEqual(TEXT("3000"), ForcedLOD(Components[2]), 2);
			TestEqual(TEXT("5000"), ForcedLOD(Components[3]), 3);
		});

		It("applies hysteresis around the switch distance", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();

			TickAt(FVector(800, 0, 0));
			TestEqual(TEXT("800"), ForcedLOD(Component), 0);
			TickAt(FVector(1050, 0, 0));
			TestEqual(TEXT("1050, inside band"), ForcedLOD(Component), 0);
			TickAt(FVector(1150, 0, 0));
			TestEqual(TEXT("1150, beyond band"), ForcedLOD(Component), 1);
			TickAt(FVector(950, 0, 0));
			TestEqual(TEXT("950, inside band"), ForcedLOD(Component), 1);
			TickAt(FVector(850, 0, 0));
			TestEqual(TEXT("850, beyond band"), ForcedLOD(Component), 0);
		});

		It("waits until the viewpoint has moved past the movement threshold", [this]()
		{
			Settings()->MovementThreshold = 500.f;
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();

			TickAt(FVector(900, 0, 0));
			TestEqual(TEXT("Start"), ForcedLOD(Component), 0);
			TickAt(FVector(1150, 0, 0));
			TestEqual(TEXT("Moved 250"), ForcedLOD(Component), 0);
			TickAt(FVector(1450, 0, 0));
			TestEqual(TEXT("Moved 550"), ForcedLOD(Component), 1);
		});

		It("applies at most MaxLODChangesPerFrame changes per tick, nearest first", [this]()
		{
			TArray<UStaticMeshComponent*> Components;
			for (int32 i = 0; i < 5; ++i)
			{
				Components.Add(SpawnMesh(FVector(i * 10.f, 0, 0)));
			}
			StartPlay();
			TickAt(FVector(0, 500, 0));

			Settings()->MaxLODChangesPerFrame = 2;
			TickAt(FVector(0, 3000, 0));
			TestEqual(TEXT("Tick 1: nearest"), ForcedLOD(Components[0]), 2);
			TestEqual(TEXT("Tick 1: second"), ForcedLOD(Components[1]), 2);
			TestEqual(TEXT("Tick 1: third waits"), ForcedLOD(Components[2]), 0);

			TickAt(FVector(0, 3000, 0));
			TestEqual(TEXT("Tick 2: third"), ForcedLOD(Components[2]), 2);
			TestEqual(TEXT("Tick 2: fourth"), ForcedLOD(Components[3]), 2);
			TestEqual(TEXT("Tick 2: fifth waits"), ForcedLOD(Components[4]), 0);

			TickAt(FVector(0, 3000, 0));
			TestEqual(TEXT("Tick 3: fifth"), ForcedLOD(Components[4]), 2);
		});

		It("spreads a pass over several ticks with MaxComponentsEvaluatedPerFrame", [this]()
		{
			TArray<UStaticMeshComponent*> Components;
			for (int32 i = 0; i < 5; ++i)
			{
				Components.Add(SpawnMesh(FVector(i * 10.f, 0, 0)));
			}
			StartPlay();
			TickAt(FVector(0, 500, 0));

			Settings()->MaxComponentsEvaluatedPerFrame = 2;
			auto CountAtLOD2 = [&Components]()
			{
				return Components.FilterByPredicate([](const UStaticMeshComponent* C) { return C->ForcedLodModel - 1 == 2; }).Num();
			};

			TickAt(FVector(0, 3000, 0));
			TestEqual(TEXT("Tick 1"), CountAtLOD2(), 2);
			TickAt(FVector(0, 3000, 0));
			TestEqual(TEXT("Tick 2"), CountAtLOD2(), 4);
			TickAt(FVector(0, 3000, 0));
			TestEqual(TEXT("Tick 3"), CountAtLOD2(), 5);
		});

		It("applies the distance scale live", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();
			TickAt(FVector(1500, 0, 0));
			TestEqual(TEXT("Scale 1"), ForcedLOD(Component), 1);

			Settings()->DistanceScale = 2.f;
			Subsystem()->RequestFullUpdate();
			TickAt(FVector(1500, 0, 0));
			TestEqual(TEXT("Scale 2"), ForcedLOD(Component), 0);
		});
	});

	Describe("Handing components back", [this]()
	{
		It("resets forced LODs when disabled with the console variable and re-applies them when enabled", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();
			TickAt(FVector(1500, 0, 0));
			TestEqual(TEXT("Before"), ForcedLOD(Component), 1);

			SetEnabledCVar(0);
			TickAt(FVector(1500, 0, 0));
			TestEqual(TEXT("Disabled"), Component->ForcedLodModel, 0);
			TestTrue(TEXT("Still tracked"), Subsystem()->IsComponentTracked(Component));

			SetEnabledCVar(1);
			TickAt(FVector(1500, 0, 0));
			TestEqual(TEXT("Re-enabled"), ForcedLOD(Component), 1);
		});

		It("lets go of a component when something else changes its forced LOD", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();
			TickAt(FVector(500, 0, 0));

			Component->SetForcedLodModel(4);
			Subsystem()->RequestFullUpdate();
			TickAt(FVector(1500, 0, 0));
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
			TestEqual(TEXT("ForcedLodModel kept"), Component->ForcedLodModel, 4);
		});

		It("drops and resets unregistered components", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();
			TickAt(FVector(1500, 0, 0));

			Component->UnregisterComponent();
			Subsystem()->RequestFullUpdate();
			TickAt(FVector(1500, 0, 0));
			TestFalse(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
			TestEqual(TEXT("ForcedLodModel"), Component->ForcedLodModel, 0);
		});

		It("drops destroyed actors", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			SpawnMesh(FVector(0, 500, 0));
			StartPlay();
			TickAt(FVector(1500, 0, 0));

			Component->GetOwner()->Destroy();
			Subsystem()->RequestFullUpdate();
			TickAt(FVector(1500, 0, 0));
			TestEqual(TEXT("Tracked count"), Subsystem()->GetNumTrackedComponents(), 1);
		});

		It("re-checks tags with RefreshActor", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			AActor* Actor = Component->GetOwner();
			StartPlay();
			TickAt(FVector(1500, 0, 0));

			Actor->Tags.Add(TEXT("NoDistanceLOD"));
			Subsystem()->RefreshActor(Actor);
			TestFalse(TEXT("Tagged: tracked"), Subsystem()->IsComponentTracked(Component));
			TestEqual(TEXT("Tagged: ForcedLodModel"), Component->ForcedLodModel, 0);

			Actor->Tags.Remove(TEXT("NoDistanceLOD"));
			Subsystem()->RefreshActor(Actor);
			TickAt(FVector(1500, 0, 0));
			TestTrue(TEXT("Untagged: tracked"), Subsystem()->IsComponentTracked(Component));
			TestEqual(TEXT("Untagged: LOD"), ForcedLOD(Component), 1);
		});

		// These call the level handlers directly; broadcasting the global delegates would also reach the editor's
		// listeners. Real level streaming would need a sublevel asset.
		It("drops and resets a level's meshes when the level is removed, and picks them up when it's added", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();
			TickAt(FVector(1500, 0, 0));

			Subsystem()->OnLevelRemoved(World->PersistentLevel, World);
			TestFalse(TEXT("Removed: tracked"), Subsystem()->IsComponentTracked(Component));
			TestEqual(TEXT("Removed: ForcedLodModel"), Component->ForcedLodModel, 0);

			Subsystem()->OnLevelAdded(World->PersistentLevel, World);
			TickAt(FVector(1500, 0, 0));
			TestTrue(TEXT("Added: tracked"), Subsystem()->IsComponentTracked(Component));
			TestEqual(TEXT("Added: LOD"), ForcedLOD(Component), 1);
		});

		It("drops everything when all levels are removed (map change)", [this]()
		{
			SpawnMesh(FVector::ZeroVector);
			SpawnMesh(FVector(0, 500, 0));
			StartPlay();
			TickAt(FVector(1500, 0, 0));

			Subsystem()->OnLevelRemoved(nullptr, World);
			TestEqual(TEXT("Tracked count"), Subsystem()->GetNumTrackedComponents(), 0);
		});

		It("ignores level events from other worlds", [this]()
		{
			UStaticMeshComponent* Component = SpawnMesh(FVector::ZeroVector);
			StartPlay();
			Subsystem()->OnLevelRemoved(nullptr, nullptr);
			TestTrue(TEXT("Tracked"), Subsystem()->IsComponentTracked(Component));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
