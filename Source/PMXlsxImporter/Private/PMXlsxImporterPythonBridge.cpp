// Copyright 2022 Proletariat, Inc.

#include "PMXlsxImporterPythonBridge.h"

#include "PMXlsxImporterContextLogger.h"
#include "PMXlsxImporterLog.h"

UPMXlsxImporterPythonBridge* UPMXlsxImporterPythonBridge::Get(FPMXlsxImporterContextLogger* InOutErrors)
{
    TArray<UClass*> PythonBridgeClasses;
    GetDerivedClasses(UPMXlsxImporterPythonBridge::StaticClass(), PythonBridgeClasses);
    int32 NumClasses = PythonBridgeClasses.Num();
    if (NumClasses > 0)
    {
        return Cast<UPMXlsxImporterPythonBridge>(PythonBridgeClasses[NumClasses - 1]->GetDefaultObject());
    }

	if (InOutErrors)
	{
		InOutErrors->Log(TEXT("No python bridge implementation found. Have you installed openpyxl? See PMXlsxImporter/README.md"));
	}
	else
	{
		UE_LOG(LogPMXlsxImporter, Error, TEXT("No python bridge implementation found. Have you installed openpyxl? See PMXlsxImporter/README.md"));
	}
    return nullptr;
}