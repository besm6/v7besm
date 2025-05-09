enum {
    YYEOF,
    number,
    stop,
    DEFINED,
    EQ,
    NE,
    LE,
    GE,
    LS,
    RS,
    ANDAND,
    OROR,
    UMINUS,
};

struct symtab {
    char *name;
    char *value;
};

extern char fastab[];

#define IB 1
#define SB 2
#define NB 4
#define CB 8
#define QB 16
#define WB 32

#define isid(a)   (fastab[(unsigned char)a] & IB)
#define isnum(a)  (fastab[(unsigned char)a] & NB)
#define iscom(a)  (fastab[(unsigned char)a] & CB)
#define isquo(a)  (fastab[(unsigned char)a] & QB)
#define iswarn(a) (fastab[(unsigned char)a] & WB)

extern int yylval;

int yylex(void);
int yyparse(void);
char *skipbl(char *p);
struct symtab *lookup(char *namep, int enterf);
void pperror(const char *s, ...);
void ppwarn(const char *s, ...);
