// Quick compilation test for new bitset structure
#include "include/bcsv/bitset.h"
#include <iostream>

int main() {
    // Test fixed-size bitset
    bcsv::bitset<64> fixed;
    fixed.set(5);
    fixed.set(10);
    
    std::cout << "Fixed bitset (64 bits):\n";
    std::cout << "  Size: " << fixed.size() << "\n";
    std::cout << "  Count: " << fixed.count() << "\n";
    std::cout << "  Bit 5: " << fixed[5] << "\n";
    std::cout << "  Bit 10: " << fixed[10] << "\n";
    
    // Test dynamic-size bitset
    bcsv::bitset<> dynamic(128);
    dynamic.set(50);
    dynamic.set(100);
    
    std::cout << "\nDynamic bitset (128 bits):\n";
    std::cout << "  Size: " << dynamic.size() << "\n";
    std::cout << "  Count: " << dynamic.count() << "\n";
    std::cout << "  Bit 50: " << dynamic[50] << "\n";
    std::cout << "  Bit 100: " << dynamic[100] << "\n";
    
    // Test resize
    dynamic.resize(256, false);
    std::cout << "\nAfter resize to 256:\n";
    std::cout << "  Size: " << dynamic.size() << "\n";
    std::cout << "  Count: " << dynamic.count() << "\n";
    
    std::cout << "\nCompilation successful!\n";
    return 0;
}
