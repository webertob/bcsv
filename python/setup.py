#!/usr/bin/env python3

"""
Setup script for pybcsv - Python bindings for the BCSV library
"""

import os
import sys
from pathlib import Path
from distutils.core import setup, Extension

# Add current directory to path for imports
current_dir = Path(__file__).parent.absolute()
project_root = current_dir.parent

# Try to import pybind11
PYBIND11_AVAILABLE = False
try:
    import pybind11
    from pybind11.setup_helpers import Pybind11Extension, build_ext
    PYBIND11_AVAILABLE = True
except ImportError:
    # Fallback Extension class when pybind11 is not available during setup
    class Pybind11Extension(Extension):
        def __init__(self, name, sources, *args, **kwargs):
            # Remove pybind11-specific arguments
            kwargs.pop('language', None)
            kwargs.pop('cxx_std', None)
            super().__init__(name, sources, *args, **kwargs)

def get_version():
    """Get version from __version__.py file"""
    version_file = current_dir / "pybcsv" / "__version__.py"
    if version_file.exists():
        with open(version_file, "r") as f:
            content = f.read()
            for line in content.split('\n'):
                if line.startswith('__version__'):
                    return line.split('=')[1].strip().strip('"').strip("'")
    return "0.1.0"

# Define include paths
include_dirs = [
    str(project_root / "include"),  # BCSV headers
    str(project_root / "include" / "lz4-1.10.0"),  # LZ4 headers
]

# Add pybind11 include if available
if PYBIND11_AVAILABLE:
    include_dirs.append(pybind11.get_include())

# Define source files (use relative paths)
source_files = [
    str((current_dir / "pybcsv" / "bindings.cpp").relative_to(current_dir)),
]

# Copy LZ4 source files to local directory to avoid absolute paths
lz4_dir = current_dir / "lz4_src"
lz4_dir.mkdir(exist_ok=True)

# Copy LZ4 files if they don't exist
lz4_source_dir = project_root / "include" / "lz4-1.10.0"
lz4_files = ["lz4.c", "lz4hc.c"]

for lz4_file in lz4_files:
    local_file = lz4_dir / lz4_file
    if not local_file.exists():
        original_file = lz4_source_dir / lz4_file
        if original_file.exists():
            import shutil
            shutil.copy2(original_file, local_file)
    
    if local_file.exists():
        source_files.append(str(local_file.relative_to(current_dir)))

# Define compile arguments
compile_args = [
    "-std=c++20",
    "-O3",
    "-Wall",
    "-Wextra",
    "-fPIC"
]

# Create extension module
if PYBIND11_AVAILABLE:
    ext_modules = [
        Pybind11Extension(
            "_bcsv",
            source_files,
            include_dirs=include_dirs,
            language="c++",
            cxx_std=20,
            extra_compile_args=compile_args,
        )
    ]
else:
    ext_modules = [
        Extension(
            "_bcsv",
            source_files,
            include_dirs=include_dirs,
            extra_compile_args=compile_args,
            language="c++",
        )
    ]

if __name__ == "__main__":
    setup(
        name="pybcsv",
        version=get_version(),
        author="Tobias",
        author_email="",
        description="Python bindings for the high-performance BCSV (Binary CSV) library",
        long_description="Python bindings for the BCSV binary CSV library with pandas integration",
        long_description_content_type="text/plain",
        packages=["pybcsv"],
        ext_modules=ext_modules,
        zip_safe=False,
        python_requires=">=3.7",
        classifiers=[
            "Development Status :: 4 - Beta",
            "Intended Audience :: Developers",
            "Programming Language :: Python :: 3",
            "Programming Language :: C++",
        ],
        install_requires=[
            "numpy>=1.19.0",
        ],
        extras_require={
            "pandas": ["pandas>=1.3.0"],
            "test": ["pytest>=6.0"],
            "dev": ["pytest>=6.0", "pandas>=1.3.0"],
        },
    )