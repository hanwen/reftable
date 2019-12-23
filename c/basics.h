// Copyright 2019 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BASICS_H
#define BASICS_H

#include <stdint.h>
#include "reftable.h"

#define true 1
#define false 0
#define ARRAYSIZE(a) sizeof(a) / sizeof(a[0])

void put_u24(byte *out, uint32_t i);
uint32_t get_u24(byte *in);

uint64_t get_u64(byte *in);
void put_u64(byte *out, uint64_t i);

void put_u32(byte *out, uint32_t i);
uint32_t get_u32(byte *in);

void put_u16(byte *out, uint16_t i);
uint16_t get_u16(byte *in);
int binsearch(int sz, int (*f)(int k, void *args), void *args);

void free_names(char **a);
void parse_names(char *buf, int size, char ***namesp);
int names_equal(char **a, char **b);

#endif
