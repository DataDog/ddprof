#include <ddprof/pprof.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <x86intrin.h>
#include <zlib.h>

int main() {
  DProf *dp = &(DProf){0};
  dp->table_type = 1; // use string_table.h
  pprof_Init(dp, (const char **)&(const char *[]){"samples", "cpu"},
             (const char **)&(const char *[]){"count", "nanoseconds"}, 2);

  // Add some fake Mappings
  uint64_t id_map0 = pprof_mapAdd(dp, 2000, 2900, 0, "hello.so", "aaaa");
  uint64_t id_map1 = pprof_mapAdd(dp, 1000, 1900, 1000, "yikes.so", "aaaa");

  printf("Map IDs: %lu, %lu\n", id_map0, id_map1);
  // Add some fake functions
  uint64_t id_fun0 =
      pprof_funAdd(dp, "recursion_fun", "recursion_fun", "Hello.c", 0);
  uint64_t id_fun1 = pprof_funAdd(dp, "work_fun", "work_fun", "t3st.c", 0);
  printf("Fun IDs: %lu, %lu\n", id_fun0, id_fun1);

  // Add some fake locations
  uint64_t id_loc0 =
      pprof_locAdd(dp, id_map0, 50, (uint64_t[]){id_fun0}, (int64_t[]){0}, 1);
  uint64_t id_loc1 =
      pprof_locAdd(dp, id_map1, 250, (uint64_t[]){id_fun1}, (int64_t[]){0}, 1);
  printf("Loc IDs: %lu, %lu\n", id_loc0, id_loc1);

  // Add some fake samples
  pprof_sampleAdd(dp, (int64_t[]){1, 300}, 2, (uint64_t[]){id_loc0}, 1);
  pprof_sampleAdd(dp, (int64_t[]){1, 300}, 2, (uint64_t[]){id_loc0, id_loc1},
                  2);
  pprof_sampleAdd(dp, (int64_t[]){1, 200}, 2, (uint64_t[]){id_loc0, id_loc1},
                  2);
  pprof_sampleAdd(dp, (int64_t[]){1, 100}, 2, (uint64_t[]){id_loc0, id_loc1},
                  2);

  // Serialize and ship
  dp->pprof.string_table = dp->string_table(dp->string_table_data);
  dp->pprof.n_string_table = dp->string_table_size(dp->string_table_data);
  size_t len = perftools__profiles__profile__get_packed_size(&dp->pprof);
  void *buf = calloc(1, len);
  perftools__profiles__profile__pack(&dp->pprof, buf);
  int fd = open("./test.pb", O_WRONLY | O_CREAT, 0777);
  ftruncate(fd, 0);
  write(fd, buf, len);
  close(fd);
  GZip("./test.pb.gz", buf, len);
  free(buf);
  pprof_Free(dp);
  return 0;
}
