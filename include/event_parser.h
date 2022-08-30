#ifndef YY_event_parse_h_included
#define YY_event_parse_h_included
/*#define YY_USE_CLASS 
*/
#line 1 "/usr/share/bison++/bison.h"
/* before anything */
#ifdef c_plusplus
 #ifndef __cplusplus
  #define __cplusplus
 #endif
#endif


 #line 8 "/usr/share/bison++/bison.h"

#line 158 "event_parser.y"
typedef union {
	uint64_t num;
	char *str;
	char typ;
	double fpnum;
	EventConfField field;
} yy_event_parse_stype;
#define YY_event_parse_STYPE yy_event_parse_stype
#ifndef YY_USE_CLASS
#define YYSTYPE yy_event_parse_stype
#endif

#line 21 "/usr/share/bison++/bison.h"
 /* %{ and %header{ and %union, during decl */
#ifndef YY_event_parse_COMPATIBILITY
 #ifndef YY_USE_CLASS
  #define  YY_event_parse_COMPATIBILITY 1
 #else
  #define  YY_event_parse_COMPATIBILITY 0
 #endif
#endif

#if YY_event_parse_COMPATIBILITY != 0
/* backward compatibility */
 #ifdef YYLTYPE
  #ifndef YY_event_parse_LTYPE
   #define YY_event_parse_LTYPE YYLTYPE
/* WARNING obsolete !!! user defined YYLTYPE not reported into generated header */
/* use %define LTYPE */
  #endif
 #endif
/*#ifdef YYSTYPE*/
  #ifndef YY_event_parse_STYPE
   #define YY_event_parse_STYPE YYSTYPE
  /* WARNING obsolete !!! user defined YYSTYPE not reported into generated header */
   /* use %define STYPE */
  #endif
/*#endif*/
 #ifdef YYDEBUG
  #ifndef YY_event_parse_DEBUG
   #define  YY_event_parse_DEBUG YYDEBUG
   /* WARNING obsolete !!! user defined YYDEBUG not reported into generated header */
   /* use %define DEBUG */
  #endif
 #endif 
 /* use goto to be compatible */
 #ifndef YY_event_parse_USE_GOTO
  #define YY_event_parse_USE_GOTO 1
 #endif
#endif

/* use no goto to be clean in C++ */
#ifndef YY_event_parse_USE_GOTO
 #define YY_event_parse_USE_GOTO 0
#endif

#ifndef YY_event_parse_PURE

 #line 65 "/usr/share/bison++/bison.h"

#line 65 "/usr/share/bison++/bison.h"
/* YY_event_parse_PURE */
#endif


 #line 68 "/usr/share/bison++/bison.h"

#line 68 "/usr/share/bison++/bison.h"
/* prefix */

#ifndef YY_event_parse_DEBUG

 #line 71 "/usr/share/bison++/bison.h"
#define YY_event_parse_DEBUG 1

#line 71 "/usr/share/bison++/bison.h"
/* YY_event_parse_DEBUG */
#endif

#ifndef YY_event_parse_LSP_NEEDED

 #line 75 "/usr/share/bison++/bison.h"

#line 75 "/usr/share/bison++/bison.h"
 /* YY_event_parse_LSP_NEEDED*/
#endif

/* DEFAULT LTYPE*/
#ifdef YY_event_parse_LSP_NEEDED
 #ifndef YY_event_parse_LTYPE
  #ifndef BISON_YYLTYPE_ISDECLARED
   #define BISON_YYLTYPE_ISDECLARED
typedef
  struct yyltype
    {
      int timestamp;
      int first_line;
      int first_column;
      int last_line;
      int last_column;
      char *text;
   }
  yyltype;
  #endif

  #define YY_event_parse_LTYPE yyltype
 #endif
#endif

/* DEFAULT STYPE*/
#ifndef YY_event_parse_STYPE
 #define YY_event_parse_STYPE int
#endif

/* DEFAULT MISCELANEOUS */
#ifndef YY_event_parse_PARSE
 #define YY_event_parse_PARSE yyparse
#endif

#ifndef YY_event_parse_LEX
 #define YY_event_parse_LEX yylex
#endif

#ifndef YY_event_parse_LVAL
 #define YY_event_parse_LVAL yylval
#endif

#ifndef YY_event_parse_LLOC
 #define YY_event_parse_LLOC yylloc
#endif

#ifndef YY_event_parse_CHAR
 #define YY_event_parse_CHAR yychar
#endif

#ifndef YY_event_parse_NERRS
 #define YY_event_parse_NERRS yynerrs
#endif

#ifndef YY_event_parse_DEBUG_FLAG
 #define YY_event_parse_DEBUG_FLAG yydebug
#endif

#ifndef YY_event_parse_ERROR
 #define YY_event_parse_ERROR yyerror
#endif

#ifndef YY_event_parse_PARSE_PARAM
 #ifndef __STDC__
  #ifndef __cplusplus
   #ifndef YY_USE_CLASS
    #define YY_event_parse_PARSE_PARAM
    #ifndef YY_event_parse_PARSE_PARAM_DEF
     #define YY_event_parse_PARSE_PARAM_DEF
    #endif
   #endif
  #endif
 #endif
 #ifndef YY_event_parse_PARSE_PARAM
  #define YY_event_parse_PARSE_PARAM void
 #endif
