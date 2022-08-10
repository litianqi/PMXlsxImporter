# Copyright 2022 Tianqi Li.

import unreal
import os
import io
from pathlib import Path
import openpyxl
import json
from typing import List


def is_int(value):
    try:
        int(value)
        return True
    except:
        return False


class PMXlsxColumnParser:
    def __init__(self, column_name, worksheet_type_info: unreal.PMXlsxWorksheetTypeInfo):
        self.column_name = column_name
        self.worksheet_type_info = worksheet_type_info
        self.field_index = -1
        self.array_index = -1

    def __repr__(self):
        return self.__str__()

    def __str__(self):
        return "column_name: {0}, field_index: {1}, array_index: {2}".format(self.column_name, self.field_index,
                                                                             self.array_index)

    def __validate_column_header_with_field_index(self, column_parts: List[str], field_index: int):
        # check field names
        cur_field_index = field_index
        for column_part in reversed(column_parts):
            if cur_field_index < 0:
                return False, "redundant fields in column header"
            field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[cur_field_index]
            if field.name_cpp != column_part:
                return False, "column header field name not match with cpp name"
            cur_field_index = field.parent_index

        if cur_field_index >= 0:
            return False, "missing fields in column header"

        # check array
        cur_field_index = field_index
        while cur_field_index >= 0:
            field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[cur_field_index]
            is_array = field.type == unreal.PMXlsxFieldType.ARRAY
            if field.parent_index < 0:  # top field
                if self.array_index >= 0 and not is_array:
                    return False, "column header's top field is an array (should not be)"
            else:  # child fields
                if is_array:
                    return False, "right now only top field can be an array"
            cur_field_index = field.parent_index

        return True, None

    def parse_column_header(self, possible_field_indices: List[int]):  # TODO: also pass in array index to validate
        column_parts = self.column_name.split('.')
        if len(column_parts) == 0:
            return False, "column header is empty"

        # parse array index (only first part can be an array)
        first_part = column_parts[0]
        left_bracket_index = first_part.find("[")
        if left_bracket_index > 0:
            if not first_part.endswith("]"):
                return False, "column header missing right bracket"
            array_index_text = first_part[left_bracket_index + 1:-1]
            if not is_int(array_index_text):
                return False, "array index in column header is not a valid number"
            self.array_index = int(array_index_text)
            column_parts[0] = first_part[0:left_bracket_index]  # replace filed_name[array_index] with field_name

        # parse fields (check is fields valid)
        failed_reasons = []
        for possible_field_index in possible_field_indices:
            valid, reason = self.__validate_column_header_with_field_index(column_parts, possible_field_index)
            if valid:
                self.field_index = possible_field_index
                return True, None
            failed_reasons.append("{0}: {1}".format(possible_field_index, reason))
        return False, ", ".join(failed_reasons)

    def __parse_data(self, raw_data: str):
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[self.field_index]
        if field.type == unreal.PMXlsxFieldType.ARRAY:
            if field.element_type == unreal.PMXlsxFieldType.NUMERIC:
                if field.element_cpp_type == "float" or field.element_cpp_type == "double":
                    return [float(x.lstrip().rstrip()) for x in raw_data.split(",")]
                else:
                    return [int(x.lstrip().rstrip()) for x in raw_data.split(",")]
            return [x.lstrip().rstrip() for x in raw_data.split(",")]
        return raw_data

    def __get_top_field(self):
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[self.field_index]
        while field.parent_index >= 0:
            field = self.worksheet_type_info.all_fields[field.parent_index]
        return field

    def __get_field_chain(self):
        field = self.worksheet_type_info.all_fields[self.field_index]
        field_chain: List[unreal.PMXlsxFieldTypeInfo] = [field]
        while field.parent_index >= 0:
            field = self.worksheet_type_info.all_fields[field.parent_index]
            field_chain.append(field)
        field_chain.reverse()  # reverse child->parent to parent->child
        return field_chain

    def parse_data_and_insert_to_dict(self, raw_data, target_dict):
        data = self.__parse_data(raw_data)

        # handle primitive types (includes struct/container types that can be read from a str)
        #   includes primitive types from a split struct/array

        field = self.worksheet_type_info.all_fields[self.field_index]
        if field.parent_index < 0:
            # print("parse_data_and_insert_to_dict {2}: data {0} target_dict {1}".format(data, target_dict, field.name_cpp))
            if self.array_index >= 0:  # this column is an array element
                if field.name_cpp not in target_dict:
                    target_dict[field.name_cpp] = []  # insert an empty array to dict
                target_dict[field.name_cpp].append(data)
            else:  # primitive type
                target_dict[field.name_cpp] = data
            return

        # handle struct or array of struct or struct of struct

        # 1. create the struct/array for top field
        top_field = self.__get_top_field()

        # print("parse_data_and_insert_to_dict {0}: target_dict {1}".format(top_field.name_cpp, target_dict))
        if top_field.name_cpp not in target_dict:
            if self.array_index >= 0:  # array of struct
                target_dict[top_field.name_cpp] = []
                # print("parse_data_and_insert_to_dict {0}: insert empty array".format(top_field.name_cpp))
            else:  # struct
                target_dict[top_field.name_cpp] = {}
                # print("parse_data_and_insert_to_dict {0}: insert empty struct".format(top_field.name_cpp))
        cur_dict = target_dict[top_field.name_cpp]
        # print("parse_data_and_insert_to_dict {0}: array index {1}, cur_dict {2}".format(top_field.name_cpp, top_field_info.array_index, cur_dict))

        if self.array_index >= 0:
            if self.array_index == len(target_dict[top_field.name_cpp]):
                target_dict[top_field.name_cpp].append({})
                # print("parse_data_and_insert_to_dict {0}: insert empty struct to array".format(top_field.name_cpp))
            cur_dict = target_dict[top_field.name_cpp][self.array_index]
            # print("parse_data_and_insert_to_dict {0}: use {1} of array as new top struct, now top struct is {2}".format(top_field.name_cpp, top_field_info.array_index, cur_dict))

        # 2. create structs for other fields 

        field_chain = self.__get_field_chain()
        for field_chain_index in range(1, len(field_chain)):
            is_last_field = field_chain_index == len(field_chain) - 1
            field = field_chain[field_chain_index]
            if is_last_field:
                cur_dict[field.name_cpp] = data
            else:
                if field.name_cpp not in cur_dict:
                    cur_dict[field.name_cpp] = {}
                    cur_dict = cur_dict[field.name_cpp]
