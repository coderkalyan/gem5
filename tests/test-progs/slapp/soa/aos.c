#include <stdio.h>

#define NUM_PERSONS 61440

// Struct size: 160 bytes, all fields naturally aligned
typedef struct {
  int id;        // 4 bytes
  char _pad1[4]; // Padding to align next field

  char name[64]; // 64 bytes

  double height; // 8 bytes
  double weight; // 8 bytes

  char address[70]; // 70 bytes
  char _pad2[6];    // Padding to align age

  unsigned char age; // 1 byte
  char _pad3[7];     // Padding to align struct size to 160
} Person;

// Simple PRNG: Linear Congruential Generator
static unsigned int prng_state = 1;

unsigned int prng_next(void) {
  prng_state = prng_state * 1664525u + 1013904223u;
  return prng_state;
}

// Integer to ASCII string (no formatting), null-terminated
void uint_to_str(unsigned int value, char *buffer, int max_len) {
  int i = max_len - 2;
  buffer[max_len - 1] = '\0';
  if (value == 0) {
    buffer[i--] = '0';
  } else {
    while (value > 0 && i >= 0) {
      buffer[i--] = '0' + (value % 10);
      value /= 10;
    }
  }
  // Shift result to start of buffer
  int j = 0;
  ++i;
  while (i < max_len - 1) {
    buffer[j++] = buffer[i++];
  }
  buffer[j] = '\0';
}

void generate_random_data(Person *people, unsigned int count) {
  for (unsigned int i = 0; i < count; ++i) {
    people[i].id = (int)prng_next();

    // name = "Person_" + i
    char num_buf[20];
    uint_to_str(i, num_buf, sizeof(num_buf));
    char *prefix = "Person_";
    int j = 0, k = 0;
    while (prefix[j] != '\0' && j < 63) {
      people[i].name[j] = prefix[j];
      ++j;
    }
    while (num_buf[k] != '\0' && j < 63) {
      people[i].name[j++] = num_buf[k++];
    }
    people[i].name[j] = '\0';

    people[i].height = (double)(prng_next() % 50 + 150);
    people[i].weight = (double)(prng_next() % 50 + 50);

    // address = "Address_" + i + "_Somewhere"
    prefix = "Address_";
    j = 0;
    k = 0;
    while (prefix[j] != '\0' && j < 69) {
      people[i].address[j] = prefix[j];
      ++j;
    }
    uint_to_str(i, num_buf, sizeof(num_buf));
    while (num_buf[k] != '\0' && j < 69) {
      people[i].address[j++] = num_buf[k++];
    }
    prefix = "_Somewhere";
    k = 0;
    while (prefix[k] != '\0' && j < 69) {
      people[i].address[j++] = prefix[k++];
    }
    people[i].address[j] = '\0';

    people[i].age = (unsigned char)(prng_next() % 100);
  }
}

double compute_mean_age(const Person *people, unsigned int count) {
  unsigned long long sum = 0;
  for (unsigned int i = 0; i < count; ++i) {
    sum += people[i].age;
  }
  return (double)sum / count;
}

int main(void) {
  // Static allocation to avoid malloc
  static Person people[NUM_PERSONS];

  generate_random_data(people, NUM_PERSONS);

  double mean_age = 0.0;
  for (int i = 0; i < 100; ++i) {
    mean_age = compute_mean_age(people, NUM_PERSONS);
  }

  printf("Mean age: %.2f\n", mean_age);

  return 0;
}
