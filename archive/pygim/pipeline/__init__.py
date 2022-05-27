import abc
from dataclasses import dataclass, field
from ..system import Factory, Name
from .reader import Connector, HttpConnector, Reader, JsonReader


@dataclass
class Bundle:
    connector: Connector
    reader: Reader

    def __iter__(self):
        connector = self.connector(self._url)
        reader = self.reader()

        connector.connect()

        for raw_data in connector:
            for transformed_data in reader.transform(raw_data):
                if self._driver:
                    yield from self._driver(transformed_data)
                else:
                    yield transformed_data

        connector.close()

    def __call__(self, url, *, driver=None):
        self._url = url
        self._driver = driver

        return self

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass


class LoaderFactoryMeta(type):
    _connectors = dict()
    _readers = dict()

    def __getitem__(self, key):
        connector, reader = key.split(',')

        bundle = Bundle(
            self._connectors[connector],
            self._readers[reader],
        )

        return bundle

    def register_connector(self, kind, connector):
        self._connectors[kind] = connector

    def register_reader(self, kind, reader):
        self._readers[kind] = reader


class LoaderFactory(metaclass=LoaderFactoryMeta):
    pass


LoaderFactory.register_connector('http', HttpConnector)
LoaderFactory.register_reader('json', JsonReader)


__all__ = ['LoaderFactory']