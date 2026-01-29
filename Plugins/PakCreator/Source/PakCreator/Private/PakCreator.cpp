// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#include "PakCreator.h"
#include "PakCreatorStyle.h"
#include "PakCreatorCommands.h"
#include "PakCreatorWindow.h"
#include "PakHelperFunctions.h"
#include "AutomatedPakParams.h"
#include "UATProcess.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY(LogPakCreator);

FPakCreatorModule* FPakCreatorModule::Singleton = nullptr;

static const FName PakCreatorTabName("PakCreator");

#define LOCTEXT_NAMESPACE "FPakCreatorModule"

const TArray<FString> FAutomatedPakParams::ValidPlatformNames = {
	"Win64",
	"Android_ASTC",
	"Android_DXT",
	"Android_ETC2",
	"IOS",
	"Mac",
	"Linux"
};

const FString FAutomatedPakParams::ReleaseVersionName = "BaseGameRelease";

void FPakCreatorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FPakCreatorStyle::Initialize();
	FPakCreatorStyle::ReloadTextures();

	FPakCreatorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FPakCreatorCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FPakCreatorModule::PluginButtonClicked),
		FCanExecuteAction());
		
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FPakCreatorModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}
	
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FPakCreatorModule::AddToolbarExtension));
		
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PakCreatorTabName, FOnSpawnTab::CreateRaw(this, &FPakCreatorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FPakCreatorTabTitle", "Pak Creator Plugin"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	Singleton = this;
}

void FPakCreatorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FPakCreatorStyle::Shutdown();

	FPakCreatorCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PakCreatorTabName);

	Singleton = nullptr;
}

bool FPakCreatorModule::IsAvailable()
{
	return Singleton != nullptr;
}

FPakCreatorModule& FPakCreatorModule::Get()
{
	if (Singleton == nullptr)
	{
		check(IsInGameThread());
		FModuleManager::LoadModuleChecked<FPakCreatorModule>("PakCreator");
	}
	check(Singleton);
	return *Singleton;
}

bool FPakCreatorModule::CreatePakProcess(const FAutomatedPakParams& Params)
{
	if (!Params.IsValid())
	{
		UE_LOG(LogPakCreator, Error, TEXT("CreatePakProcess: Params contain invalid data."));
		return false;
	}

	SavedPakParams = Params;

	const FString ConfigurationName = "Shipping";
	FString PlatformName = FAutomatedPakParams::ValidPlatformNames[0];
	FString Cookflavor = "";

	FPakHelperFunctions::GetPlatformNameAndFlavorBySelection(*Params.PlatformName, PlatformName, Cookflavor);

	const FString BaseGameCommand =
		FPakHelperFunctions::MakeUATCommand(
			Params.UProjectPath,
			PlatformName,
			Cookflavor,
			ConfigurationName,
			Params.BuildTarget,
			FPaths::Combine(Params.GetTemporaryStagingDirectory(), FAutomatedPakParams::ReleaseVersionName))
		+ FPakHelperFunctions::MakeUATParams_BaseGame(Params.OutputPath, FAutomatedPakParams::ReleaseVersionName);

#if ENGINE_MAJOR_VERSION == 5
	PendingUATCommands.Enqueue({ FAutomatedPakParams::ReleaseVersionName, BaseGameCommand });
#else
	PendingUATCommands.Enqueue(TPair<FString, FString>(FAutomatedPakParams::ReleaseVersionName, BaseGameCommand));
#endif

	for (const FString& PluginName : Params.PluginNames)
	{
		const FString DLCCommand =
			FPakHelperFunctions::MakeUATCommand(
				Params.UProjectPath,
				PlatformName,
				Cookflavor,
				ConfigurationName,
				Params.BuildTarget,
				FPaths::Combine(Params.GetTemporaryStagingDirectory(), PluginName))
			+ FPakHelperFunctions::MakeUATParams_DLC(PluginName, FAutomatedPakParams::ReleaseVersionName);

#if ENGINE_MAJOR_VERSION == 5
		PendingUATCommands.Enqueue({ PluginName, DLCCommand });
#else
		PendingUATCommands.Enqueue(TPair<FString, FString>(PluginName, DLCCommand));
#endif
	}

	return RunBuild();
}

bool FPakCreatorModule::IsPakProcessRunning() const
{
	return Runnable.IsValid();
}

bool FPakCreatorModule::StopPakProcess()
{
	PendingUATCommands.Empty();

	if (Runnable.IsValid())
	{
		// Cancel thread
		Runnable->Cancel();

		// Wait for join
		Runnable.Reset();

		return true;
	}

	return false;
}

