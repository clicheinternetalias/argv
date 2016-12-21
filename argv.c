/* argv.c
 * Parse a command line.
 * syntax:
 *   -d      -d arg      -d=      -d=arg
 *   -long   -long arg   -long=   -long=arg
 *   --long  --long arg  --long=  --long=arg
 *   -lo     -lo arg     -lo=     -lo=arg
 *   --lo    --lo arg    --lo=    --lo=arg
 *   -darg
 *   -abc a_arg b_arg c_arg
 *   mixed params
 *   nonmixed params
 *   -- nonmixed params
 */
#include "argv.h"
#include <stdio.h>
#include <string.h>

#define TEST 1

#define argv_has_arg(x)      ((x) != 0)
#define argv_is_optional(x)  ((x) & ARGV_F_OPT_)
#define argv_is_required(x)  ((x) & ARGV_F_REQ_)
#define argv_is_attached(x)  ((x) & ARGV_F_ATTACH_)

#define ARGV_TOK_END   0
#define ARGV_TOK_ARG   1
#define ARGV_TOK_DASH  2
#define ARGV_TOK_SOPT  3
#define ARGV_TOK_LOPT  4

/* ********************************************************************** */
/* Error Messages */
/* ********************************************************************** */

static int
argv_error_cb(const char * s)
{
    return fprintf(stderr, "%s\n", s);
}

static void
argv_error(ARGVPARSER * apr, const char * fmt,
           char * tok, size_t toklen, int toktype)
{
    char buf[256];
    apr->errs++;

    if (!apr->err_fp) return;

    snprintf(buf, sizeof(buf), fmt, apr->pname,
             (int)toklen + (1 + (toktype == ARGV_TOK_LOPT)),
                     tok - (1 + (toktype == ARGV_TOK_LOPT)));
    buf[sizeof(buf) - 1] = '\0';
    apr->err_fp(buf);
}

/* ********************************************************************** */
/* Tokenize */
/* ********************************************************************** */

static int
argv_tok_next(ARGVPARSER * apr, char ** tok, size_t * toklen)
{
    char * cur;
    int lopt = 0;

    /* If !tok, we're just peeking at the next token;
     * don't modify apr.
     */

    /* Have we reached the end of a multi option?
     * (This doesn't affect what's peeked.)
     */
    if (apr->multi == 1 && !*(apr->argv[apr->mulopt] + apr->mulidx)) {
        apr->multi = 0;
        apr->optidx = apr->mularg - 1;
    }

    /* Are we parsing a multi option?
     */
    if (apr->multi) {
        if (apr->multi == 1) { /* return an opt */
            if (tok) {
                *tok = (apr->argv[apr->mulopt] + apr->mulidx);
                *toklen = 1;
                apr->mulidx++;
            }
            return ARGV_TOK_SOPT;

        } else if (apr->mularg < apr->argc) { /* return an arg */
            if (tok) {
                *tok = apr->argv[apr->mularg];
                *toklen = strlen(*tok);
                apr->mularg++;
                apr->multi = 1;
            }
            return ARGV_TOK_ARG;

        } else {
            return ARGV_TOK_END;
        }
    }

    /* Did we already find an arg?
     * -darg -d=arg -name=arg --name=arg
     */
    if (apr->argidx) {
        if (tok) {
            *tok = (apr->argv[apr->optidx] + apr->argidx);
            *toklen = strlen(*tok);
            apr->argidx = 0;
        }
        return ARGV_TOK_ARG;
    }

    /* End of command line
     */
    if (apr->optidx + 1 >= apr->argc) {
        if (tok) {
            *tok = NULL;
            *toklen = 0;
        }
        return ARGV_TOK_END;
    }

    cur = apr->argv[apr->optidx + 1];

    if (tok) apr->optidx++;

    /* Check for '\0', 'arg', '-\0'
     */
    if (*cur != '-' || apr->sawdash || *++cur == '\0') {
        if (tok) {
            *tok = apr->argv[apr->optidx];
            *toklen = strlen(*tok);
        }
        return ARGV_TOK_ARG;
    }

    /* Check for '--', '--\0'
     */
    if (*cur == '-') {
        lopt = 1;
        if (!*++cur) {
            if (tok) {
                *tok = apr->argv[apr->optidx];
                *toklen = 2;
                apr->sawdash = 1;
            }
            return ARGV_TOK_DASH;
        }
    }

    /* Check for 'opt\0', 'opt='
     */
    if (tok) {
        char * endp;
        for (endp = cur; *endp && *endp != '='; ++endp) ;
        *tok = cur;
        *toklen = (size_t)(endp - cur);
        if (*endp == '=') {
            apr->argidx = (size_t)(endp - apr->argv[apr->optidx]) + 1;
        }
    }
    return lopt ? ARGV_TOK_LOPT : ARGV_TOK_SOPT;
}

