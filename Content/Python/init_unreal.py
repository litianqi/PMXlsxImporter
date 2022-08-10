# Copyright 2022 Proletariat, Inc.

import unreal
import os
import io
from pathlib import Path
import openpyxl
import json
import pm_xlsx_parser as xlsx_parser
from importlib import *
reload(xlsx_parser)

@unreal.uclass()
class PMXlsxImporterPythonBridgeImpl(unreal.PMXlsxImporterPythonBridge):

    @unreal.ufunction(override=True)
    def read_worksheet_names(self, absolute_file_path):
        unreal.log("Reading xlsx file \"{0}\"".format(absolute_file_path))

        with open(absolute_file_path, "rb") as f:
            in_mem_file = io.BytesIO(f.read())
        workbook = openpyxl.load_workbook(in_mem_file, read_only=True, data_only=True)
        unreal.log("All sheet names: {0}".format(workbook.sheetnames))
        return workbook.sheetnames

    def parse_headers(self, row):
        headers = []
        for cell in row:
            headers.append(cell.value)
        return headers

    @unreal.ufunction(override=True)
    def read_worksheet(self, absolute_file_path, worksheet_name):
        with open(absolute_file_path, "rb") as f:
            in_mem_file = io.BytesIO(f.read())
        workbook = openpyxl.load_workbook(in_mem_file, read_only=True, data_only=True)
        worksheet = workbook[worksheet_name]

        results = []

        # with unreal.ScopedEditorTransaction("Import from xlsx") as transaction:
        # with unreal.ScopedSlowTask(worksheet.rows.length, "Importing from xlsx") as slow_task:
        # slow_task.make_dialog(True)
        headers = None
        row_index = 0
        for row in worksheet.rows:
            # if slow_task.should_cancel():
            #    break
            # slow_task.enter_progress_frame(i)

            if headers == None:
                headers = self.parse_headers(row)
                continue

            result = unreal.PMXlsxImporterPythonBridgeDataAssetInfo()

            column_index = 0
            for cell in row:
                # unreal.log("{0}: {1} {2}".format(column_index, headers[column_index], cell.value))
                # force keys and values to strings because unreal doesn't know how to convert other types automatically, and we want to pass a TMap<FString, FString> to unreal
                result.data[str(headers[column_index])] = str(cell.value)
                column_index += 1

            result.asset_name = result.data['Name']
            results.append(result)

            row_index += 1

        return results

    @unreal.ufunction(override=True)
    def read_worksheet_as_json(self, absolute_file_path, worksheet_name, worksheet_type_info):
        # parse header row
        parser = xlsx_parser.PMXlsxParser(absolute_file_path, worksheet_name, worksheet_type_info)
        valid = parser.parse_header_row(1)
        if not valid:
            return "ERROR"  # header parse failed, return early
        
        # parse data rows
        data_list = parser.parse_data(2)

        # convert data to json string
        json_string = json.dumps(data_list, indent=4)

        # write json string to Intermediate folder for debug purpose
        json_parent_dir = Path(absolute_file_path).stem
        json_file_name = worksheet_name + '.json'
        json_output_file = Path(os.path.join(
            unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_intermediate_dir()), 'XlsxJsonFiles',
            json_parent_dir, json_file_name))
        json_output_file.parent.mkdir(exist_ok=True, parents=True)
        json_output_file.write_text(json_string)

        return json_string
