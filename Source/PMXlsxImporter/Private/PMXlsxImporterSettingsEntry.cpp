// Copyright 2022 Proletariat, Inc.

#include "PMXlsxImporterSettingsEntry.h"

#include "PMXlsxDataAssetImporterJSON.h"
#include "PMXlsxDataAsset.h"
#include "PMXlsxImporterLog.h"
#include "EditorAssetLibrary.h"
#include "Engine/AssetManager.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"
#include "PMXlsxDataTableImportUtils.h"
#include "PMXlsxImporterPythonReflection.h"
#include "PMXlsxImporterSettings.h"
#include "Engine/Private/DataTableJSON.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define PM_ENABLE_SOURCE_CONTROL 0

void FPMXlsxImporterSettingsEntry::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty->GetNameCPP() == TEXT("WorksheetName"))
	{
		// Double check that the user picked a valid worksheet name. It's possible they didn't. See
		// the comment in UPMXlsxImporterSettings::GetWorksheetNames() 
		const TArray<FString> WorksheetNames = GetWorksheetNames();
		if (!WorksheetNames.Contains(WorksheetName))
		{
			WorksheetName = FString();
		}
	}

	if (PropertyChangedEvent.MemberProperty->GetNameCPP() == TEXT("XlsxFile") &&
		!GetXlsxAbsolutePath().IsEmpty())
	{
		const TArray<FString> WorksheetNames = GetWorksheetNames();
		WorksheetName = WorksheetNames.Num() == 0 ? TEXT("") : WorksheetNames[0];
	}

	if (PropertyChangedEvent.MemberProperty->GetNameCPP() != TEXT("OutputDir") &&
		!GetXlsxAbsolutePath().IsEmpty() &&
		!WorksheetName.IsEmpty())
	{
		const FString XlsxFilename = FPaths::GetBaseFilename(XlsxFile.FilePath);
		if (ImportType == EPMXlsxImportType::DataAsset && DataAssetType.IsValid())
		{
			OutputDir.Path = FString::Printf(TEXT("Generated/%s/%s/%s"), *DataAssetType.ToString(), *XlsxFilename, *WorksheetName);
		}
		else if (ImportType == EPMXlsxImportType::DataTable && !DataTableRowType.IsNull())
		{
			OutputDir.Path = TEXT("Generated/DataTables");
		}
	}
}

TArray<FString> FPMXlsxImporterSettingsEntry::GetWorksheetNames() const
{
	const FString& XlsxAbsolutePath = GetXlsxAbsolutePath();
	if (XlsxAbsolutePath.IsEmpty())
	{
		UE_LOG(LogPMXlsxImporter, Warning, TEXT("Could not get worksheet names: xlsx file not set"));
		return TArray<FString>();
	}

	UPMXlsxImporterPythonBridge* PythonBridge = UPMXlsxImporterPythonBridge::Get();
	return PythonBridge ? PythonBridge->ReadWorksheetNames(XlsxAbsolutePath) : TArray<FString>();
}

