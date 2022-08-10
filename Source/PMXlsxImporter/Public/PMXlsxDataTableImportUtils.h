// Copyright Tianqi Li. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FPMXlsxImporterContextLogger;

/**
 * 
 */
class PMXLSXIMPORTER_API FPMXlsxDataTableImportUtils
{
public:
	static void ImportDataTableFromXlsx(UDataTable* DataTable, const FString JsonString, FPMXlsxImporterContextLogger& InOutErrors);

	static bool WasDataTableModified(UDataTable* Updated, UDataTable* Original);
};
