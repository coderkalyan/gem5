#include <stdint.h>
#include <stdlib.h>

#define SIZE 4

int main() {
  uint32_t buffer[SIZE];

  // Write to buffer
  for (int i = 0; i < SIZE; i++) {
    buffer[i] = i;
  }

  // Read from buffer
  uint32_t sum = 0;
  for (int i = 0; i < SIZE; i++) {
    sum += buffer[i];
  }

  return (int)sum;
}