void FPMXlsxImporterSettingsEntry::SyncAssets(FPMXlsxImporterContextLogger& InOutErrors, int32 MaxErrors) const
{
	auto ScopedErrorContext = InOutErrors.PushContext(FString::Printf(TEXT("%s:%s"), *XlsxFile.FilePath, *WorksheetName));

	if ((ImportType == EPMXlsxImportType::DataAsset && !DataAssetType.IsValid()) ||
		(ImportType == EPMXlsxImportType::DataTable && DataTableRowType.IsNull()))
	{
		InOutErrors.Log(TEXT("Could not sync assets: invalid data asset type"));
		return;
	}

	const FString& XlsxAbsolutePath = GetXlsxAbsolutePath();
	if (XlsxAbsolutePath.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not sync assets: xlsx file not set"));
		return;
	}

	if (WorksheetName.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not sync assets: no worksheet name set"));
		return;
	}
	
	if (OutputDir.Path.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not sync assets: no output dir set"));
		return;
	}
	
	if (ImportType == EPMXlsxImportType::DataAsset)
	{
		UAssetManager& AssetManager = UAssetManager::Get();
		FPrimaryAssetTypeInfo TypeInfo;
		if (!AssetManager.GetPrimaryAssetTypeInfo(DataAssetType, TypeInfo))
		{
			InOutErrors.Logf(TEXT("Could not sync assets: could not get type info for %s"), *DataAssetType.ToString());
			return;
		}
		UClass* Class = TypeInfo.AssetBaseClassLoaded;

		IFileManager& FileManager = IFileManager::Get();
	
		UPMXlsxImporterPythonBridge* PythonBridge = UPMXlsxImporterPythonBridge::Get();
		if (PythonBridge == nullptr)
		{
			return; // UPMXlsxImporterPythonBridge::Get() logs an error when it returns null
		}

		const UPMXlsxImporterSettings* ImporterSettings = GetDefault<UPMXlsxImporterSettings>();
		check(ImporterSettings);

		FPMXlsxImporterPythonBridgeAssetNames AssetNames = PythonBridge->ReadWorksheetAssetNames(XlsxAbsolutePath, WorksheetName,
			ImporterSettings->XlsxHeaderRow, ImporterSettings->XlsxDataStartRow);
		if (!AssetNames.Error.IsEmpty())
		{
			InOutErrors.Logf(TEXT("Could not sync assets: could not read asset names from worksheet:\n%s"), *AssetNames.Error);
			return;
		}

		for (const FString& AssetName : AssetNames.AssetNames)
		{
			const FString AssetPath = GetProjectRootOutputPath(AssetName);
			// Note that DoesAssetExist is case-insensitive.
			// This is good - perforce will have issues if you change the case of a file.
			if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
			{
				// https://isaratech.com/save-a-procedurally-generated-texture-as-a-new-asset/
				UPackage* Package = CreatePackage(*AssetPath);
				Package->FullyLoad();
				UPMXlsxDataAsset* Asset = NewObject<UPMXlsxDataAsset>(Package, Class, FName(AssetName), RF_Public | RF_Standalone | RF_MarkAsRootSet);
				Package->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(Asset);
				const FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());

#if ENGINE_MAJOR_VERSION == 4
				if (!UPackage::SavePackage(Package, Asset, EObjectFlags::RF_NoFlags, *PackageFileName))
#elif ENGINE_MAJOR_VERSION == 5
				FSavePackageArgs SaveArgs;
				if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
#else
# error Unknown engine version
#endif
				{
					InOutErrors.Logf(TEXT("Unable to save file %s"), *PackageFileName);
					if (InOutErrors.Num() < MaxErrors)
					{
						continue;
					}
					else
					{
						return;
					}
				}
				UE_LOG(LogPMXlsxImporter, Log, TEXT("Created new asset %s"), *AssetPath);
#if PM_ENABLE_SOURCE_CONTROL
				const FString AssetAbsolutePath = FileManager.ConvertToAbsolutePathForExternalAppForWrite(*PackageFileName);
				USourceControlHelpers::MarkFileForAdd(AssetAbsolutePath);
#endif
			}
		}
#if PM_ENABLE_SOURCE_CONTROL
		TArray<FString> ExistingAssets = UEditorAssetLibrary::ListAssets(GetProjectRootOutputDir(), /*bRecursive:*/ false, /*bIncludeFolder:*/ false);
		for (const FString& ExistingAssetPath : ExistingAssets)
		{
			// ExistingAssetPath = (e.g.) "/Game/Generated/TestData/test/Sheet1/TestDataFromXLS1.TestDataFromXLS1"

			if (!ShouldAssetExist(ExistingAssetPath, ParsedWorksheet))
			{
				// Convert ExistingAssetPath an absolute file path for USourceControlHelpers. There must be a better way to do this.
				// USourceControlHelpers does try to do this conversion, but it doesn't always work.
				// PackageName = "/Game/Generated/TestData/test/Sheet1/TestDataFromXLS1"
				const FString PackageName = FEditorFileUtils::ExtractPackageName(ExistingAssetPath);
				// RelativePath = "../../../PluginDev/Content/Generated/TestData/test/Sheet1/TestDataFromXLS1.uasset"
				const FString RelativePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
				// AbsolutePath = "C:/dev/plugindev-main/PluginDev/Content/Generated/TestData/test/Sheet1/TestDataFromXLS1.uasset"
				const FString AbsolutePath = FileManager.ConvertToAbsolutePathForExternalAppForWrite(*RelativePath);

				FSourceControlState State = USourceControlHelpers::QueryFileState(AbsolutePath);
				if (!State.bIsValid)
				{
					InOutErrors.Logf(TEXT("Source control state is invalid for %s. Refusing to delete this file."), *AbsolutePath);
					if (InOutErrors.Num() < MaxErrors)
					{
						continue;
					}
					else
					{
						return;
					}
				}

				// Internally marks the file for delete in source control and logs what it's doing
				if (!UEditorAssetLibrary::DeleteAsset(ExistingAssetPath))
				{
					InOutErrors.Logf(TEXT("Unable to delete asset %s"), *ExistingAssetPath);
					if (InOutErrors.Num() >= MaxErrors)
					{
						return;
					}
				}
			}
		}
#endif

		// Force the AssetManager to rescan now so that it's up to date when we try to validate FPrimaryAssetIds in ParseData().
		TArray<FString> PathToScan;
		PathToScan.Add(GetProjectRootOutputDir());
		AssetManager.ScanPathsSynchronous(PathToScan);
	}
	else if (ImportType == EPMXlsxImportType::DataTable)
	{
		UObject* Obj = DataTableRowType.Get();
		if (Obj == nullptr)
		{
			Obj = DataTableRowType.LoadSynchronous();
		}

		if (Obj != nullptr)
		{
			UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Obj);
			if (ScriptStruct != nullptr)
			{
				IFileManager& FileManager = IFileManager::Get();

				const FString AssetPath = GetProjectRootOutputPath(GetDataTableName());
				// Note that DoesAssetExist is case-insensitive.
				// This is good - perforce will have issues if you change the case of a file.
				if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
				{
					// https://isaratech.com/save-a-procedurally-generated-texture-as-a-new-asset/
					UPackage* Package = CreatePackage(*AssetPath);
					Package->FullyLoad();
					UDataTable* DataTable = NewObject<UDataTable>(Package, UDataTable::StaticClass(), FName(GetDataTableName()), RF_Public | RF_Standalone | RF_MarkAsRootSet);
					DataTable->RowStruct = ScriptStruct;
					Package->MarkPackageDirty();
					FAssetRegistryModule::AssetCreated(DataTable);
					const FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());

#if ENGINE_MAJOR_VERSION == 4
					if (!UPackage::SavePackage(Package, Asset, EObjectFlags::RF_NoFlags, *PackageFileName))
#elif ENGINE_MAJOR_VERSION == 5
					FSavePackageArgs SaveArgs;
					if (!UPackage::SavePackage(Package, DataTable, *PackageFileName, SaveArgs))
#else
# error Unknown engine version
#endif
					{
						InOutErrors.Logf(TEXT("Unable to save file %s"), *PackageFileName);
						return;
					}
					UE_LOG(LogPMXlsxImporter, Log, TEXT("Created new asset %s"), *AssetPath);
#if PM_ENABLE_SOURCE_CONTROL
					const FString AssetAbsolutePath = FileManager.ConvertToAbsolutePathForExternalAppForWrite(*PackageFileName);
					USourceControlHelpers::MarkFileForAdd(AssetAbsolutePath);
#endif
				}
			}
		}
	}
}

