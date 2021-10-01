#include <string>
#include <vector>

namespace ddprof {

using Tag = std::pair<std::string, std::string>;
using Tags = std::vector<Tag>;

void split(const char *str, Tags &tags, char c = ',');

} // namespace ddprof

typedef struct UserTags {
  UserTags(const char *tag_str, int nproc);
  ddprof::Tags _tags;
} UserTags;
