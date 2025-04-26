/*
 * Copyright (c) 2006 The Regents of The University of Michigan
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
#define NAME_LENGTH 64
#define ADDRESS_LENGTH 70

typedef struct {
    int *id;                         // Array of IDs
    char (*name)[NAME_LENGTH];       // Array of names
    double *height;                  // Array of heights
    double *weight;                  // Array of weights
    char (*address)[ADDRESS_LENGTH]; // Array of addresses
    unsigned char *age;              // Array of ages
} PersonSoA;

void allocate_person_soa(PersonSoA *p, size_t count) {
    p->id = (int *) malloc(sizeof(int) * count);
    p->name = (char(*)[NAME_LENGTH]) malloc(sizeof(char[NAME_LENGTH]) * count);
    p->height = (double *) malloc(sizeof(double) * count);
    p->weight = (double *) malloc(sizeof(double) * count);
    p->address =
        (char(*)[ADDRESS_LENGTH]) malloc(sizeof(char[ADDRESS_LENGTH]) * count);
    p->age = (unsigned char *) malloc(sizeof(unsigned char) * count);

    if (!p->id || !p->name || !p->height || !p->weight || !p->address ||
        !p->age) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(1);
    }
}

void free_person_soa(PersonSoA *p) {
    free(p->id);
    free(p->name);
    free(p->height);
    free(p->weight);
    free(p->address);
    free(p->age);
}

void generate_random_data_soa(PersonSoA *p, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        p->id[i] = rand();
        snprintf(p->name[i], NAME_LENGTH, "Person_%zu", i);
        p->height[i] = (double) (rand() % 50 + 150); // Height in cm
        p->weight[i] = (double) (rand() % 50 + 50);  // Weight in kg
        snprintf(p->address[i], ADDRESS_LENGTH, "Address_%zu_Somewhere", i);
        p->age[i] = (unsigned char) (rand() % 100);
    }
}

double compute_mean_age_soa(const PersonSoA *p, size_t count) {
    unsigned long long sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += p->age[i];
    }
    return (double) sum / count;
}

int main() {
    srand((unsigned int) time(NULL));

    PersonSoA persons;
    allocate_person_soa(&persons, NUM_PERSONS);

    generate_random_data_soa(&persons, NUM_PERSONS);

    double mean_age = compute_mean_age_soa(&persons, NUM_PERSONS);
    printf("Mean age: %.2f\n", mean_age);

    free_person_soa(&persons);
    return 0;
}