void FPMXlsxImporterSettingsEntry::ParseData(FPMXlsxImporterContextLogger& InOutErrors, int32 MaxErrors) const
{
	auto ScopedErrorContext = InOutErrors.PushContext(FString::Printf(TEXT("%s:%s"), *XlsxFile.FilePath, *WorksheetName));

	if ((ImportType == EPMXlsxImportType::DataAsset && !DataAssetType.IsValid()) ||
		(ImportType == EPMXlsxImportType::DataTable && DataTableRowType.IsNull()))
	{
		InOutErrors.Log(TEXT("Could not parse data: invalid data asset type"));
		return;
	}

	const FString& XlsxAbsolutePath = GetXlsxAbsolutePath();
	if (XlsxAbsolutePath.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not parse data: xlsx file not set"));
		return;
	}

	if (WorksheetName.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not parse data: no worksheet name set"));
		return;
	}

	if (OutputDir.Path.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not parse data: no output dir set"));
		return;
	}

	UPMXlsxImporterPythonBridge* PythonBridge = UPMXlsxImporterPythonBridge::Get();
	if (PythonBridge == nullptr)
	{
		return; // UPMXlsxImporterPythonBridge::Get() logs an error when it returns null
	}

	const UStruct* Struct = GetReflectionStruct(InOutErrors);
	if (Struct == nullptr)
	{
		if (ImportType == EPMXlsxImportType::DataAsset)
		{
			InOutErrors.Log(TEXT("Could not parse data: DataAssetType is not valid"));
		}
		else
		{
			InOutErrors.Log(TEXT("Could not parse data: DataTableRowType is not valid"));
		}
		return;
	}
	
	FPMXlsxWorksheetTypeInfo WorksheetTypeInfo;
	WorksheetTypeInfo.ReadStruct(Struct);

	const UPMXlsxImporterSettings* ImporterSettings = GetDefault<UPMXlsxImporterSettings>();
	check(ImporterSettings);

	const FPMXlsxImporterPythonBridgeJsonString JSONData = PythonBridge->ReadWorksheetAsJson(XlsxAbsolutePath, WorksheetName,
		ImporterSettings->XlsxHeaderRow, ImporterSettings->XlsxDataStartRow, WorksheetTypeInfo);
	if (!JSONData.Error.IsEmpty())
	{
		InOutErrors.Logf(TEXT("Could not parse data: could not read worksheet:\n%s"), *JSONData.Error);
		return;
	}
	if (JSONData.JsonString.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not parse data: json data is empty."));
		return;
	}

	TArray< TSharedPtr<FJsonValue> > ParsedTableRows;
	{
		const TSharedRef< TJsonReader<TCHAR> > JsonReader = TJsonReaderFactory<TCHAR>::Create(JSONData.JsonString);
		if (!FJsonSerializer::Deserialize(JsonReader, ParsedTableRows) || ParsedTableRows.Num() == 0)
		{
			InOutErrors.Log(FString::Printf(TEXT("Failed to parse the JSON data. Error: %s"), *JsonReader->GetErrorMessage()));
			return;
		}
	}
	
	if (ImportType == EPMXlsxImportType::DataAsset)
	{
		// Iterate over rows
		for (int32 RowIdx = 0; RowIdx < ParsedTableRows.Num(); ++RowIdx)
		{
			const TSharedPtr<FJsonValue>& ParsedTableRowValue = ParsedTableRows[RowIdx];
			TSharedPtr<FJsonObject> ParsedTableRowObject = ParsedTableRowValue->AsObject();
			if (!ParsedTableRowObject.IsValid())
			{
				InOutErrors.Log(FString::Printf(TEXT("Row '%d' is not a valid JSON object."), RowIdx));
				continue;
			}
			
			const FName AssetName = DataTableUtils::MakeValidName(ParsedTableRowObject->GetStringField(TEXT("Name")));
			
			FString AssetPath = GetProjectRootOutputPath(AssetName.ToString());
			UPMXlsxDataAsset* Asset = Cast<UPMXlsxDataAsset>(UEditorAssetLibrary::LoadAsset(AssetPath));
			if (Asset == nullptr)
			{
				InOutErrors.Logf(TEXT("Asset %s is not a UPMXlsxDataAsset"), *AssetPath);
				continue;
			}

			Asset->ImportFromXLSX(ParsedTableRowObject.ToSharedRef(), InOutErrors);

			if (InOutErrors.Num() >= MaxErrors)
			{
				return;
			}
		}
	}
	else
	{
		const FString AssetPath = GetProjectRootOutputPath(GetDataTableName());
		UDataTable* DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (DataTable == nullptr)
		{
			InOutErrors.Logf(TEXT("Asset %s is not a UDataTable"), *AssetPath);
			return;
		}

		FPMXlsxDataTableImportUtils::ImportDataTableFromXlsx(DataTable, JSONData.JsonString, InOutErrors);
	}
}

