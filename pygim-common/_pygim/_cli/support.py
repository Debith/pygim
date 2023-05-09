# -*- coding: utf-8 -*-
"""
Utilities useful with Command-Line Application.
"""

import functools

import click

__all__ = ["flag_opt"]

flag_opt = functools.partial(click.option, is_flag=True, default=False)
