# Copyright 2022 Tianqi Li.

import unreal
import os
import io
from pathlib import Path
import openpyxl
import traceback
from typing import List
import pm_xlsx_field_parser as fp
from importlib import *

reload(fp)


class PMXlsxParser:
    def __init__(self, absolute_file_path, worksheet_name, worksheet_type_info: unreal.PMXlsxWorksheetTypeInfo):
        self.absolute_file_path = absolute_file_path
        self.file_name = os.path.basename(absolute_file_path)
        self.worksheet_name = worksheet_name
        self.worksheet_type_info = worksheet_type_info
        self.field_parsers: List[fp.PMXlsxFieldParser] = []

        with open(self.absolute_file_path, "rb") as f:
            in_mem_file = io.BytesIO(f.read())
        workbook = openpyxl.load_workbook(in_mem_file, read_only=True, data_only=True)
        self.worksheet = workbook[self.worksheet_name]

    def parse_header_row(self, header_row_index):
        unreal.log("{0}-{1}: LIST ALL CPP FIELDS:".format(self.file_name, self.worksheet_name))
        for field in self.worksheet_type_info.all_fields:
            unreal.log("{0}-{1}: - index: {2}, name: {3}, type: {5}, cpp_type: {4}, ele_type: {6}".
                       format(self.file_name, self.worksheet_name, field.index, field.name_cpp, field.type,
                              field.cpp_type, field.element_type))

        row = list(self.worksheet.rows)[header_row_index - 1]

        # should have at least one data row
        # if len(worksheet.rows) < header_row_index + 1:
        #     unreal.log_error("Xlsx file {0} - sheet {1} doesn't has any data rows".format(self.absolute_file_path, 
        #                                                                                     self.worksheet_name))
        #     return False

        if len(self.worksheet_type_info.top_fields) == 0:
            raise fp.ValidationError("Data class is not valid, has 0 top fields",
                                     None, None, self.file_name, self.worksheet_name)

        unreal.log("{0}-{1}: PARSE ALL FIELDS:".format(self.file_name, self.worksheet_name))
        column_index = 0
        for field_index in self.worksheet_type_info.top_fields:
            field_parser = fp.factory_create_field_parser(field_index, self.worksheet_type_info)
            try:
                column_index = field_parser.parse_header_row(row, column_index, -1)
            except fp.ValidationError as ex:
                ex.row_index = header_row_index
                ex.file_name = self.file_name
                ex.worksheet_name = self.worksheet_name
                raise ex
            except Exception:
                # handle all other exceptions
                raise fp.ValidationError("field \"{0}\"'s header is not valid:\n{1}".
                                         format(field_parser.get_field_name(), traceback.format_exc(), None,
                                                header_row_index, self.file_name, self.worksheet_name)) from None
            unreal.log("{0}-{1}: - field {2} range [{3}-{4}]".
                       format(self.file_name, self.worksheet_name, field_parser.get_field_name(),
                              field_parser.start_column_index, field_parser.end_column_index - 1))
            self.field_parsers.append(field_parser)

    def __parse_data_row(self, row_index):
        result = {}
        row = list(self.worksheet.rows)[row_index - 1]
        column_index = 0
        for field_parser in self.field_parsers:
            try:
                result[field_parser.get_field_name()] = field_parser.parse_data_row(row)
                # print("- {0}: {1}".format(field_parser.get_field_name(), result[field_parser.get_field_name()]))
            except fp.ValidationError as e:
                raise fp.ValidationError(e.message, e.column_index, row_index, self.file_name,
                                         self.worksheet_name) from e
            except Exception as ex:
                raise fp.ValidationError("field \"{0}\"'s data is not valid:\n{1}".
                                         format(field_parser.get_field_name(), traceback.format_exc()), None, row_index,
                                         self.file_name, self.worksheet_name) from None
            column_index += 1
        return result

    def parse_data(self, start_row):
        result_array = []
        row_index = start_row
        for row in self.worksheet.iter_rows(min_row=start_row):
            if not row[0].value or str(row[0].value).isspace():
                break  # not valid since this row
            row_dict = self.__parse_data_row(row_index)
            result_array.append(row_dict)
            row_index += 1
        return result_array
