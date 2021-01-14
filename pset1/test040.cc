#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>
// Diabolical wild free #1.

int main() {
    char* a = (char*) malloc(200);
    char* b = (char*) malloc(50);
    char* c = (char*) malloc(200);
    char* p = (char*) malloc(3000);
    (void) a, (void) c;
    memcpy(p, b - 200, 450);
    free(p + 200);
    m61_print_statistics();
}

//! MEMORY BUG???: invalid free of pointer ???, not allocated
//! ???