bool FPakCreatorModule::RunUATBuildProcess(const FString& CommandLine)
{
	checkf(Runnable.IsValid() == false, TEXT("A process is already running."));

	FString ExecutablePath, Executable;
	FUATProcess::GetUATExecutable(ExecutablePath, Executable);

	Runnable = MakeShared<FUATProcess>();
	Runnable->OnTerminated().BindRaw(this, &FPakCreatorModule::OnPakProcessComplete);

	return Runnable->Launch(ExecutablePath / Executable, ExecutablePath, CommandLine);
}

bool FPakCreatorModule::RunBuild()
{
	checkf(IsInGameThread(), TEXT("RunBuild called from non game thread."));

#if ENGINE_MAJOR_VERSION == 5
	decltype(PendingUATCommands)::FElementType BuildItem;
#else
	TPair<FString, FString> BuildItem;
#endif

	if (PendingUATCommands.Dequeue(BuildItem))
	{
		CurrentTaskName = BuildItem.Key;

		return RunUATBuildProcess(BuildItem.Value);
	}

	return false;
}

void FPakCreatorModule::OnPakProcessComplete(int32 ErrorCode)
{
	if (!IsInGameThread())
	{
		// Retry on the GameThread.
		AsyncTask(ENamedThreads::GameThread, [ErrorCode]()
		{
			if (FPakCreatorModule::IsAvailable())
			{
				FPakCreatorModule::Get().OnPakProcessComplete(ErrorCode);
			}
		});
		return;
	}

	Runnable.Reset();

	if (ErrorCode != 0)
	{
		PendingUATCommands.Empty();

		UE_LOG(LogPakCreator, Warning, TEXT("UAT BuildCookRun exited with error code = %i. See the Output Log for more information. (Window -> Developer Tools -> Output Log)"), ErrorCode);
		return;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Check if the current task is a pak or just the base game.
	if (CurrentTaskName != FAutomatedPakParams::ReleaseVersionName)
	{
		// Its a pak, let's move the new pak to our desired location.

		const FString CurrentStagingDirectory = SavedPakParams.GetTemporaryStagingDirectory() / CurrentTaskName;

		TArray<FString> FoundFiles;
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".pak"));
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".ucas"));
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".utoc"));
		PlatformFile.FindFilesRecursively(FoundFiles, *CurrentStagingDirectory, TEXT(".sig"));

		if (FoundFiles.Num() > 0)
		{
			for (const FString& File : FoundFiles)
			{
				const FString CopyTo = SavedPakParams.OutputPath / FPaths::GetCleanFilename(File);

				// Delete existing pak at target location
				if (PlatformFile.FileExists(*CopyTo))
				{
					PlatformFile.DeleteFile(*CopyTo);
				}

				if (PlatformFile.MoveFile(*CopyTo, *File))
				{
					UE_LOG(LogPakCreator, Log, TEXT("Moving %s to %s"), *FPaths::GetCleanFilename(File), *SavedPakParams.OutputPath);
				}
				else
				{
					UE_LOG(LogPakCreator, Error, TEXT("Failed to move file from %s to %s"), *File, *CopyTo);
				}
			}
		}
		else
		{
			UE_LOG(LogPakCreator, Error, TEXT("Error: Pak file not found in directory %s"), *CurrentStagingDirectory);
		}
	}

	if (!RunBuild())
	{
		// If no more builds have been launched do some cleanup

		// Remove temporary staging directory
		if (!PlatformFile.DeleteDirectoryRecursively(*SavedPakParams.GetTemporaryStagingDirectory()))
		{
			UE_LOG(LogPakCreator, Warning, TEXT("Failed to delete temporary build directory %s"), *SavedPakParams.GetTemporaryStagingDirectory());
		}
	}
}

TSharedRef<SDockTab> FPakCreatorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	CreatorWindow = MakeShareable(new FPakCreatorWindow());
	return CreatorWindow->OnSpawnPluginTab(SpawnTabArgs);
}

void FPakCreatorModule::PluginButtonClicked()
{
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION == 27)
	FGlobalTabmanager::Get()->TryInvokeTab(PakCreatorTabName);
#else
	FGlobalTabmanager::Get()->InvokeTab(PakCreatorTabName);
#endif
}

void FPakCreatorModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FPakCreatorCommands::Get().OpenPluginWindow);
}

void FPakCreatorModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FPakCreatorCommands::Get().OpenPluginWindow);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPakCreatorModule, PakCreator)
