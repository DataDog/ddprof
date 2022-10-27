/*
  Unless explicitly stated otherwise all files in this repository are licensed
  under the Apache License Version 2.0. This product includes software
  developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
  Datadog, Inc.
*/

%{
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

#include "event_config.hpp"
#include "perf_archmap.hpp"

#define YYDEBUG 0

typedef struct yy_buffer_state * YY_BUFFER_STATE;
extern int yylex(void);
extern int yyparse(void);
extern YY_BUFFER_STATE yy_scan_string(const char * str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

EventConfMode mode_from_str(const std::string &str) {
  EventConfMode mode = EventConfMode::kDisabled;
  if (str.empty())
    return mode;

  const std::string m_str{"Mm"};
  const std::string g_str{"Gg"};
  const std::string a_str{"Aa*"};

  for (const char &c : str) {
    if (m_str.find(c) != std::string::npos)
      mode |= EventConfMode::kMetric;
    if (g_str.find(c) != std::string::npos)
      mode |= EventConfMode::kCallgraph;
    if (a_str.find(c) != std::string::npos)
      mode |= EventConfMode::kAll;
  }
  return mode;
}

void conf_finalize(EventConf *conf) {
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
  
  const char *modenames[] = {"ILLEGAL", "callgraph", "metric", "metric and callgraph"};
  printf("  type: %s\n", modenames[static_cast<unsigned>(tp->mode)]);


  if (tp->value_source  == EventConfValueSource::kSample)
    printf("  location: value\n");
  else if (tp->value_source == EventConfValueSource::kRegister)
    printf("  location: register (%d)\n", tp->register_num);
  else if (tp->value_source == EventConfValueSource::kRaw)
    printf("  location: raw event (%lu with size %d bytes)\n", tp->arg_offset, tp->arg_size);

  if (tp->arg_coeff != 0)
    printf("  coefficient: %f\n", tp->arg_coeff);

  printf("\n");

}

EventConf g_accum_event_conf = {};

void yyerror(const char *str) {
#ifdef EVENT_PARSER_MAIN
  fprintf(stderr, "err: %s\n", str);
#endif 
}

#define VAL_ERROR() \
 do { \
   yyerror("Invalid value"); \
   YYABORT;  \
 } while(0)

EventConf *EventConf_parse(const char *msg) {
  g_accum_event_conf.clear();
  int ret = -1;
  YY_BUFFER_STATE buffer = yy_scan_string(msg);
  ret = yyparse();
  yy_delete_buffer(buffer);
  return 0 == ret ? &g_accum_event_conf : NULL;
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
      conf { conf_finalize(&g_accum_event_conf); }
      | confs CONFSEP conf // Unchained, subsequent configs ignored
      ;

conf:
    | opt | conf OPTSEP conf ;

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
           g_accum_event_conf.mode |= mode_from_str(*$3);
           break;
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
       if ($3 < 0 && $$ != EventConfField::kPeriod && $$ != EventConfField::kArgCoeff) {
         VAL_ERROR();
         break;
       }
       switch($$) {
         case EventConfField::kId:
           g_accum_event_conf.id = $3;
           break;
         case EventConfField::kArgSize:
           // sz without a valid offset is ignored?
           g_accum_event_conf.arg_size= $3;
           break;
         case EventConfField::kArgCoeff:
           g_accum_event_conf.arg_coeff = 0.0 + $3;
           break;
         case EventConfField::kMode:
           g_accum_event_conf.mode = static_cast<EventConfMode>($3) & EventConfMode::kAll;
           break;

         case EventConfField::kParameter:
         case EventConfField::kRegister:
         case EventConfField::kArgOffset:
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
           if ($$ == EventConfField::kArgOffset) {
             g_accum_event_conf.value_source = EventConfValueSource::kRaw;
             g_accum_event_conf.arg_offset = $3;
           }
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
       if ($$ == EventConfField::kArgCoeff)
         g_accum_event_conf.arg_coeff = $3;
       else
         VAL_ERROR();
     }
     ;

integer: NUMBER | HEXNUMBER
