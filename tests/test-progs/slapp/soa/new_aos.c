#include <stdio.h>
#include <stdlib.h>

#define NUM_STRUCTS 8192  // Enough to overflow both caches (32KB / 8B = 4096 entries per 8-way set)
#define STRIDE 1

typedef struct {
    double important_field;
    char padding[64];
} MyStruct;

int main() {
    MyStruct* arr = (MyStruct*)malloc(sizeof(MyStruct) * NUM_STRUCTS);

    for (int i = 0; i < NUM_STRUCTS * 10; ++i) {
        arr[(i * STRIDE) % NUM_STRUCTS].important_field += 1.0;
    }

    printf("Final value: %f\n", arr[0].important_field);

    free(arr);
    return 0;
}