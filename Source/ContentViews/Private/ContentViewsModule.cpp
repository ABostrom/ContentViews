#include "Modules/ModuleManager.h"

/** Registers view assets with the active backend. Stage one (legacy asset view) lands here first. */
class FContentViewsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FContentViewsModule, ContentViews)
