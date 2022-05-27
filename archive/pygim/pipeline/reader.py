"""
Contains generic reader.
"""

import abc
from io import FileIO
from collections.abc import Generator
from json import loads
import requests

from ..system.primitives import GimDict as gdict

__all__ = ["JsonReader", "HttpConnector"]


class Reader:
    pass


class Connector:
    def __init__(self, url):
        self._url = url

    def __iter__(self):
        response = requests.get(self._url)
        if response.status_code != 200:
            raise ValueError(response.status_code)
        yield from [requests.get(self._url).text]


class JsonReader(Reader):
    def transform(self, raw_data):
        return [gdict(loads(raw_data))]


class HttpConnector(Connector):
    def connect(self):
        pass

    def close(self):
        pass

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()