// Copyright Tianqi Li. All Rights Reserved.


#include "PMXlsxDataTableImportUtils.h"

#include "EditorAssetLibrary.h"
#include "PMXlsxImporterContextLogger.h"
#include "PMXlsxImporterSettings.h"
#include "Engine/Private/DataTableJSON.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "PMXlsxImporterLog.h"


void FPMXlsxDataTableImportUtils::ImportDataTableFromXlsx(UDataTable* DataTable, const FString JsonString,
                                                          FPMXlsxImporterContextLogger& InOutErrors)
{
	// Keep a copy around to see if anything actually gets changed.
	// It would be more accurate to pull Original from what's currently checked into source control,
	// but that would be very slow.
	UDataTable* DuplicatedDataTable = DuplicateObject(DataTable, nullptr, DataTable->GetFName());
	// Array used to store problems about table creation
	TArray<FString> OutProblems = DuplicatedDataTable->CreateTableFromJSONString(JsonString);

	for (FString Problem : OutProblems)
	{
		InOutErrors.Logf(TEXT("%s"), *Problem);
	}

	// Telling Unreal to save a file guarantees the file becomes modified even if there aren't meaningful changes to
	// that file's data. We only want to check out and save modified assets.
	if (OutProblems.IsEmpty() && WasDataTableModified(DuplicatedDataTable, DataTable))
	{
		// Do the actual import here
		UDataTableFunctionLibrary::FillDataTableFromJSONString(DataTable, JsonString);
		
		const UPMXlsxImporterSettings* SettingsCDO = GetDefault<UPMXlsxImporterSettings>();
		if (SettingsCDO->bCheckoutGeneratedAssets && !UEditorAssetLibrary::CheckoutLoadedAsset(DataTable))
		{
			// CheckoutLoadedAsset will print its own errors, but we want to add one here so that we can
			// properly track if the run as a whole succeeded or not.
			InOutErrors.Logf(TEXT("Unable to checkout asset %s"), *DataTable->GetName());
			return;
		}
		// No reason to mark the package as dirty. We know we need to save right now.
		if (!UEditorAssetLibrary::SaveLoadedAsset(DataTable, /*bOnlyIfIsDirty:*/ false))
		{
			// SaveLoadedAsset will print its own errors, but we want to add one here so that we can
			// properly track if the run as a whole succeeded or not.
			InOutErrors.Logf(TEXT("Unable to save asset %s"), *DataTable->GetName());
			return;
		}
	}
}

bool FPMXlsxDataTableImportUtils::WasDataTableModified(UDataTable* Updated, UDataTable* Original)
{
	// Export this and Original as text, then compare the text

	const FString OriginalArchive = Original->GetTableAsString();

	const FString UpdatedArchive = Updated->GetTableAsString();

	const bool bWasModified = OriginalArchive != UpdatedArchive;
	UE_LOG(LogPMXlsxImporter, Verbose, TEXT("%s %s modified"), *Updated->GetName(), bWasModified ? TEXT("WAS") : TEXT("was NOT"));

	if (bWasModified)
	{
		UE_LOG(LogPMXlsxImporter, VeryVerbose, TEXT("Orignal: %s"), *OriginalArchive);
		UE_LOG(LogPMXlsxImporter, VeryVerbose, TEXT("Updated: %s"), *UpdatedArchive);
	}
	else
	{
		UE_LOG(LogPMXlsxImporter, VeryVerbose, TEXT("%s"), *OriginalArchive);
	}

	return bWasModified;
}

