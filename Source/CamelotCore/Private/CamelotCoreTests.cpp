#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "DaocZoneProfile.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamelotCoreIdentityTest, "DAoC.Network.CamelotCoreIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCamelotCoreIdentityTest::RunTest(const FString &)
{
    TestTrue(TEXT("Renamed module loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("CamelotCore")));
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CamelotCore"));
    if (!TestTrue(TEXT("Renamed plugin discoverable"), Plugin.IsValid()))
        return false;

    FString ZoneCatalog;
    TestTrue(TEXT("Zone travel catalog available"), FFileHelper::LoadFileToString(ZoneCatalog, *(Plugin->GetBaseDir() / TEXT("Data/WorldZones.json"))));
    const auto Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(FString(TEXT("/Script/DaocClientLibUE5.DaocZoneProfile"))));
    TestEqual(TEXT("Existing asset class references redirect to renamed module"), Redirected.PackageName, FName(TEXT("/Script/CamelotCore")));
    TestEqual(TEXT("Reflected class belongs to renamed module"), UDaocZoneProfile::StaticClass()->GetOutermost()->GetFName(), FName(TEXT("/Script/CamelotCore")));
    return true;
}
#endif