#endif

/* TOKEN C */
#ifndef YY_USE_CLASS

 #ifndef YY_event_parse_PURE
  #ifndef yylval
   extern YY_event_parse_STYPE YY_event_parse_LVAL;
  #else
   #if yylval != YY_event_parse_LVAL
    extern YY_event_parse_STYPE YY_event_parse_LVAL;
   #else
    #warning "Namespace conflict, disabling some functionality (bison++ only)"
   #endif
  #endif
 #endif


 #line 169 "/usr/share/bison++/bison.h"
#define	EQ	258
#define	OPTSEP	259
#define	CONFSEP	260
#define	FLOAT	261
#define	NUMBER	262
#define	HEXNUMBER	263
#define	WORD	264
#define	KEY	265


#line 169 "/usr/share/bison++/bison.h"
 /* #defines token */
/* after #define tokens, before const tokens S5*/
#else
 #ifndef YY_event_parse_CLASS
  #define YY_event_parse_CLASS event_parse
 #endif

 #ifndef YY_event_parse_INHERIT
  #define YY_event_parse_INHERIT
 #endif

 #ifndef YY_event_parse_MEMBERS
  #define YY_event_parse_MEMBERS 
 #endif

 #ifndef YY_event_parse_LEX_BODY
  #define YY_event_parse_LEX_BODY  
 #endif

 #ifndef YY_event_parse_ERROR_BODY
  #define YY_event_parse_ERROR_BODY  
 #endif

 #ifndef YY_event_parse_CONSTRUCTOR_PARAM
  #define YY_event_parse_CONSTRUCTOR_PARAM
 #endif
 /* choose between enum and const */
 #ifndef YY_event_parse_USE_CONST_TOKEN
  #define YY_event_parse_USE_CONST_TOKEN 0
  /* yes enum is more compatible with flex,  */
  /* so by default we use it */ 
 #endif
 #if YY_event_parse_USE_CONST_TOKEN != 0
  #ifndef YY_event_parse_ENUM_TOKEN
   #define YY_event_parse_ENUM_TOKEN yy_event_parse_enum_token
  #endif
 #endif

class YY_event_parse_CLASS YY_event_parse_INHERIT
{
public: 
 #if YY_event_parse_USE_CONST_TOKEN != 0
  /* static const int token ... */
  
 #line 212 "/usr/share/bison++/bison.h"
static const int EQ;
static const int OPTSEP;
static const int CONFSEP;
static const int FLOAT;
static const int NUMBER;
static const int HEXNUMBER;
static const int WORD;
static const int KEY;


#line 212 "/usr/share/bison++/bison.h"
 /* decl const */
 #else
  enum YY_event_parse_ENUM_TOKEN { YY_event_parse_NULL_TOKEN=0
  
 #line 215 "/usr/share/bison++/bison.h"
	,EQ=258
	,OPTSEP=259
	,CONFSEP=260
	,FLOAT=261
	,NUMBER=262
	,HEXNUMBER=263
	,WORD=264
	,KEY=265


#line 215 "/usr/share/bison++/bison.h"
 /* enum token */
     }; /* end of enum declaration */
 #endif
public:
 int YY_event_parse_PARSE(YY_event_parse_PARSE_PARAM);
 virtual void YY_event_parse_ERROR(char *msg) YY_event_parse_ERROR_BODY;
 #ifdef YY_event_parse_PURE
  #ifdef YY_event_parse_LSP_NEEDED
   virtual int  YY_event_parse_LEX(YY_event_parse_STYPE *YY_event_parse_LVAL,YY_event_parse_LTYPE *YY_event_parse_LLOC) YY_event_parse_LEX_BODY;
  #else
   virtual int  YY_event_parse_LEX(YY_event_parse_STYPE *YY_event_parse_LVAL) YY_event_parse_LEX_BODY;
  #endif
 #else
  virtual int YY_event_parse_LEX() YY_event_parse_LEX_BODY;
  YY_event_parse_STYPE YY_event_parse_LVAL;
  #ifdef YY_event_parse_LSP_NEEDED
   YY_event_parse_LTYPE YY_event_parse_LLOC;
  #endif
  int YY_event_parse_NERRS;
  int YY_event_parse_CHAR;
 #endif
 #if YY_event_parse_DEBUG != 0
  public:
   int YY_event_parse_DEBUG_FLAG;	/*  nonzero means print parse trace	*/
 #endif
public:
 YY_event_parse_CLASS(YY_event_parse_CONSTRUCTOR_PARAM);
public:
 YY_event_parse_MEMBERS 
};
/* other declare folow */
#endif


#if YY_event_parse_COMPATIBILITY != 0
 /* backward compatibility */
 /* Removed due to bison problems
 /#ifndef YYSTYPE
 / #define YYSTYPE YY_event_parse_STYPE
 /#endif*/

 #ifndef YYLTYPE
  #define YYLTYPE YY_event_parse_LTYPE
 #endif
 #ifndef YYDEBUG
  #ifdef YY_event_parse_DEBUG 
   #define YYDEBUG YY_event_parse_DEBUG
  #endif
 #endif

#endif
/* END */

 #line 267 "/usr/share/bison++/bison.h"
#endif
