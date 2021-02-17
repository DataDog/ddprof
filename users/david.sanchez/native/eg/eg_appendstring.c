#include "append_string.h"

int main() {
  AppendString *as = &(AppendString){0};
  as_init(as);
  as_free(as);
}
