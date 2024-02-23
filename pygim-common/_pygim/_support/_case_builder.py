# -*- coding: utf-8 -*-
'''
Supporting builder class for case conversion.
'''

import re
from dataclasses import dataclass

__all__ = ["CaseBuilder"]

@dataclass
class CaseBuilder:
    """ A class to convert a string to different case. """
    _word_separator: object = re.compile(r'[^a-zA-Z]+')
    _capital_detector: object = re.compile(r'(?<=[a-z])(?=[A-Z])')
    _trimmer: object = re.compile(r'^ | $')
    _replacer: object = re.compile(r'  +')

    def set_text(self, text):
        self._text = text
        return self

    def set_sep(self, sep):
        self._sep = sep
        return self

    def adjust(self):
        step1 = self._word_separator.sub(' ', self._text)
        self._text = self._capital_detector.sub(' ', step1)
        return self

    def lower(self):
        self._transform_func = str.lower
        return self

    def capitalize(self):
        self._transform_func = str.capitalize
        return self

    def trim(self):
        self._text = self._trimmer.sub('', self._text)
        return self

    def replace(self):
        self._text = self._replacer.sub(' ', self._text)
        return self

    def build(self, sep):
        return sep.join(self._transform_func(word) for word in self._text.split())