// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"

#include "DistanceLODderTestPawn.generated.h"

/** Pawn whose root is an HMD-locked camera, so its location is the viewpoint DistanceLODder measures from. */
UCLASS(NotPlaceable, Transient)
class ADistanceLODderTestPawn : public APawn
{
	GENERATED_BODY()

public:
	ADistanceLODderTestPawn()
	{
		Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
		Camera->bLockToHmd = true;
		RootComponent = Camera;
	}

	UPROPERTY(VisibleAnywhere, Category = "DistanceLODder")
	TObjectPtr<UCameraComponent> Camera;
};
