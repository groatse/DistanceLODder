// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "DistanceLODderSettings.generated.h"

/**
 * Project Settings -> Plugins -> DistanceLODder.
 *
 * Static mesh LODs are picked from the distance between the player pawn's HMD camera and each mesh,
 * using a fixed reference field of view instead of the (gaze-dependent) view magnification.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "DistanceLODder"))
class DISTANCELODDER_API UDistanceLODderSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDistanceLODderSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Master switch. When off, the subsystem isn't created and UE picks LODs as usual. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	/**
	 * Static and Stationary static mesh components whose actor (or the component itself) has this tag are left to UE's own LOD system.
	 * Tags are read when the component is picked up; call UDistanceLODderSubsystem::RefreshActor after changing them at runtime.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	FName OptOutTag = TEXT("NoDistanceLOD");

	/**
	 * FOV of the narrower (usually vertical) axis of the reference view the screen sizes are computed against.
	 * UE measures screen size against the narrower axis (max(M00, M11) of the projection).
	 * 59 degrees matches the editor viewport at 90 degrees horizontal and 16:9, so LOD screen sizes tuned
	 * in the editor behave the same in the headset.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "LOD", meta = (ClampMin = "10.0", ClampMax = "170.0", Units = "Degrees"))
	float ReferenceVerticalFOV = 59.f;

	/** Multiplies every switch distance. Below 1 switches to lower-detail LODs sooner, above 1 later. */
	UPROPERTY(Config, EditAnywhere, Category = "LOD", meta = (ClampMin = "0.01"))
	float DistanceScale = 1.f;

	/**
	 * A mesh goes to a coarser LOD only once it's this much further than the switch distance,
	 * and back to a finer LOD only once it's this much closer.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "LOD", meta = (ClampMin = "0.0", ClampMax = "50.0", Units = "Percent"))
	float HysteresisPercent = 10.f;

	/** Use the screen sizes below for every mesh instead of each mesh's own LOD screen sizes. Meshes are capped at LOD3. */
	UPROPERTY(Config, EditAnywhere, Category = "LOD|Global Screen Sizes")
	bool bUseGlobalScreenSizes = false;

	UPROPERTY(Config, EditAnywhere, Category = "LOD|Global Screen Sizes", meta = (EditCondition = "bUseGlobalScreenSizes", ClampMin = "0.0", ClampMax = "2.0", DisplayName = "LOD1 Screen Size"))
	float GlobalScreenSizeLOD1 = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "LOD|Global Screen Sizes", meta = (EditCondition = "bUseGlobalScreenSizes", ClampMin = "0.0", ClampMax = "2.0", DisplayName = "LOD2 Screen Size"))
	float GlobalScreenSizeLOD2 = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category = "LOD|Global Screen Sizes", meta = (EditCondition = "bUseGlobalScreenSizes", ClampMin = "0.0", ClampMax = "2.0", DisplayName = "LOD3 Screen Size"))
	float GlobalScreenSizeLOD3 = 0.125f;

	/** LODs are re-evaluated only after the HMD has moved this far since the last pass. */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float MovementThreshold = 25.f;

	/** Each LOD change rebuilds the component's render state, so this caps the CPU cost per frame. Nearest meshes go first. */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "1"))
	int32 MaxLODChangesPerFrame = 32;

	/** Distance checks per frame. A pass over all meshes is spread over several frames when there are more. */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "1"))
	int32 MaxComponentsEvaluatedPerFrame = 4096;

	/** Number of LODs the global screen sizes cover (LOD0..LOD3). */
	static constexpr int32 NumGlobalLODs = 4;

	/** Global screen size threshold for LODIndex (1..3). LOD0 has no threshold. */
	float GetGlobalScreenSize(int32 LODIndex) const;
};
