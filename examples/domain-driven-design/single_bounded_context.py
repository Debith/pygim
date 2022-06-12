# -*- coding: utf-8 -*-
"""
This example shows in a single file, how to configure and create simple bounded context application.
"""

import abc
from fileinput import filename
import pathlib
from dataclasses import dataclass

class LoadRepository(abc.ABC):
    @abc.abstractmethod
    def load(self):
        pass


class SaveRepository(abc.ABC):
    @abc.abstractmethod
    def save(self):
        pass


class Repository(LoadRepository, SaveRepository):
    pass


class Factory(abc.ABC):
    @abc.abstractmethod
    def create(self):
        pass


class BoundedContext:
    pass


class ExampleBuilder(Factory):
    def create():
        pass


@dataclass
class ExampleApplication:
    load_func: callable
    create_bc: callable
    transform_func: callable
    ui_func: callable

    PARSERS = {}

    @classmethod
    def register_parser(cls, extension):
        def _dec(func):
            cls.PARSERS[extension] = func
            return func
        return _dec

    def show_file_contents(self, file_name):
        factory = type('', (), {})()

        full_file_name = pathlib.Path(__file__).parent / file_name
        dto = full_file_name.read_text()
        parsed = self.PARSERS[full_file_name.suffix](dto)
        agg = self.create_bc(parsed)
        root_agg = self.transform_func(agg)
        self.ui_func(root_agg)


import json
import csv

@ExampleApplication.register_parser(".json")
def parse_json(data):
    return json.loads(data)[0]['description']


@ExampleApplication.register_parser(".csv")
def parse_csv(data):
    return list(csv.reader(data.splitlines()))[-1][-1]


def load_file(factory: Factory, filename):
    text = json.loads(filename.read())
    factory.text = text[0]['description']


def create_bc(factory):
    return factory


def transform_data(root_agg):
    root_agg = root_agg.splitlines()
    return root_agg

def render_data(vm):
    print("\n".join(vm))






example_app = ExampleApplication(load_file, create_bc, transform_data, render_data)
example_app.show_file_contents("single_bounded_context_data.json")
example_app.show_file_contents("single_bounded_context_data.csv")