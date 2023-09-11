/*
  Unless explicitly stated otherwise all files in this repository are licensed
  under the Apache License Version 2.0. This product includes software
  developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
  Datadog, Inc.
*/

%define api.pure
%lex-param {void *scanner} // actually yyscan_t
%parse-param {void *scanner} // actually yyscan_t

%{
#include <optional>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "event_config.hpp"
#include "event_parser.h"
#include "perf_archmap.hpp"

#define YYDEBUG 0

#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef struct yy_buffer_state * YY_BUFFER_STATE;
typedef void * yyscan_t;
#endif

extern int yylex_init(yyscan_t * scanner);
extern int yylex(YYSTYPE * lvalp, void *scanner);
extern int yyparse(yyscan_t scanner);
extern int yylex_destroy(yyscan_t scanner);
extern YY_BUFFER_STATE yy_scan_string(const char * str, yyscan_t scanner);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer, yyscan_t scanner);


EventConf g_accum_event_conf = {};
EventConf g_template_event_conf = {};
std::vector<EventConf>* g_event_configs;

std::optional<EventValueMode> mode_from_str(const std::string &str) {
  const std::string a_str{"Aa*"};
  const std::string l_str{"Ll"};
  const std::string o_str{"Oo"};
  EventValueMode mode = EventValueMode::kDisabled;
  for (const char &c : str) {
    if (o_str.find(c) != std::string::npos) {
      mode |= EventValueMode::kOccurence;
    } else if (l_str.find(c) != std::string::npos) {
        mode |= EventValueMode::kLiveUsage;
    } else if (a_str.find(c) != std::string::npos) {
      mode |= EventValueMode::kAll;
    } else {
      fprintf(stderr, "Warning, unexpected mode %c \n", c);
      return {}; // unexpected mode
    }
  }
  return mode;
}

void conf_finalize(EventConf * conf, std::vector<EventConf> * configs) {
  // Generate label if needed
  // * if both, "<eventname>:<groupname>"
  // * if only event, "<eventname>"
  // * if neither, but id, "id:<id>"
  // * if none, then this is an invalid event anyway
  if (conf->label.empty()) {
    if (!conf->eventname.empty() && !conf->groupname.empty()) {
      conf->label = conf->groupname + ":" + conf->eventname;
    } else if (!conf->eventname.empty() && conf->groupname.empty()) {
      conf->label = conf->eventname;
    } else if (conf->id) {
      conf->label = "id:" + std::to_string(conf->id);
    }
  }

  // If no cadence type is explicitly set, then period=1
  if (conf->cad_type == EventConfCadenceType::kUndefined) {
    conf->cad_type = EventConfCadenceType::kPeriod;
  }

  configs->push_back(*conf);
  g_accum_event_conf = g_template_event_conf;
}

void conf_print(const EventConf *tp) {
  if (tp->id)
    printf("  id: %lu\n", tp->id);
  else if (!tp->groupname.empty())
    printf("  tracepoint: %s:%s\n", tp->groupname.c_str(), tp->eventname.c_str());
  else
    printf("  event: %s\n", tp->eventname.c_str());

  if (!tp->label.empty())
    printf("  label: %s\n", tp->label.c_str());
  else
    printf("  label: <generated from event/groupname>\n");

  const char *modenames[] = {"ILLEGAL", "callgraph", "metric", "live callgraph", "metric and callgraph"};
  printf("  type: %s\n", modenames[static_cast<unsigned>(tp->mode)]);


  if (tp->value_source  == EventConfValueSource::kSample)
    printf("  location: value\n");
  else if (tp->value_source == EventConfValueSource::kRegister)
    printf("  location: register (%d)\n", tp->register_num);
  else if (tp->value_source == EventConfValueSource::kRaw)
    printf("  location: raw event (%lu with size %d bytes)\n", tp->raw_offset, tp->raw_size);
  printf("  stack_sample_size: %u\n", tp->stack_sample_size);
  if (tp->value_scale != 0)
    printf("  scaling factor: %f\n", tp->value_scale);

  printf("\n");

}

void yyerror(yyscan_t scanner, const char *str) {
#ifdef EVENT_PARSER_MAIN
  fprintf(stderr, "err: %s\n", str);
#endif
}

#define VAL_ERROR() \
 do { \
   yyerror(NULL, "Invalid value"); \
   YYABORT;  \
 } while(0)

 int EventConf_parse(const char *msg, const EventConf &template_conf, std::vector<EventConf>& event_configs) {
  g_template_event_conf = template_conf;
  g_accum_event_conf = g_template_event_conf;
  g_event_configs = &event_configs;
  int ret = -1;
  yyscan_t scanner = NULL;
  YY_BUFFER_STATE buffer = NULL;

  yylex_init(&scanner);
  buffer = yy_scan_string(msg, scanner);
  ret = yyparse(scanner);
  yy_delete_buffer(buffer, scanner);
  yylex_destroy(scanner);
  return ret;
}

