# Copyright 2022 Tianqi Li.

import unreal
import os
import io
from pathlib import Path
import openpyxl
import json
from typing import List
import pm_xlsx_column_parser as xlsx_column_parser
from importlib import *
reload(xlsx_column_parser)


class PMXlsxParser:
    def __init__(self, absolute_file_path, worksheet_name, worksheet_type_info: unreal.PMXlsxWorksheetTypeInfo):
        self.absolute_file_path = absolute_file_path
        self.file_name = os.path.basename(absolute_file_path)
        self.worksheet_name = worksheet_name
        self.worksheet_type_info = worksheet_type_info
        self.column_parsers: List[xlsx_column_parser.PMXlsxColumnParser] = []

        with open(self.absolute_file_path, "rb") as f:
            in_mem_file = io.BytesIO(f.read())
        workbook = openpyxl.load_workbook(in_mem_file, read_only=True, data_only=True)
        self.worksheet = workbook[self.worksheet_name]

    def __get_first_child_field_index(self, field_index):
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[field_index]
        while len(field.child_indices) > 0:
            field_index = field.child_indices[0]
            field = self.worksheet_type_info.all_fields[field_index]
        return field_index
    
    def __get_last_child_field_index(self, field_index):
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[field_index]
        while len(field.child_indices) > 0:
            field_index = field.child_indices[-1]
            field = self.worksheet_type_info.all_fields[field_index]
        return field_index

    def __increment_field_index(self, field_index):
        current_field_index = field_index
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[current_field_index]
        parent_field_index = field.parent_index

        results = []

        while parent_field_index >= 0:
            parent_field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[parent_field_index]
            index_in_child_indices = parent_field.child_indices.index(current_field_index)
            if index_in_child_indices < len(parent_field.child_indices) - 1:
                # append sibling field and exit
                sibling_field = self.__get_first_child_field_index(
                    parent_field.child_indices[index_in_child_indices + 1])
                results.append(sibling_field)
                break
            else:
                # this is the last field in child indices
                if parent_field.type == unreal.PMXlsxFieldType.ARRAY:
                    # since this is an array, it may repeat several times
                    results.append(self.__get_first_child_field_index(parent_field_index))
                # go up until we find a sibling field
                current_field_index = parent_field_index
                parent_field_index = parent_field.parent_index

        # this is a top field index
        # either the passed field is the last child field of a top field, or is a top field itself
        if parent_field_index < 0:
            index_in_top_fields = self.worksheet_type_info.top_fields.index(current_field_index)
            if field.type == unreal.PMXlsxFieldType.ARRAY:
                # since this is an array, it may repeat several times
                first_child_field = self.__get_first_child_field_index(current_field_index)
                results.append(first_child_field)
            if index_in_top_fields < len(self.worksheet_type_info.top_fields) - 1:
                # append sibling field
                sibling_field = self.__get_first_child_field_index(
                    self.worksheet_type_info.top_fields[index_in_top_fields + 1])
                results.append(sibling_field)
        # print("__increment_field_index: {0} => {1}".format(field_index, results))
        return results

    def parse_header_row(self, header_row_index):
        unreal.log("{0}-{1} LIST ALL CPP FIELDS:".format(self.file_name, self.worksheet_name))
        for field in self.worksheet_type_info.all_fields:
            unreal.log("{0}-{1} - index: {2}, name: {3}, type: {5}, cpp_type: {4}, ele_type: {6}".
                       format(self.file_name, self.worksheet_name, field.index, field.name_cpp, field.type,
                              field.cpp_type, field.element_type))

        row = list(self.worksheet.rows)[header_row_index - 1]

        # should have at least one data row
        # if len(worksheet.rows) < header_row_index + 1:
        #     unreal.log_error("Xlsx file {0} - sheet {1} doesn't has any data rows".format(self.absolute_file_path, 
        #                                                                                     self.worksheet_name))
        #     return False

        if len(self.worksheet_type_info.top_fields) == 0:
            unreal.log_error("{0}-{1} doesn't has any column".
                             format(self.file_name, self.worksheet_name))
            return False

        unreal.log("{0}-{1} PARSE ALL COLUMNS:".format(self.file_name, self.worksheet_name))
        possible_field_indices = [self.__get_first_child_field_index(self.worksheet_type_info.top_fields[0])]
        for cell in row:
            column_name = cell.value
            if not column_name or column_name.isspace():
                break

            column_parser = xlsx_column_parser.PMXlsxColumnParser(column_name, self.worksheet_type_info)
            valid, reason = column_parser.parse_column_header(possible_field_indices)
            if not valid:
                unreal.log_error("{0}-{1}'s column {2} is not consist with cpp type or name, reason: {3}".
                                 format(self.file_name, self.worksheet_name, column_name, reason))
                return False
            unreal.log("{0}-{1} - {2}: {3} (candidates: {4})".
                       format(self.file_name, self.worksheet_name, column_name, column_parser.field_index,
                              possible_field_indices))

            self.column_parsers.append(column_parser)
            possible_field_indices = self.__increment_field_index(column_parser.field_index)
            if len(possible_field_indices) == 0:
                break  # this is the last one
        
        # check is any cpp fields are missing in xlsx
        last_field_index = self.__get_last_child_field_index(self.worksheet_type_info.top_fields[-1])
        if self.column_parsers[-1].field_index != last_field_index:
            unreal.log_error("{0}-{1}: one or more columns are missing".
                             format(self.file_name, self.worksheet_name))
            return False
        
        return True

    def __parse_data_row(self, row_index):
        row_dict = {}
        row = list(self.worksheet.rows)[row_index - 1]
        column_index = 0
        for column_parser in self.column_parsers:
            cell_raw_data = row[column_index].value
            column_parser.parse_data_and_insert_to_dict(cell_raw_data, row_dict)
            column_index += 1
        return row_dict

    def parse_data(self, start_row):
        result_array = []
        row_index = start_row
        for row in self.worksheet.iter_rows(min_row=start_row):
            if not row[0].value or row[0].value.isspace():
                break  # not valid since this row    
            row_dict = self.__parse_data_row(row_index)
            result_array.append(row_dict)
            row_index += 1
        return result_array
