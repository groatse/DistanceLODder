// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"

namespace DistanceLODderTests
{
	/**
	 * Spawns a static mesh actor with the given mesh and mobility, also after begin play.
	 * SetStaticMesh refuses registered Static components after begin play, and deferred spawning already
	 * registers native components, so the component is set up while unregistered.
	 */
	inline UStaticMeshComponent* SpawnStaticMeshActor(UWorld* World, UStaticMesh* Mesh, const FVector& Location, EComponentMobility::Type Mobility = EComponentMobility::Static)
	{
		const FTransform Transform(Location);
		AStaticMeshActor* Actor = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		const bool bWasRegistered = Component->IsRegistered();
		if (bWasRegistered)
		{
			Component->UnregisterComponent();
		}
		Component->SetMobility(Mobility);
		Component->SetStaticMesh(Mesh);
		if (bWasRegistered)
		{
			Component->RegisterComponent();
		}
		Actor->FinishSpawning(Transform);
		return Component;
	}
}