void FPMXlsxImporterSettingsEntry::Validate(FPMXlsxImporterContextLogger& InOutErrors, int32 MaxErrors) const
{
	auto ScopedErrorContext = InOutErrors.PushContext(FString::Printf(TEXT("%s:%s"), *XlsxFile.FilePath, *WorksheetName));

	{
		// TODO: @litianqi implement validate
		return;
	}
	
	if (ImportType == EPMXlsxImportType::DataAsset && !DataAssetType.IsValid())
	{
		InOutErrors.Log(TEXT("Could not validate assets: invalid data asset type"));
		return;
	}

	const FString& XlsxAbsolutePath = GetXlsxAbsolutePath();
	if (XlsxAbsolutePath.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not validate asset data: xlsx file not set"));
		return;
	}

	if (WorksheetName.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not validate asset data: no worksheet name set"));
		return;
	}

	if (OutputDir.Path.IsEmpty())
	{
		InOutErrors.Log(TEXT("Could not validate asset data: no output dir set"));
		return;
	}

	UPMXlsxImporterPythonBridge* PythonBridge = UPMXlsxImporterPythonBridge::Get();
	if (PythonBridge == nullptr)
	{
		return; // UPMXlsxImporterPythonBridge::Get() logs an error when it returns null
	}

	const UPMXlsxImporterSettings* ImporterSettings = GetDefault<UPMXlsxImporterSettings>();
	check(ImporterSettings);

	FPMXlsxImporterPythonBridgeAssetNames AssetNames = PythonBridge->ReadWorksheetAssetNames(XlsxAbsolutePath, WorksheetName,
		ImporterSettings->XlsxHeaderRow, ImporterSettings->XlsxDataStartRow);
	if (!AssetNames.Error.IsEmpty())
	{
		InOutErrors.Logf(TEXT("Could not validate assets: could not read asset names from worksheet:\n%s"), *AssetNames.Error);
		return;
	}

	UPMXlsxDataAsset* PreviousAsset = nullptr;
	for (const FString& AssetName : AssetNames.AssetNames)
	{
		FString AssetPath = GetProjectRootOutputPath(AssetName);
		UPMXlsxDataAsset* Asset = Cast<UPMXlsxDataAsset>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (Asset == nullptr)
		{
			InOutErrors.Logf(TEXT("Asset %s is not a UPMXlsxDataAsset"), *AssetPath);
			continue;
		}

		Asset->Validate(PreviousAsset, InOutErrors);
		if (InOutErrors.Num() >= MaxErrors)
		{
			return;
		}

		PreviousAsset = Asset;
	}
}

