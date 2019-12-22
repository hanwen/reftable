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

#include <unistd.h>
#include <string.h>

#include "stack.h"

#include "api.h"
#include "basics.h"
#include "record.h"
#include "test_framework.h"

void test_read_file(void) {
  char fn[256] = "/tmp/stack.XXXXXX";
  int fd = mkstemp(fn);
  assert(fd > 0);

  char out[1024] = "line1\n\nline2\nline3";
  
  int n = write(fd, out, strlen(out));
  assert(n == strlen(out));
  int err = close(fd);
  assert(err >= 0);

  char **names = NULL;
  err = read_lines(fn, &names);
  assert_err(err);

  char *want[] = {"line1", "line2", "line3"};
  for (int i = 0; names[i] != NULL; i++) {
    assert(0 == strcmp(want[i], names[i]));
  }
  free_names(names);
  remove(fn);
}

void test_parse_names(void) {
  char buf[] = "line\n";
  char **names = NULL;
  parse_names(buf, strlen(buf), &names);

  assert(NULL != names[0]);
  assert(0 == strcmp(names[0], "line"));
  assert(NULL == names[1]);
  free_names(names);
}

void test_names_equal(void) {
  char *a[]  = { "a", "b", "c", NULL };
  char *b[]  = { "a", "b", "d", NULL };
  char *c[]  = { "a", "b",  NULL };

  assert(names_equal(a, a));
  assert(!names_equal(a, b));
  assert(!names_equal(a, c));
}

int write_test_ref(struct writer *wr, void*arg) {
  struct ref_record *ref = arg;

  writer_set_limits(wr, ref->update_index, ref->update_index);
  int err = writer_add_ref(wr, ref);

  return err;
}

void test_stack_add(void) {
  char dir[256] = "/tmp/stack.XXXXXX";
  assert(mkdtemp(dir));
  printf("%s\n", dir);
  char fn[256] = "";
  strcat(fn, dir);
  strcat(fn, "/");
  strcat(fn, "refs");

  struct write_options cfg = {  };
  struct stack *st = NULL;
  int err = new_stack(&st, dir, fn, cfg);
  assert_err(err);

  struct ref_record refs[4] = {};
  int N = ARRAYSIZE(refs);
  for (int i = 0; i < N; i++) {
    char buf[256];
    sprintf(buf, "branch%02d", i);
    refs[i].ref_name = strdup(buf);
    refs[i].value = malloc(HASH_SIZE);
    refs[i].update_index = i + 1;
    set_test_hash(refs[i].value , i);
  }
  
  for (int i = 0; i < N; i++) {
    int err = stack_add(st, &write_test_ref, &refs[i]);
    assert_err(err);
  }

  // XXX compact here.

  struct merged_table *mt = stack_merged(st);
  for (int i = 0; i < N; i++) {
    struct iterator it = {};
    int err = merged_table_seek_ref(mt, &it, refs[i].ref_name);
    assert_err(err);

    struct ref_record dest = {};
    err = iterator_next_ref(it, &dest);
    assert_err(err);
    assert(ref_record_equal(&dest, refs + i));
    iterator_destroy(&it);
    ref_record_clear(&dest);
  }

  err = stack_compact_all(st);
  assert_err(err);

  // cleanup
  stack_destroy(st);
  for (int i = 0; i < N; i++) {
    free(refs[i].value);
    free(refs[i].ref_name);
  }

}

int main() {
  add_test_case("test_parse_names", &test_parse_names);
  add_test_case("test_read_file", &test_read_file);
  add_test_case("test_names_equal", &test_names_equal);
  add_test_case("test_stack_add", &test_stack_add);
  test_main();
}
