#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>

int main() {
    // testing sz of 0
    void *ptr = malloc(10);
    void *new_ptr = realloc(ptr, 0);
    assert(new_ptr == nullptr);

    // testing copying
    ptr = malloc(10);
    ((char *) ptr)[0] = '9';
    new_ptr = realloc(ptr, 20);
    assert(((char *) ptr)[0] == '9');

    // testing resize
    free(new_ptr);
    new_ptr = realloc(nullptr, 10);
    ((char *) new_ptr)[9] = '9';
}