FSourceControlState FPMXlsxImporterSettingsEntry::GetXlsxFileSourceControlState(bool bSilent /* = false*/) const
{
	return USourceControlHelpers::QueryFileState(GetXlsxAbsolutePath(), bSilent);
}

FString FPMXlsxImporterSettingsEntry::GetXlsxAbsolutePath() const
{
	return XlsxFile.FilePath.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), XlsxFile.FilePath);
}

FString FPMXlsxImporterSettingsEntry::GetProjectRootOutputDir() const
{
	return FString::Printf(TEXT("/Game/%s"), *OutputDir.Path);
}

FString FPMXlsxImporterSettingsEntry::GetProjectRootOutputPath(const FString& AssetName) const
{
	return FString::Printf(TEXT("%s/%s"), *GetProjectRootOutputDir(), *AssetName).Replace(TEXT("//"), TEXT("/"));
}

UStruct* FPMXlsxImporterSettingsEntry::GetReflectionStruct(FPMXlsxImporterContextLogger& InOutErrors) const
{
	if (ImportType == EPMXlsxImportType::DataAsset)
	{
		const UAssetManager& AssetManager = UAssetManager::Get();
		FPrimaryAssetTypeInfo TypeInfo;
		if (!AssetManager.GetPrimaryAssetTypeInfo(DataAssetType, TypeInfo))
		{
			InOutErrors.Logf(TEXT("Could not sync assets: could not get type info for %s"), *DataAssetType.ToString());
			return nullptr;
		}
		return TypeInfo.AssetBaseClassLoaded;
	}
	else
	{
		UObject* Obj = DataTableRowType.Get();
		if (Obj == nullptr)
		{
			Obj = DataTableRowType.LoadSynchronous();
		}

		if (Obj != nullptr)
		{
			return Cast<UScriptStruct>(Obj);
		}
	}
	return nullptr;
}

bool FPMXlsxImporterSettingsEntry::ShouldAssetExist(const FString& AssetPath, const TArray<FString>& AssetNames) const
{
	for (const FString& AssetName : AssetNames)
	{
		if (AssetPath.EndsWith(FString::Printf(TEXT(".%s"), *AssetName)))
		{
			return true;
		}
	}
	return false;
}

FString FPMXlsxImporterSettingsEntry::GetDataTableName() const
{
	const UPMXlsxImporterSettings* SettingsCDO = GetDefault<UPMXlsxImporterSettings>();
	return SettingsCDO->DataTableAssetPrefix + WorksheetName;
}
