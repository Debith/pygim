# -*- coding: utf-8 -*-
'''
This module implmements various string tools.
'''

__all__ = ['banner']


def banner(text: str, width: int = 78, char: str = "#") -> str:
    '''
    Returns a string with the given text centered and surrounded by the given character.

    Parameters
    ----------
    text : str
        The text to be displayed in the banner.
    width : int, optional
        The width of the banner. Default is 78.
    char : str, optional
        The character used to surround the text. Default is "#".

    Returns
    -------
    str
        The banner string.

    Raises
    ------
    ValueError
        If the length of the text is greater than the specified width minus 4.
    '''
    text_length = len(text)
    if text_length > width - 4:
        raise ValueError("Text is too long for the specified width.")

    horizontal_line = char * width
    empty_line = char + " " * (width - 2) + char

    top_line = horizontal_line
    bottom_line = horizontal_line
    middle_line = empty_line

    if text_length % 2 == 0:
        left_padding = (width - 2 - text_length) // 2
        right_padding = left_padding
    else:
        left_padding = (width - 2 - text_length) // 2
        right_padding = left_padding + 1

    middle_line = char + " " * left_padding + text + " " * right_padding + char

    return "\n".join([top_line, middle_line, bottom_line])
