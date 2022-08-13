# Copyright 2022 Tianqi Li.
import traceback

import unreal
import os
import io
import re
from pathlib import Path
import openpyxl
import json
import hjson
from typing import List


class ValidationError(Exception):
    def __init__(self, message, column_index, row_index, file_name, worksheet_name):
        self.message = message
        self.file_name = file_name
        self.worksheet_name = worksheet_name
        self.row_index = row_index
        self.column_index = column_index

    def __str__(self):
        if self.row_index is not None and self.column_index is not None:
            return "{0}:{1}-[{2}:{3}]: {4}". \
                format(self.file_name, self.worksheet_name, self.row_index,
                       xl_col_to_name(self.column_index + 1), self.message)
        elif self.row_index is not None:
            return "{0}:{1}-[{2}:?]: {3}". \
                format(self.file_name, self.worksheet_name, self.row_index, self.message)
        else:
            return "{0}:{1}: {2}".format(self.file_name, self.worksheet_name, self.message)


def is_int(value: str):
    try:
        int(value)
        return True
    except (ValueError, Exception) as ex:
        return False


def xl_col_to_name(column_int: int):
    start_index = 1  # it can start either at 0 or at 1
    letter = ''
    while column_int > 25 + start_index:
        letter += chr(65 + int((column_int - start_index) / 26) - 1)
        column_int = column_int - (int((column_int - start_index) / 26)) * 26
    letter += chr(65 - start_index + (int(column_int)))
    return letter


def factory_create_field_parser(field_index: int, worksheet_type_info: unreal.PMXlsxWorksheetTypeInfo):
    field: unreal.PMXlsxFieldTypeInfo = worksheet_type_info.all_fields[field_index]
    if field.type == unreal.PMXlsxFieldType.ARRAY:
        return PMXlsxArrayFieldParser(field_index, worksheet_type_info)
    elif field.type == unreal.PMXlsxFieldType.STRUCT:
        return PMXlsxStructFieldParser(field_index, worksheet_type_info)
    else:
        return PMXlsxFieldParser(field_index, worksheet_type_info)


