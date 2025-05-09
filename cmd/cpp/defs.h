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

extern int yylval;

int yylex(void);
int yyparse(void);
char *skipbl(char *p);
struct symtab *lookup(char *namep, int enterf);
void pperror(const char *s, ...);
void ppwarn(const char *s, ...);
