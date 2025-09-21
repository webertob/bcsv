#!/usr/bin/env python3

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

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

class CustomBuildExt(build_ext if PYBIND11_AVAILABLE else object):
    """Custom build extension to handle C and C++ files separately."""
    
    def build_extensions(self):
        # Set specific compiler flags for different file types
        for ext in self.extensions:
            # Get the default compiler flags
            original_compile_args = ext.extra_compile_args[:]
            
            # Create a new list of args without C++ specific flags for C files
            c_only_args = [arg for arg in original_compile_args if not arg.startswith('-std=c++')]
            
            # Override compile method to apply different flags per file
            original_compile = self.compiler.compile
            
            def custom_compile(sources, output_dir=None, macros=None, include_dirs=None, 
                             debug=0, extra_preargs=None, extra_postargs=None, depends=None):
                
                # Separate C and C++ files
                c_sources = [s for s in sources if s.endswith('.c')]
                cpp_sources = [s for s in sources if s.endswith('.cpp') or s.endswith('.cxx') or s.endswith('.cc')]
                
                objects = []
                
                # Compile C files with C-only flags
                if c_sources:
                    objects.extend(original_compile(
                        c_sources, output_dir, macros, include_dirs, debug,
                        extra_preargs, c_only_args, depends
                    ))
                
                # Compile C++ files with full flags
                if cpp_sources:
                    objects.extend(original_compile(
                        cpp_sources, output_dir, macros, include_dirs, debug,
                        extra_preargs, extra_postargs, depends
                    ))
                
                return objects
            
            # Temporarily replace the compile method
            self.compiler.compile = custom_compile
            
        # Call the parent build method
        super().build_extensions()

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
    cmdclass = {"build_ext": CustomBuildExt}
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
    cmdclass = {"build_ext": CustomBuildExt}

if __name__ == "__main__":
    setup(
        name="pybcsv",
        version=get_version(),
        author="Tobias Weber",
        author_email="weber.tobias.md@gmail.com",
        description="Python bindings for the high-performance BCSV (Binary CSV) library",
        long_description="Python bindings for the BCSV binary CSV library with pandas integration",
        long_description_content_type="text/plain",
        packages=["pybcsv"],
        ext_modules=ext_modules,
        cmdclass=cmdclass,
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