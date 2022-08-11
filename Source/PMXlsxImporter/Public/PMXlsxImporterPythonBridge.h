// Copyright 2022 Proletariat, Inc.

#pragma once

#include "CoreMinimal.h"
#include "PMXlsxImporterPythonReflection.h"
#include "PMXlsxImporterPythonBridge.generated.h"

class FPMXlsxImporterContextLogger;

USTRUCT(Blueprintable, BlueprintType)
struct FPMXlsxImporterPythonBridgeAssetNames
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	TArray<FString> AssetNames;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString Error;
};

USTRUCT(Blueprintable, BlueprintType)
struct FPMXlsxImporterPythonBridgeJsonString
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString JsonString;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString Error;
};

UCLASS(Blueprintable)
class UPMXlsxImporterPythonBridge : public UObject
{
	GENERATED_BODY()

public:

	// Returns the Python subclass of UPMXlsxImporterPythonBridge which contains all of the
	// UFUNCTION(BlueprintImplementableEvent)s that have been implemented in Python
	// See https://forums.unrealengine.com/t/running-a-python-script-with-c/114117/3
	static UPMXlsxImporterPythonBridge* Get(FPMXlsxImporterContextLogger* InOutErrors = nullptr);

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	TArray<FString> ReadWorksheetNames(const FString& AbsoluteFilePath);

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	FPMXlsxImporterPythonBridgeAssetNames ReadWorksheetAssetNames(const FString& AbsoluteFilePath, const FString& WorksheetName, int32 HeaderRow, int32 DataStartRow);

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	FPMXlsxImporterPythonBridgeJsonString ReadWorksheetAsJson(const FString& AbsoluteFilePath, const FString& WorksheetName, int32 HeaderRow, int32 DataStartRow, const FPMXlsxWorksheetTypeInfo& WorksheetTypeInfo);
};
