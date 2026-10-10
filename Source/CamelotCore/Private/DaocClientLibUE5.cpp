#include "DaocZoneToolbox.h"
#include "Modules/ModuleManager.h"

class FCamelotCoreModule : public IModuleInterface
{
  public:
    virtual void StartupModule() override
    {
        UE_LOG(LogTemp, Display,
               TEXT("DAoC plugin: zone toolbox and developer controls v2 loaded "
                    "(full rebuild required after adding reflected components)."));
#if WITH_EDITOR
        if (!IsRunningCommandlet())
            RegisterDaocZoneToolbox();
#endif
    }
    virtual void ShutdownModule() override
    {
#if WITH_EDITOR
        if (!IsRunningCommandlet())
            UnregisterDaocZoneToolbox();
#endif
    }
};
IMPLEMENT_MODULE(FCamelotCoreModule, CamelotCore)
