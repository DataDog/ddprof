with section("format"):

  # How wide to allow formatted cmake files
  line_width = 100
  autosort = True

with section("parse"):
  additional_commands = {
    "add_unit_test": {
      "pargs": '*',
      "flags": ["NO_DETECT_LEAK"],
      "kwargs": {
        "LIBRARIES": '*',
        "DEFINITIONS": '*',
        "DEPENDS": '*',
      },
    },
    "add_exe": {  
      "pargs": '*',
      "kwargs": {
        "LIBRARIES": '*',
        "DEFINITIONS": '*',
      },
    },      
    "add_benchmark": {  
      "pargs": '*',
      "kwargs": {
        "LIBRARIES": '*',
        "DEFINITIONS": '*',
      },
    }
  }
