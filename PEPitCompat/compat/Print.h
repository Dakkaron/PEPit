/*
 * PEPitCompat — Print class stub (Arduino Print base class)
 * 
 * Arduino's Print class provides print/println methods built on a virtual write().
 * TFT_eSPI inherits from Print. On Linux we just need the virtual write() hook.
 */

#ifndef PRINT_H
#define PRINT_H

#include <cstddef>
#include <cstdio>
#include <cstdint>

class Print {
public:
    virtual ~Print() = default;
    
    virtual size_t write(uint8_t) { return 0; }
    virtual size_t write(const char *str) {
        if (!str) return 0;
        size_t n = 0;
        while (*str) n += write((uint8_t)*str++);
        return n;
    }
    
    size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }
    size_t write(const char *buffer, size_t size) {
        return write((const uint8_t *)buffer, size);
    }
    
    size_t print(const char* string) { return write(string); }
    size_t print(char c) { return write(c); }
    size_t print(unsigned int n, int base = 10) { 
        char buf[20]; sprintf(buf, "%u", n); return write(buf); 
    }
    size_t print(int n) { 
        char buf[20]; sprintf(buf, "%d", n); return write(buf); 
    }
    size_t print(unsigned long n, int base = 10) { 
        char buf[20]; sprintf(buf, "%lu", n); return write(buf); 
    }
    size_t print(long n) { 
        char buf[20]; sprintf(buf, "%ld", n); return write(buf); 
    }
    size_t print(double n, int digits = 2) { 
        char buf[30]; snprintf(buf, sizeof(buf), "%.*f", digits, n); return write(buf); 
    }
    
    size_t println(const char* string) { size_t n = print(string); print('\n'); return n + 1; }
    size_t println(char c) { size_t n = print(c); print('\n'); return n + 1; }
    size_t println(unsigned int n, int base = 10) { size_t n2 = print(n, base); print('\n'); return n2 + 1; }
    size_t println(int n) { size_t n2 = print(n); print('\n'); return n2 + 1; }
    size_t println(unsigned long n, int base = 10) { size_t n2 = print(n, base); print('\n'); return n2 + 1; }
    size_t println(long n) { size_t n2 = print(n); print('\n'); return n2 + 1; }
    size_t println(double n, int digits = 2) { size_t n2 = print(n, digits); print('\n'); return n2 + 1; }
    size_t println(void) { print('\n'); return 1; }
};

#endif // PRINT_H
