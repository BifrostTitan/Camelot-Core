using UnrealBuildTool;
using System.IO;

/// <summary>Declares shared runtime dependencies and editor-only import dependencies for Camelot Core.</summary>
public class CamelotCore : ModuleRules
{
    /// <summary>Configures the module for the requested runtime or editor target.</summary>
    /// <param name="Target">Unreal build settings used to select platform and editor dependencies.</param>
    public CamelotCore(ReadOnlyTargetRules Target) : base(Target)
    {
        // Require the pinned source dependencies, including when the plugin is copied to a host.
        string dependencyRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty"));
        RequireDependency(dependencyRoot, "DAoC-Client-Lib", "include/daoc_client_lib/nif_asset.h");
        RequireDependency(dependencyRoot, "daoc-niflib", "include/niflib/ni_file.h");
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "ProceduralMeshComponent",
            "Niagara",
            "Slate",
            "SlateCore"
        });
        PrivateDependencyModuleNames.Add("ImageWrapper");
        PrivateDependencyModuleNames.Add("Json");
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "Sockets", "Networking", "ApplicationCore" });
        RuntimeDependencies.Add("$(PluginDir)/Data/WorldZones.json");
        PrivateDefinitions.Add("DAOC_PCX_UNREAL=1");
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
            PrivateDependencyModuleNames.AddRange(new[] { "UMG", "RenderCore" });
            PrivateDependencyModuleNames.AddRange(new[] { "ToolMenus", "PropertyEditor" });
            PrivateDependencyModuleNames.Add("AssetRegistry");
            PrivateDependencyModuleNames.Add("AssetTools");
            PrivateDependencyModuleNames.Add("NiagaraEditor");
            PrivateDependencyModuleNames.AddRange(new[] { "MeshDescription", "StaticMeshDescription" });
            PrivateDependencyModuleNames.AddRange(new[] { "SkeletalMeshDescription", "AnimationCore" });
        }
    }

    /// <summary>Rejects incomplete plugin installations before compilation begins.</summary>
    private void RequireDependency(string Root, string Name, string Header)
    {
        foreach (string File in new[] { "CMakeLists.txt", Header })
        {
            string DependencyFile = Path.Combine(Root, Name, File);
            if (!System.IO.File.Exists(DependencyFile))
                throw new BuildException("Camelot Core requires ThirdParty/{0}. Run 'git submodule update --init --recursive' in the source checkout, and include ThirdParty when installing the plugin. Missing: {1}", Name, DependencyFile);
            ExternalDependencies.Add(DependencyFile);
        }
    }
}