class PMXlsxFieldParser:
    def __init__(self, field_index: int, worksheet_type_info: unreal.PMXlsxWorksheetTypeInfo):
        self.field_index = field_index
        self.worksheet_type_info = worksheet_type_info
        self.start_column_index = -1
        self.end_column_index = -1

    def get_field_name(self):
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[self.field_index]
        return field.name_cpp

    def _validate_column_name(self, header_row, column_index, array_index: int):
        """
        Validate column name's syntax, raise a ValueError exception if syntax error occurs
        :param column_index: column index in header row
        :param array_index: >= 0 means which [array index] this column has, -1 means column doesn't has []
        """
        if column_index >= len(header_row):
            raise ValidationError("miss filed \"{0}\" in header row".format(self.get_field_name()), column_index, None,
                                  None, None)

        cell = header_row[column_index]
        column_name: str = cell.value

        if not column_name or column_name.isspace():
            raise ValidationError("column name is empty", column_index, None, None, None)

        column_parts: List[str] = column_name.split('.')
        column_parts.reverse()

        field_index = self.field_index
        column_part_idx = 0
        while field_index > 0:
            field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[field_index]
            if column_part_idx >= len(column_parts):
                raise ValidationError("column name {0} is the same with cpp name, oen or more fields are missing".
                                      format(column_name), column_index, None, None, None)
            column_part = column_parts[column_part_idx].strip()
            if field.type == unreal.PMXlsxFieldType.ARRAY:
                left_bracket_index = column_part.find("[")
                # validate [
                has_array_index = False
                if left_bracket_index > 0:
                    if array_index < 0:
                        raise ValidationError("unexpected [ in column name {0}, doesn't inconsistent "
                                              "with array's first column".format(column_name), column_index, None, None,
                                              None)
                    else:
                        has_array_index = True
                else:
                    if array_index >= 0:
                        raise ValidationError("missing [ in column name {0}".format(column_name), column_index, None,
                                              None, None)

                if has_array_index:
                    # validate ]
                    if not column_part.endswith("]"):
                        raise ValidationError("missing ] in column name {0}".format(column_name), column_index, None,
                                              None, None)

                    # validate array_index
                    array_index_text = column_part[left_bracket_index + 1:-1].strip()
                    if not is_int(array_index_text):
                        raise ValidationError("array index in {0} is not a valid number".
                                              format(column_name), column_index, None, None, None)
                    if array_index != int(array_index_text):
                        raise ValidationError("array index in {0} is wrong, expect {1} got {2}".
                                              format(column_name, array_index, array_index_text), column_index, None,
                                              None, None)

                    # validate field_name
                    field_name_text = column_part[0:left_bracket_index].strip()
                    if field_name_text != field.name_cpp:
                        raise ValidationError("column name {0} is not the same with cpp name, expect {1} got {2}".
                                              format(column_name, field.name_cpp, field_name_text), column_index, None,
                                              None, None)
                else:
                    # validate field_name
                    if field.name_cpp != column_part:
                        raise ValidationError("column name {0} is not the same with cpp name, expect {1} got {2}".
                                              format(column_name, field.name_cpp, column_part), column_index, None,
                                              None, None)
            else:
                # validate field_name
                if field.name_cpp != column_part:
                    raise ValidationError("column name {0} is not the same with cpp name, expect {1} got {2}".
                                          format(column_name, field.name_cpp, column_part), column_index, None, None,
                                          None)
            field_index = field.parent_index
            column_part_idx += 1

    def parse_header_row(self, header_row, start_column_index: int, array_index: int):
        """
        Parse header row to determine how many columns this filed occupies, and validate if these column names are valid
        :return: end column index
        """
        # this will raise an exception if error occurs
        self._validate_column_name(header_row, start_column_index, array_index)

        self.start_column_index = start_column_index
        self.end_column_index = start_column_index + 1  # ordinary type only occupy one column

        return self.end_column_index

    def _parse_data_cell(self, data_row, column_index: int, field_index: int):
        cell = data_row[column_index]
        raw_value: str = cell.value
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[field_index]

        field_type = field.type
        field_cpp_type = field.cpp_type
        if field_type == unreal.PMXlsxFieldType.ARRAY:
            field_type = field.element_type
            field_cpp_type = field.element_cpp_type

        try:
            if field_type == unreal.PMXlsxFieldType.NUMERIC:
                if field_cpp_type == "float" or field_cpp_type == "double":
                    return float(raw_value)
                else:
                    return int(raw_value)
            elif field_type == unreal.PMXlsxFieldType.BOOL:
                if isinstance(raw_value, bool):
                    return raw_value
                elif raw_value.casefold() == "true".casefold():
                    return True
                elif raw_value.casefold() == "false".casefold():
                    return False
                else:
                    raise ValueError()
            elif field_type == unreal.PMXlsxFieldType.ENUM:
                if isinstance(raw_value, int):
                    return raw_value
                elif str(raw_value).isspace():
                    raise ValueError()
            # more type checks can be added here
        except (ValueError, TypeError) as ex:
            raise ValidationError("value {0} is not a valid {1}".
                                  format(raw_value, field_cpp_type), column_index, None, None, None) from None
        except Exception as ex:
            raise ValidationError("error while parse value {0} (type {1})\n{2}".
                                  format(raw_value, field_cpp_type, traceback.format_exc()),
                                  column_index, None, None, None) from None

        return raw_value

    def parse_data_row(self, data_row):
        """
        Parse data row to get a valid Python value (a List, a Dict or an ordinary value)
        :return: parsed value
        """
        return self._parse_data_cell(data_row, self.start_column_index, self.field_index)

    def is_data_row_empty(self, data_row):
        """
        Returns whether data row is empty, ignore validity of data
        """
        for column_index in range(self.start_column_index, self.end_column_index):
            cell = data_row[column_index]
            raw_value: str = cell.value
            if raw_value and not str(raw_value).isspace():
                return False
        return True


class PMXlsxStructFieldParser(PMXlsxFieldParser):
    def __init__(self, field_index: int, worksheet_type_info: unreal.PMXlsxWorksheetTypeInfo):
        super().__init__(field_index, worksheet_type_info)
        self.child_parsers: List[PMXlsxFieldParser] = []

    def parse_header_row(self, header_row, start_column_index: int, array_index: int):
        self.start_column_index = start_column_index

        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[self.field_index]

        if len(field.child_indices) > 0:
            next_column_index = start_column_index
            for child_index in field.child_indices:
                child_field = self.worksheet_type_info.all_fields[child_index]
                if child_field.type == unreal.PMXlsxFieldType.ARRAY:
                    raise ValidationError("array in struct is not allowed", next_column_index, None, None, None)
                else:
                    child_parser = factory_create_field_parser(child_index, self.worksheet_type_info)
                    next_column_index = child_parser.parse_header_row(header_row, next_column_index, array_index)
                    self.child_parsers.append(child_parser)
            self.end_column_index = next_column_index
        else:
            PMXlsxFieldParser.parse_header_row(self, header_row, start_column_index, array_index)
        return self.end_column_index

    def parse_data_row(self, data_row):
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[self.field_index]

        if len(field.child_indices) > 0:
            result = {}
            for child_parser in self.child_parsers:
                result[child_parser.get_field_name()] = child_parser.parse_data_row(data_row)
            return result
        else:
            cell = data_row[self.start_column_index]
            raw_data: str = cell.value
            strip_data = raw_data.strip()
            if strip_data.startswith("{") and strip_data.endswith("}"):
                # struct with { and } is treated as a json string
                try:
                    return hjson.loads(strip_data)
                except (ValueError, Exception) as ex:
                    raise ValidationError("{0} is not a valid struct".format(strip_data), self.start_column_index, None,
                                          None, None) from None
            else:
                # struct without { and } is treated as a ordinary string
                return raw_data


