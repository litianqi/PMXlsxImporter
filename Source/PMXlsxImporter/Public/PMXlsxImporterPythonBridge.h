// Copyright 2022 Proletariat, Inc.

#pragma once

#include "CoreMinimal.h"
#include "PMXlsxImporterPythonReflection.h"
#include "PMXlsxImporterPythonBridge.generated.h"

USTRUCT(Blueprintable, BlueprintType)
struct FPMXlsxImporterPythonBridgeDataAssetInfo
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString AssetName;
};

UCLASS(Blueprintable)
class UPMXlsxImporterPythonBridge : public UObject
{
	GENERATED_BODY()

public:

	// Returns the Python subclass of UPMXlsxImporterPythonBridge which contains all of the
	// UFUNCTION(BlueprintImplementableEvent)s that have been implemented in Python
	// See https://forums.unrealengine.com/t/running-a-python-script-with-c/114117/3
	static UPMXlsxImporterPythonBridge* Get();

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	TArray<FString> ReadWorksheetNames(const FString& AbsoluteFilePath);

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	TArray<FString> ReadWorksheetNameColumn(const FString& AbsoluteFilePath, const FString& WorksheetName, int32 DataStartRow);

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	FString ReadWorksheetAsJson(const FString& AbsoluteFilePath, const FString& WorksheetName, int32 HeaderRow, int32 DataStartRow, const FPMXlsxWorksheetTypeInfo& WorksheetTypeInfo);
};
