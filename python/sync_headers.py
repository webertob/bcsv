#!/usr/bin/env python3

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

"""
Script to synchronize BCSV and dependency headers and sources
from the main project into the Python package.
Ensures both header (.h) and source (.c/.cpp) files are copied
as needed for compilation.
"""

import os
import shutil
import argparse
from pathlib import Path


def sync_headers(project_root=None, force=False, verbose=False):
    """
    Synchronize BCSV headers from the main project to the Python package.
    
    Args:
        project_root: Path to the main BCSV project root (auto-detected if None)
        force: Force overwrite existing headers even if they're newer
        verbose: Print detailed information about what's being done
    """
    # Get paths
    python_dir = Path(__file__).parent.absolute()
    
    if project_root is None:
        project_root = python_dir.parent
    else:
        project_root = Path(project_root)
    
    source_include_dir = project_root / "include"
    target_include_dir = python_dir / "include"
    
    if verbose:
        print(f"Source include directory: {source_include_dir}")
        print(f"Target include directory: {target_include_dir}")
    
    # Check if source exists
    if not source_include_dir.exists():
        print(f"Error: Source include directory not found: {source_include_dir}")
        return False
    
    # Create target directory if it doesn't exist
    target_include_dir.mkdir(exist_ok=True)
    
    # Sync BCSV headers
    source_bcsv_dir = source_include_dir / "bcsv"
    target_bcsv_dir = target_include_dir / "bcsv"
    
    if source_bcsv_dir.exists():
        if verbose:
            print("Syncing BCSV headers...")
        
        if target_bcsv_dir.exists() and not force:
            # Check if we need to update
            source_newer = any(
                not (target_bcsv_dir / f.name).exists() or 
                f.stat().st_mtime > (target_bcsv_dir / f.name).stat().st_mtime
                for f in source_bcsv_dir.glob("*")
                if f.is_file()
            )
            
            if not source_newer:
                if verbose:
                    print("BCSV headers are up to date")
            else:
                if verbose:
                    print("Updating BCSV headers...")
                shutil.copytree(source_bcsv_dir, target_bcsv_dir, dirs_exist_ok=True)
        else:
            if verbose:
                print("Copying BCSV headers...")
            shutil.copytree(source_bcsv_dir, target_bcsv_dir, dirs_exist_ok=True)
    
    # Sync LZ4 headers AND sources (needed for compilation)
    source_lz4_dir = source_include_dir / "lz4-1.10.0"
    target_lz4_dir = target_include_dir / "lz4-1.10.0"
    if source_lz4_dir.exists():
        if verbose:
            print("Syncing LZ4 headers and sources...")
        target_lz4_dir.mkdir(exist_ok=True)
        # Copy all .h and .c files
        for item in source_lz4_dir.glob("*"):
            if item.is_file() and item.suffix in (".h", ".c"):
                target_file = target_lz4_dir / item.name
                if not target_file.exists() or force or item.stat().st_mtime > target_file.stat().st_mtime:
                    if verbose: print(f"  Copying {item.name}")
                    shutil.copy2(item, target_file)
    
    # Sync boost headers if they exist
    source_boost_dir = source_include_dir / "boost-1.89.0"
    target_boost_dir = target_include_dir / "boost-1.89.0"
    
    if source_boost_dir.exists():
        if verbose:
            print("Syncing Boost headers...")
        
        if not target_boost_dir.exists() or force:
            shutil.copytree(source_boost_dir, target_boost_dir, dirs_exist_ok=True)
        elif verbose:
            print("Boost headers already exist (use --force to update)")
    
    if verbose:
        print("Header synchronization complete!")
    
    return True


def main():
    parser = argparse.ArgumentParser(description="Sync BCSV headers to Python package")
    parser.add_argument("--project-root", help="Path to the main BCSV project root")
    parser.add_argument("--force", action="store_true", help="Force overwrite existing headers")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    
    args = parser.parse_args()
    
    success = sync_headers(
        project_root=args.project_root,
        force=args.force,
        verbose=args.verbose
    )
    
    if not success:
        exit(1)


if __name__ == "__main__":
    main()