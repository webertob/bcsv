# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

"""Version information for pybcsv."""

# Version is now automatically generated from Git tags via setuptools-scm
# This file is kept for backward compatibility
try:
    from pybcsv._version import version as __version__
except ImportError:
    # Fallback version when not installed via setuptools
    __version__ = "0.0.0.dev0"
__author__ = "Tobias Weber"
__email__ = "weber.tobias.md@gmail.com"
__license__ = "MIT"