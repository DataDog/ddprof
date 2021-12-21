#pragma once

namespace ddprof {

enum SymbolErrors {
  truncated_stack,
  unknown_dso,
  dwfl_frame,
};

}
