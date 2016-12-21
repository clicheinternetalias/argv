/* argv.h
 * Parse a command line.
 */
#ifndef ARGV_H_
#define ARGV_H_ 1

#include <stddef.h>

/* Internal. Do not use.
 */
#define ARGV_F_ARG_     0x01
#define ARGV_F_REQ_     0x02
#define ARGV_F_OPT_     0x04
#define ARGV_F_ATTACH_  0x08

/* Values for optarg.
 */
#define ARGV_NOARG  0
#define ARGV_ARG    ARGV_F_ARG_|ARGV_F_OPT_
#define ARGV_ARGA   ARGV_F_ARG_|ARGV_F_OPT_|ARGV_F_ATTACH_
#define ARGV_REQ    ARGV_F_ARG_|ARGV_F_REQ_
#define ARGV_REQA   ARGV_F_ARG_|ARGV_F_REQ_|ARGV_F_ATTACH_

/* The option struct.
 */
typedef struct ARGV_s {
    char   optchar;  /* short name */
    char * optname;  /* long name */
    size_t optlen;   /* long name length */
    int    optarg;   /* has an argument? */
    int    optval;   /* token value to return to caller */
} ARGV;

/* The last entry in an option array.
 */
#define ARGV_LAST { 0, NULL, 0, ARGV_NOARG, 0 }

/* The parser object.
 */
typedef struct ARGVPARSER_s {
    const char * pname;
    int          argc;
    char      ** argv;
    ARGV       * args;
    int        (*err_fp)(const char *);
    int          errs;
    int          mixed;    /* allow options and parameters to mix */
    int          sawdash;  /* saw --, rest are parameters */
    int          optidx;   /* index into argv of cur option */
    int          argidx;   /* index into cur option of arg */
    int          multi;    /* are we parsing a multi opt or multi arg? */
    int          mulopt;   /* index into argv of multi opt */
    int          mulidx;   /* index into multi opt of current opt */
    int          mularg;   /* index into argv of cur arg */
} ARGVPARSER;

extern int  argv_init(ARGVPARSER * apr, ARGV * args, int argc, char ** argv);
extern void argv_uninit(ARGVPARSER * apr);

extern int  argv_set_args(ARGVPARSER * apr, ARGV * args);
extern int  argv_set_mixed(ARGVPARSER * apr, int mixed);
extern int  argv_set_errorf(ARGVPARSER * apr, int (*cb)(const char *));
extern int  argv_error_count(ARGVPARSER * apr);
extern int  argv_set_progname(ARGVPARSER * apr, const char * progname);

extern int  argv_next(ARGVPARSER * apr, char ** arg, size_t * arglen);

#endif /* ARGV_H_ */
