// Copyright (C) 2023 Blue Mountains GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AutomatedPakParams.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPakCreator, Log, All);

class FToolBarBuilder;
class FMenuBuilder;

class FPakCreatorModule : public IModuleInterface
{
public:
	// IModuleInterface

	/**
	 * Called when voice module is loaded
	 * Initialize platform specific parts of template handling
	 */
	virtual void StartupModule() override;

	/**
	 * Called when voice module is unloaded
	 * Shutdown platform specific parts of template handling
	 */
	virtual void ShutdownModule() override;

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	PAKCREATOR_API static bool IsAvailable();

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	PAKCREATOR_API static FPakCreatorModule& Get();

	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

private:

	void AddToolbarExtension(FToolBarBuilder& Builder);
	void AddMenuExtension(FMenuBuilder& Builder);

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedPtr<class FPakCreatorWindow> CreatorWindow;
	
	/** Singleton Instance */
	static FPakCreatorModule* Singleton;

public:

	PAKCREATOR_API bool CreatePakProcess(const FAutomatedPakParams& Params);
	PAKCREATOR_API bool IsPakProcessRunning() const;
	PAKCREATOR_API bool StopPakProcess();

private:
	// Command queue for building paks with UAT. Pair key: name of the plugin(DLC), value: command line string, TQueue is thread safe.
	TQueue<TPair<FString, FString>> PendingUATCommands;
	
	FString CurrentTaskName;
	FAutomatedPakParams SavedPakParams;

	TSharedPtr<class FUATProcess> Runnable;

	bool RunUATBuildProcess(const FString& CommandLine);
	bool RunBuild();
	void OnPakProcessComplete(int32 ErrorCode);
};
