// Copyright (c) 2026 groatse. Licensed under the MIT License.

#pragma once

#include "Modules/ModuleManager.h"

class FDistanceLODderEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
