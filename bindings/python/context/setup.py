#!/usr/bin/python
from distutils.core import setup, Extension

ext = Extension('sennactx',
                libraries = ['senna'],
                sources = ['sennactx.c'])

setup(name = 'sennactx',
      version = '1.0',
      description = 'python SQTP',
      long_description = '''
      This is a SQTP Python API package.
      ''',
      license='GNU LESSER GENERAL PUBLIC LICENSE',
      author = 'Brazil',
      author_email = 'senna at razil.jp',
      ext_modules = [ext]
     )