#ifdef EVENT_PARSER_MAIN
bool g_debugout_enable = false;
int main(int c, char **v) {
  g_debugout_enable = false;
  if (c) {
    printf(">\"%s\"\n", v[1]);
    YY_BUFFER_STATE buffer = yy_scan_string(v[1]);
    g_debugout_enable = true;
    if (!yyparse())
      conf_print(&g_accum_event_conf);
    else
      fprintf(stderr, "  ERROR\n");
    yy_delete_buffer(buffer);
  } else {
    yyparse();
  }
  return 0;
}
#endif
%}

%union {
	int64_t num;
	std::string *str;
	char typ;
	double fpnum;
	EventConfField field;
};

%token EQ OPTSEP CONFSEP
%token <fpnum> FLOAT
%token <num> NUMBER HEXNUMBER
%token <str> WORD
%token <str> KEY

%type <num> integer
%type <field> conf
%type <field> opt

%destructor { delete $$; } WORD

%%

// this only allows a single config to be processed at a time
// ... and has ugly whitespace stripping
confs:
      conf { conf_finalize(&g_accum_event_conf, g_event_configs); }
      | confs CONFSEP conf { conf_finalize(&g_accum_event_conf, g_event_configs); }
      | confs CONFSEP
      | %empty
      ;

conf:
    opt | conf OPTSEP opt | conf opt;

opt:
   WORD {
     g_accum_event_conf.eventname = *$1;
     delete $1;
   }
   | KEY EQ WORD {
       switch($$) {
         case EventConfField::kEvent:
           g_accum_event_conf.eventname = *$3;
           break;
         case EventConfField::kGroup:
           g_accum_event_conf.groupname = *$3;
           break;
         case EventConfField::kLabel:
           g_accum_event_conf.label = *$3;
           break;
         case EventConfField::kMode:
           {
             auto mode = mode_from_str(*$3);
             if (!mode) {
               delete $3;
               VAL_ERROR();
             }
             // override mode if present
             g_accum_event_conf.mode = *mode;
             break;
           }
         default:
           delete $3;
           VAL_ERROR();
           break;
       }
       delete $3;
     }
     | KEY EQ WORD ':' WORD {
       if ($$ == EventConfField::kEvent || $$ == EventConfField::kGroup) {
         g_accum_event_conf.eventname = *$3;
         g_accum_event_conf.groupname = *$5;
       }
       delete $3;
       delete $5;
     }
     | KEY EQ integer {
       // FIXME TODO HACK
       // As a temporary measure, we're allowing integers to be negative ONLY
       // for the period.
       if ($3 < 0 && $$ != EventConfField::kPeriod && $$ != EventConfField::kValueScale) {
         VAL_ERROR();
         break;
       }
       switch($$) {
         case EventConfField::kId:
           g_accum_event_conf.id = $3;
           break;
         case EventConfField::kRawSize:
           // sz without a valid offset is ignored?
           if ($3 != 1 && $3 != 2 && $3 != 4 && $3 != 8) {
             VAL_ERROR();
             break;
           }
           g_accum_event_conf.raw_size= $3;
           break;
         case EventConfField::kValueScale:
           g_accum_event_conf.value_scale = 0.0 + $3;
           break;
         case EventConfField::kMode:
           g_accum_event_conf.mode = static_cast<EventValueMode>($3) & EventValueMode::kAll;
           break;

         case EventConfField::kParameter:
         case EventConfField::kRegister:
         case EventConfField::kRawOffset:
           // If the location type has already been set, then this is an error.
           if (g_accum_event_conf.value_source != EventConfValueSource::kSample) {
             VAL_ERROR();
             break;
           }
           if ($$ == EventConfField::kParameter) {
             g_accum_event_conf.value_source = EventConfValueSource::kRegister;
             unsigned int regno = param_to_perf_regno($3);
             if (regno == -1u) {
               VAL_ERROR();
               break;
             }
             g_accum_event_conf.register_num = regno;
           }
           if ($$ == EventConfField::kRegister) {
             if ($3 >= PERF_REGS_COUNT) {
               VAL_ERROR();
               break;
             }
             g_accum_event_conf.value_source = EventConfValueSource::kRegister;
             g_accum_event_conf.register_num = $3;
           }
           if ($$ == EventConfField::kRawOffset) {
             g_accum_event_conf.value_source = EventConfValueSource::kRaw;
             g_accum_event_conf.raw_offset = $3;
           }
           break;
         case EventConfField::kStackSampleSize:
            g_accum_event_conf.stack_sample_size = $3;
            break;
         case EventConfField::kPeriod:
         case EventConfField::kFrequency:
           // If the cadence has already been set, it's an error
           if (g_accum_event_conf.cad_type != EventConfCadenceType::kUndefined) {
             VAL_ERROR();
             break;
            }

           g_accum_event_conf.cadence = $3;
           if ($$ == EventConfField::kPeriod)
             g_accum_event_conf.cad_type = EventConfCadenceType::kPeriod;
           if ($$ == EventConfField::kFrequency)
             g_accum_event_conf.cad_type = EventConfCadenceType::kFrequency;
           break;
         default: VAL_ERROR(); break;
       }
     }
     | KEY EQ FLOAT {
       if ($$ == EventConfField::kValueScale)
         g_accum_event_conf.value_scale = $3;
       else
         VAL_ERROR();
     }
     ;

integer: NUMBER | HEXNUMBER
