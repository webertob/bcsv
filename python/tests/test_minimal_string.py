#!/usr/bin/env python3
"""
Minimal test to isolate the string memory corruption issue.
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pybcsv
import tempfile

def test_minimal_string():
    """Test with progressively larger strings to find the breaking point."""
    
    print("Testing minimal string handling...")
    
    layout = pybcsv.Layout()
    layout.add_column("test_string", pybcsv.STRING)
    
    # Test progressively larger strings
    test_sizes = [100, 1000, 10000, 32768, 65527]  # 65527 + 8 = 65535 (max row size)
    
    for size in test_sizes:
        try:
            with tempfile.NamedTemporaryFile(suffix='.bcsv', delete=False) as tmp:
                filepath = tmp.name
            
            print(f"Testing string size: {size} bytes...")
            
            test_string = "x" * size
            
            # Write
            writer = pybcsv.Writer(layout)
            writer.open(filepath)
            writer.write_row([test_string])
            writer.close()
            
            # Read
            reader = pybcsv.Reader()
            reader.open(filepath)
            read_data = reader.read_all()
            reader.close()
            
            # Verify
            if len(read_data[0][0]) != size:
                print(f"❌ Size mismatch at {size}: expected {size}, got {len(read_data[0][0])}")
            else:
                print(f"✅ Size {size} successful")
                
            # Clean up
            os.unlink(filepath)
            
        except Exception as e:
            print(f"❌ Failed at size {size}: {e}")
            try:
                os.unlink(filepath)
            except:
                pass
            break

if __name__ == "__main__":
    test_minimal_string()