class PMXlsxArrayFieldParser(PMXlsxFieldParser):
    def __init__(self, field_index: int, worksheet_type_info: unreal.PMXlsxWorksheetTypeInfo):
        super().__init__(field_index, worksheet_type_info)
        self.child_parsers: List[PMXlsxFieldParser] = []
        self.is_array_in_one_cell = False
        self.array_length = 0  # only set if there is child_parsers

    def __is_column_belongs_to_this_field(self, header_row, column_index: int):
        if column_index >= len(header_row):
            return False
        column_name: str = header_row[column_index].value
        return column_name and not column_name.isspace() and column_name.lstrip().startswith(self.get_field_name())

    def parse_header_row(self, header_row, start_column_index: int, array_index: int):
        if start_column_index >= len(header_row):
            raise ValidationError("miss filed \"{0}\" in header row".format(self.get_field_name()), start_column_index,
                                  None, None, None)

        self.start_column_index = start_column_index

        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[self.field_index]
        next_column_index = start_column_index

        if len(field.child_indices) > 0:  # array of struct
            array_index = 0
            while self.__is_column_belongs_to_this_field(header_row, next_column_index):
                for child_field_index in field.child_indices:
                    child_parser = factory_create_field_parser(child_field_index, self.worksheet_type_info)
                    next_column_index = child_parser.parse_header_row(header_row, next_column_index, array_index)
                    self.child_parsers.append(child_parser)
                array_index += 1
            self.array_length = array_index
        else:  # array of ordinary types
            start_column_name = header_row[start_column_index].value
            self.is_array_in_one_cell = start_column_name and start_column_name.find("[") < 0
            if self.is_array_in_one_cell:
                next_column_index = PMXlsxFieldParser.parse_header_row(self, header_row, start_column_index, -1)
            else:  # array split on multiple cells
                array_index = 0
                while self.__is_column_belongs_to_this_field(header_row, next_column_index):
                    next_column_index = PMXlsxFieldParser.parse_header_row(self, header_row, start_column_index,
                                                                           array_index)
                    array_index += 1
        self.end_column_index = next_column_index
        return self.end_column_index

    def parse_data_row(self, data_row):
        field: unreal.PMXlsxFieldTypeInfo = self.worksheet_type_info.all_fields[self.field_index]

        if len(self.child_parsers) > 0:  # array of struct
            result = []
            for array_index in range(0, self.array_length):
                # check empty
                is_empty_element = True
                for field_th in range(0, len(field.child_indices)):
                    parser_index = array_index * len(field.child_indices) + field_th
                    child_parser = self.child_parsers[parser_index]
                    if not child_parser.is_data_row_empty(data_row):
                        is_empty_element = False
                        break
                if is_empty_element:
                    break  # stop on first empty element
                else:
                    # parse data
                    array_element = {}
                    for field_th in range(0, len(field.child_indices)):
                        parser_index = array_index * len(field.child_indices) + field_th
                        child_parser = self.child_parsers[parser_index]
                        data = child_parser.parse_data_row(data_row)
                        array_element[child_parser.get_field_name()] = data
                    result.append(array_element)
            return result
        else:  # array of ordinary types
            if self.is_array_in_one_cell:
                cell = data_row[self.start_column_index]
                raw_data: str = cell.value
                strip_data = raw_data.strip()
                try:
                    if strip_data.startswith("[") and strip_data.endswith("]"):
                        return hjson.loads(strip_data)
                    else:
                        # [ and ] can be omitted
                        return hjson.loads("[ " + strip_data + " ]")
                except ValueError as ex:
                    raise ValidationError("data {0} is not a valid array".
                                          format(strip_data), self.start_column_index, None, None, None) from None
                except Exception as ex:
                    raise ValidationError("error while parse array {0}\n{1}".
                                          format(strip_data, traceback.format_exc()),
                                          self.start_column_index, None, None, None) from None
            else:  # array split on multiple cells
                result = []
                for column_index in range(self.start_column_index, self.end_column_index):
                    # check empty
                    cell = data_row[column_index]
                    raw_value: str = cell.value
                    if not raw_value or str(raw_value).isspace():
                        break  # stop on first empty element
                    # parse data
                    data = self._parse_data_cell(data_row, column_index, self.field_index)
                    result.append(data)
                return result