/* ********************************************************************** */
/* Identify an Option */
/* ********************************************************************** */

static ARGV *
argv_tok_identify(ARGVPARSER * apr,
                  int toktype, char * tok, size_t * toklen)
{
    size_t i;
    ARGV * o;
    int ambig = 0;

    /* Short option
     * We may be in the middle of a multi opt.
     */
    if (*toklen == 1 && toktype != ARGV_TOK_LOPT) {

        for (i = 0; apr->args[i].optval; ++i) {
            o = &(apr->args[i]);
            if (o->optchar == *tok) {
                if (apr->multi && argv_is_required(o->optarg))
                    apr->multi = 2;
                return o;
            }
        }
    }

    /* If we are in the middle of a multi opt, we don't have to
     * worry about matching a long name in the middle of it: we check
     * for all valid options before beginning, and the multi opt code
     * always returns toklen == 1 and never returns ARGV_TOK_LOPT.
     */

    /* Long option, exact match
     */
    for (i = 0; apr->args[i].optval; ++i) {
        o = &(apr->args[i]);
        if (o->optlen == *toklen && !strncmp(o->optname, tok, *toklen))
            return o;
    }

    /* Long option, unique prefix
     * Only report ambiguity after checking for multi opt.
     */
    {
        ARGV * found = NULL;
        for (i = 0; apr->args[i].optval; ++i) {
            o = &(apr->args[i]);
            if (o->optname && !strncmp(o->optname, tok, *toklen)) {
                if (found) { ambig = 1; break; }
                found = o;
            }
        }
        if (!ambig && found) return found;
    }

    /* Short option, attached argument
     */
    if (*toklen > 1 && toktype != ARGV_TOK_LOPT) {

        for (i = 0; apr->args[i].optval; ++i) {
            o = &(apr->args[i]);
            if (argv_is_attached(o->optarg) && o->optchar == *tok) {
                *toklen = 1;
                apr->argidx = 2;
                return o;
            }
        }
    }

    /* Multiple short options
     * Each character must be a valid option with
     * either no arg or a non-attached required arg.
     * We also cannot allow "-abc=carg".
     * To keep later parsing simple, we check for missing args here.
     */
    if (*toklen > 1 && toktype != ARGV_TOK_LOPT && !apr->argidx) {
        ARGV * first = NULL;
        char * cur;
        int argcnt = 0;
        for (cur = tok; *cur; ++cur) {
            for (i = 0; apr->args[i].optval; ++i) {
                o = &(apr->args[i]);

                if (!argv_is_optional(o->optarg) &&
                    !argv_is_attached(o->optarg) &&
                    o->optchar == *cur) {

                    if (!first) first = o;
                    if (!argv_has_arg(o->optarg)) break;

                    if (argv_is_required(o->optarg)) {
                        ++argcnt;
                        if (apr->optidx + argcnt < apr->argc &&
                            *(apr->argv[apr->optidx + argcnt]) != '-')
                            break;
                    }
                }
            }
            if (!apr->args[i].optval) break;
        }
        if (!*cur) {
            apr->multi = 1;
            if (argv_is_required(first->optarg))
                apr->multi = 2;
            apr->mulopt = apr->optidx;
            apr->mulidx = 2;
            apr->mularg = apr->optidx + 1;
            *toklen = 1;
            return first;
        }
    }

    if (ambig) {
        argv_error(apr, "%s: option '%.*s' is ambiguous",
                   tok, *toklen, toktype);
    } else {
        argv_error(apr, "%s: unrecognized option '%.*s'",
                   tok, *toklen, toktype);
    }
    return NULL;
}

/* ********************************************************************** */
/* Return Next Argument */
/* ********************************************************************** */

