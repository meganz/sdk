#!/bin/env python
import os
import re
from setuptools import setup


def read(fname):
    return open(os.path.join(os.path.dirname(__file__), fname)).read()


def get_version():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    version_path = os.path.join(script_dir, "..", "..", "include", "mega", "version.h")

    with open(version_path, "r") as f:
        version_file = f.read()

    major_match = re.search(r"#define\s+MEGA_MAJOR_VERSION\s+(\d+)", version_file)
    minor_match = re.search(r"#define\s+MEGA_MINOR_VERSION\s+(\d+)", version_file)
    patch_match = re.search(r"#define\s+MEGA_MICRO_VERSION\s+(\d+)", version_file)

    major = major_match.group(1) if major_match else "0"
    minor = minor_match.group(1) if minor_match else "0"
    patch = patch_match.group(1) if patch_match else "0"

    version = f"{major}.{minor}.{patch}"

    return version


setup(
    name="megasdk",
    version=get_version(),
    description="Python bindings to the Mega file storage SDK.",
    long_description=read("DESCRIPTION.rst"),
    url="http://github.com/meganz/sdk/",
    license="Simplified BSD",
    author="Guy Kloss",
    author_email="gk@mega.co.nz",
    packages=["mega"],
    package_dir={"mega": "."},
    package_data={
        "mega": ["_SDKPythonBindings.so", "mega.py"],
    },
    include_package_data=True,
    keywords=["MEGA", "privacy", "cloud", "storage", "API"],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Natural Language :: English",
        "License :: OSI Approved :: BSD License",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Programming Language :: Python :: 2",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: Implementation :: CPython",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
)
