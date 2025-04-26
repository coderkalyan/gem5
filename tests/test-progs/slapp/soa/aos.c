/*
 * Copyright (c) 2025 Kalyan Sriram <kgsriram@wisc.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_PERSONS 61440 // 1000000

// Struct of size 155 bytes (not a multiple of 64 and greater than 128)
typedef struct {
    int id;            // 4 bytes
    char name[64];     // 64 bytes
    double height;     // 8 bytes
    double weight;     // 8 bytes
    char address[70];  // 70 bytes
    unsigned char age; // 1 byte
    // Total = 4 + 64 + 8 + 8 + 70 + 1 = 155 bytes
} Person;

void generate_random_data(Person *people, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        people[i].id = rand();
        snprintf(people[i].name, sizeof(people[i].name), "Person_%zu", i);
        people[i].height = (double) (rand() % 50 + 150); // Height in cm
        people[i].weight = (double) (rand() % 50 + 50);  // Weight in kg
        snprintf(people[i].address, sizeof(people[i].address),
                 "Address_%zu_Somewhere", i);
        people[i].age = (unsigned char) (rand() % 100);
    }
}

double compute_mean_age(const Person *people, size_t count) {
    unsigned long long sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += people[i].age;
    }
    return (double) sum / count;
}

int main() {
    srand((unsigned int) time(NULL));

    Person *people = (Person *) malloc(NUM_PERSONS * sizeof(Person));
    if (people == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }

    generate_random_data(people, NUM_PERSONS);

    double mean_age;
    for (int i = 0; i < 100; i++) {
        mean_age = compute_mean_age(people, NUM_PERSONS);
    }
    printf("Mean age: %.2f\n", mean_age);

    free(people);
    return 0;
}