int
argv_next(ARGVPARSER * apr, char ** arg, size_t * arglen)
{
    size_t toklen = 0; /* arglen may be NULL */
    ARGV * opt;
    int toktype;

    if (!arglen) arglen = &toklen;

    while ((toktype = argv_tok_next(apr, arg, arglen)) != ARGV_TOK_END) {

        if (toktype == ARGV_TOK_ARG) {
            if (!apr->mixed) apr->sawdash = 1;
            return 1;
        }
        if (toktype == ARGV_TOK_DASH) continue;

        opt = argv_tok_identify(apr, toktype, *arg, arglen);
        if (!opt) continue;

        /* Option cannot have arg
         */
        if (!argv_has_arg(opt->optarg)) {
            if (apr->argidx) { /* -opt=arg */
                argv_error(apr, "%s: invalid argument to option '%.*s'",
                           *arg, *arglen, toktype);
                apr->argidx = 0; /* drop the arg */
            }
            *arg = NULL;
            *arglen = 0;

        /* Option may/must have arg
         */
        } else {
            if (argv_tok_next(apr, NULL, NULL) == ARGV_TOK_ARG) {
                argv_tok_next(apr, arg, arglen);

            } else if (argv_is_required(opt->optarg)) {
                argv_error(apr, "%s: option '%.*s' requires an argument",
                           *arg, *arglen, toktype);
                continue;
            }
        }
        return opt->optval;
    }
    return 0;
}

/* ********************************************************************** */
/* Miscellaneous */
/* ********************************************************************** */

int
argv_set_errorf(ARGVPARSER * apr, int (*cb)(const char *))
{
    apr->err_fp = cb;
    return 0;
}

int
argv_set_progname(ARGVPARSER * apr, const char * progname)
{
    apr->pname = progname;
    return 0;
}

int
argv_set_mixed(ARGVPARSER * apr, int mixed)
{
    apr->mixed = !!mixed;
    return 0;
}

int
argv_error_count(ARGVPARSER * apr)
{
    return apr->errs;
}

int
argv_set_args(ARGVPARSER * apr, ARGV * args)
{
    size_t i;
    ARGV * o;

    apr->args = args;

    for (i = 0; apr->args[i].optval; ++i) {
        o = &(apr->args[i]);
        if (!o->optlen && o->optname)
            o->optlen = strlen(o->optname);
    }
    return 0;
}

/* ********************************************************************** */
/* Init / Uninit */
/* ********************************************************************** */

void
argv_uninit(ARGVPARSER * apr)
{
    memset(apr, 0, sizeof(ARGVPARSER));
}

int
argv_init(ARGVPARSER * apr, ARGV * args, int argc, char ** argv)
{
    const char * p;

    memset(apr, 0, sizeof(ARGVPARSER));
    apr->argc = argc;
    apr->argv = argv;
    apr->err_fp = &argv_error_cb;

    apr->pname = apr->argv[0];
    for (p = apr->pname; *p; ++p) {
        if (*p == '/' || *p == '\\') apr->pname = p + 1;
    }
    return argv_set_args(apr, args);
}

/* ********************************************************************** */
/* TEST */
/* ********************************************************************** */

#ifdef TEST

int
main(int argc, char * argv[])
{
    ARGV args[] = {
        { 'a', NULL,   0, ARGV_NOARG, 'a' },
        { 'b', "bb",   2, ARGV_NOARG, 'b' },
        { 0,   "cc",   2, ARGV_NOARG, 'c' },
        { 'd', NULL,   0, ARGV_ARGA,  'd' },
        { 'e', NULL,   0, ARGV_REQA,  'e' },
        { 'f', NULL,   0, ARGV_ARG,   'f' },
        { 'g', "gg",   2, ARGV_ARG,   'g' },
        { 0,   "hh",   2, ARGV_ARG,   'h' },
        { 'i', NULL,   0, ARGV_REQ,   'i' },
        { 'j', "jj",   2, ARGV_REQ,   'j' },
        { 0,   "kk",   2, ARGV_REQ,   'k' },
        { 0,   "long", 4, ARGV_REQ,   'l' },
        { 0,   "lost", 4, ARGV_REQ,   'L' },
        ARGV_LAST
    };
    ARGVPARSER apr;
    int c;
    size_t n = 0;
    char * s = NULL;

    argv_init(&apr, args, argc, argv);
    argv_set_mixed(&apr, 1);

    while ((c = argv_next(&apr, &s, &n))) {
        switch (c) {
        case 1:  fprintf(stderr, "param:%s,%d\n", s, (int)n); break;
        default: fprintf(stderr, "%c=%s,%d\n", c, s, (int)n); break;
        }
        fflush(stderr);
    }

    argv_uninit(&apr);
    return 0;
}

#endif /* TEST */

/* ********************************************************************** */
/* ********************************************************************** */
