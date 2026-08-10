%token CHAR CCL NCCL STR DELIM SCON ITER NEWE NULLS
%left SCON '/' NEWE
%left '|'
%left '$' '^'
%left CHAR CCL NCCL '(' '.' STR NULLS
%left ITER
%left CAT
%left '*' '+' '?'

%{
#include "ldefs.h"
%}
%%
%{
// b6yacc copies this block to file scope, so these are globals -- and yylex()
// below declares locals of its own, which is why they are not called i, j, k.
int act_i;
int act_j, act_k;
int act_g;
char *act_p;
static char *strval(int which);
static char *dsave(const char *s);
%}
acc	:	lexinput
	={
	}
	;
lexinput:	defns delim prods end
	|	defns delim end
	={
		if(!funcflag)phead2();
		funcflag = TRUE;
	}
	| error
	={
		}
	;
end:		delim | ;
defns:	defns STR STR
	={	if(dptr + 1 >= DEFSIZE)
			error("Too many definitions");
		def[dptr] = dsave(strval($2));
		subs[dptr] = dsave(strval($3));
		dptr++;
		subs[dptr]=def[dptr]=0;	/* for lookup - require ending null */
	}
	|
	;
delim:	DELIM
	={
		sect++;
		}
	;
prods:	prods pr
	={	$$ = mn2(RNEWE,$1,$2);
		}
	|	pr
	={	$$ = $1;}
	;
pr:	r NEWE
	={
		if(divflg == TRUE)
			act_i = mn1(S1FINAL,casecount);
		else act_i = mn1(FINAL,casecount);
		$$ = mn2(RCAT,$1,act_i);
		divflg = FALSE;
		casecount++;
		}
	| error NEWE
	={
		}
r:	CHAR
	={	$$ = mn0($1); }
	| STR
	={
		act_p = strval($1);
		act_i = mn0((unsigned char)*act_p++);
		while(*act_p)
			act_i = mn2(RSTR,act_i,(unsigned char)*act_p++);
		$$ = act_i;
		}
	| '.'
	={	symbol['\n'] = 0;
		if(psave < 0){
			if(ccptr + NCH > ccl + CCLSIZE)
				error("Too many large character classes");
			psave = (int)(ccptr - ccl);
			for(act_i=1;act_i<'\n';act_i++){
				symbol[act_i] = 1;
				*ccptr++ = act_i;
				}
			for(act_i='\n'+1;act_i<NCH;act_i++){
				symbol[act_i] = 1;
				*ccptr++ = act_i;
				}
			*ccptr++ = 0;
			}
		$$ = mn1(RCCL,psave);
		cclinter(1);
		}
	| CCL
	={	$$ = mn1(RCCL,$1); }
	| NCCL
	={	$$ = mn1(RNCCL,$1); }
	| r '*'
	={	$$ = mn1(STAR,$1); }
	| r '+'
	={	$$ = mn1(PLUS,$1); }
	| r '?'
	={	$$ = mn1(QUEST,$1); }
	| r '|' r
	={	$$ = mn2(BAR,$1,$3); }
	| r r %prec CAT
	={	$$ = mn2(RCAT,$1,$2); }
	| r '/' r
	={	if(!divflg){
			act_j = mn1(S2FINAL,-casecount);
			act_i = mn2(RCAT,$1,act_j);
			$$ = mn2(DIV,act_i,$3);
			}
		else {
			$$ = mn2(RCAT,$1,$3);
			warning("Extra slash removed");
			}
		divflg = TRUE;
		}
	| r ITER ',' ITER '}'
	={	if($2 > $4){
			act_i = $2;
			$2 = $4;
			$4 = act_i;
			}
		if($4 <= 0)
			warning("Iteration range must be positive");
		else {
			act_j = $1;
			for(act_k = 2; act_k<=$2;act_k++)
				act_j = mn2(RCAT,act_j,dupl($1));
			for(act_i = $2+1; act_i<=$4; act_i++){
				act_g = dupl($1);
				for(act_k=2;act_k<=act_i;act_k++)
					act_g = mn2(RCAT,act_g,dupl($1));
				act_j = mn2(BAR,act_j,act_g);
				}
			$$ = act_j;
			}
	}
	| r ITER '}'
	={
		if($2 < 0)warning("Can't have negative iteration");
		else if($2 == 0) $$ = mn0(RNULLS);
		else {
			act_j = $1;
			for(act_k=2;act_k<=$2;act_k++)
				act_j = mn2(RCAT,act_j,dupl($1));
			$$ = act_j;
			}
		}
	| r ITER ',' '}'
	={
				/* from n to infinity */
		if($2 < 0)warning("Can't have negative iteration");
		else if($2 == 0) $$ = mn1(STAR,$1);
		else if($2 == 1)$$ = mn1(PLUS,$1);
		else {		/* >= 2 iterations minimum */
			act_j = $1;
			for(act_k=2;act_k<$2;act_k++)
				act_j = mn2(RCAT,act_j,dupl($1));
			act_k = mn1(PLUS,dupl($1));
			$$ = mn2(RCAT,act_j,act_k);
			}
		}
	| SCON r
	={	$$ = mn2(RSCON,$2,$1); }
	| '^' r
	={	$$ = mn1(CARAT,$2); }
	| r '$'
	={	act_i = mn0('\n');
		if(!divflg){
			act_j = mn1(S2FINAL,-casecount);
			act_k = mn2(RCAT,$1,act_j);
			$$ = mn2(DIV,act_k,act_i);
			}
		else $$ = mn2(RCAT,$1,act_i);
		divflg = TRUE;
		}
	| '(' r ')'
	={	$$ = $2; }
	|	NULLS
	={	$$ = mn0(RNULLS); }
	;
%%
// yylex()'s two string buffers.  A STR token's value names one of them rather
// than pointing into it: YYSTYPE is int and an int is not a char *.
static char token[TOKENSIZE];

static char *strval(int which)
{
	return which == STR_NAME ? buf : token;
}

// copy a definition or its translation into dchar, checking for room first
static char *dsave(const char *s)
{
	char *r;
	size_t n;

	n = strlen(s) + 1;
	if (dp + n > dchar + DEFCHAR)
		error("Definitions too long");
	strcpy(dp, s);
	r = dp;
	dp += n;
	return r;
}

// one of the %e %n %p %a %o %k bounds
static int sizeopt(char *p, const char *what)
{
	int n;

	while(*p && !digit(*p)) p++;
	n = siconv(p);
	if (n <= 0)
		error("%s must be positive", what);
	if (report == 2) report = 1;
	return n;
}

int yylex(void)
{
	register char *p;
	register int c, i;
	char  *t;
	int n, j, k, x;
	static int sectbegin;
	static int iter;

	if(sect == DEFSECTION) {		/* definitions section */
		while(!eof) {
			if(prev == '\n'){		/* next char is at beginning of line */
				getl(p=buf);
				switch(*p){
				case '%':
					switch(c= *(p+1)){
					case '%':
						lgate();
						fprintf(fout,"# define YYNEWLINE %d\n",ctable['\n']);
						fprintf(fout,"int yylex(void)\n{\nint nstr; extern int yyprevious;\n");
						sectbegin = TRUE;
						name = (int *)myalloc(treesize,sizeof(*name));
						left = (int *)myalloc(treesize,sizeof(*left));
						right = (int *)myalloc(treesize,sizeof(*right));
						nullstr = myalloc(treesize,sizeof(*nullstr));
						parent = (int *)myalloc(treesize,sizeof(*parent));
						if(name == 0 || left == 0 || right == 0 || parent == 0 || nullstr == 0)
							error("Too little core for parse tree");
						return(DELIM);
					case 'p': case 'P':	/* has overridden number of positions */
						maxpos = sizeopt(p, "%p");
						continue;
					case 'n': case 'N':	/* has overridden number of states */
						nstates = sizeopt(p, "%n");
						continue;
					case 'e': case 'E':		/* has overridden number of tree nodes */
						treesize = sizeopt(p, "%e");
						continue;
					case 'o': case 'O':
						outsize = sizeopt(p, "%o");
						continue;
					case 'a': case 'A':		/* has overridden number of transitions */
						ntrans = sizeopt(p, "%a");
						continue;
					case 'k': case 'K': /* overriden packed char classes */
						n = sizeopt(p, "%k");
						free(pchar);
						pchlen = n;
						pchar=pcptr=(unsigned char *)myalloc(pchlen, sizeof(*pchar));
						continue;
					case 't': case 'T': 	/* character set specifier */
						n = siconv(p+2);
						if(n <= 0 || n >= NCH)
							error("%%t size must be 1 through %d",NCH-1);
						chset = TRUE;
						for(i = 0; i<NCH; i++)
							ctable[i] = 0;
						while(getl(p) && strcmp(p,"%T") != 0 && strcmp(p,"%t") != 0){
							if((n = siconv(p)) <= 0 || n >= NCH){
								warning("Character value %d out of range",n);
								continue;
								}
							while(!space(*p) && *p) p++;
							while(space(*p)) p++;
							t = p;
							while(*t){
								c = ctrans(&t);
								if(ctable[c]){
									if (printable(c))
										warning("Character '%c' used twice",c);
									else
										warning("Character %o used twice",c);
									}
								else ctable[c] = n;
								t++;
								}
							p = buf;
							}
						{
						/* fold every character nobody named into a class
						   somebody did */
						unsigned char chused[NCH]; int kr;
						for(i=0; i<NCH; i++)
							chused[i]=0;
						for(i=0; i<NCH; i++)
							chused[ctable[i]]=1;
						for(kr=i=1; i<NCH; i++)
							if (ctable[i]==0)
								{
								while (kr < NCH-1 && chused[kr] == 0)
									kr++;
								if (chused[kr] == 0)
									error("%%t names no class to fold into");
								ctable[i]=kr;
								chused[kr]=1;
								}
						}
						lgate();
						continue;
					case '{':
						lgate();
						while(getl(p) && strcmp(p,"%}") != 0)
							fprintf(fout, "%s\n",p);
						if(p[0] == '%') continue;
						error("Premature eof");
					case 's': case 'S':		/* start conditions */
						lgate();
						while(*p && chpos(*p," \t,") < 0) p++;
						n = TRUE;
						while(n){
							while(*p && chpos(*p," \t,") >= 0) p++;
							t = p;
							while(*p && chpos(*p," \t,") < 0)p++;
							if(!*p) n = FALSE;
							*p++ = 0;
							if (*t == 0) continue;
							i = sptr*2;
							fprintf(fout,"# define %s %d\n",t,i);
							if(sptr + 1 >= STARTSIZE)
								error("Too many start conditions");
							if(sp + strlen(t) + 1 > schar + STARTCHAR)
								error("Start conditions too long");
							strcpy(sp, t);
							sname[sptr++] = sp;
							sname[sptr] = 0;	/* required by lookup */
							sp += strlen(sp) + 1;
							}
						continue;
					default:
						warning("Invalid request %s",p);
						continue;
						}	/* end of switch after seeing '%' */
				case ' ': case '\t':		/* must be code */
					lgate();
					fprintf(fout, "%s\n",p);
					continue;
				default:		/* definition */
					while(*p && !space(*p)) p++;
					if(*p == 0)
						continue;
					prev = *p;
					*p = 0;
					bptr = p+1;
					yylval = STR_NAME;
					if(digit(buf[0]))
						warning("Substitution strings may not begin with digits");
					return(STR);
					}
				}
			/* still sect 1, but prev != '\n' */
			else {
				p = bptr;
				while(*p && space(*p)) p++;
				if(*p == 0)
					warning("No translation given - null string assumed");
				strcpy(token, p);
				yylval = STR_TOKEN;
				prev = '\n';
				return(STR);
				}
			}
		/* end of section one processing */
		}
	else if(sect == RULESECTION){		/* rules and actions */
		while(!eof){
			switch(c=gch()){
			case '\0':
				return(0);
			case '\n':
				if(prev == '\n') continue;
				x = NEWE;
				break;
			case ' ':
			case '\t':
				if(sectbegin == TRUE){
					cpyact();
					while((c=gch()) && c != '\n');
					continue;
					}
				if(!funcflag)phead2();
				funcflag = TRUE;
				fprintf(fout,"case %d:\n",casecount);
				if(cpyact()){
					fprintf(fout,"break;\n");
					}
				while((c=gch()) && c != '\n');
				if(peek == ' ' || peek == '\t' || sectbegin == TRUE){
					warning("Executable statements should occur right after %%%%");
					continue;
					}
				x = NEWE;
				break;
			case '%':
				if(prev != '\n') goto character;
				if(peek == '{'){	/* included code */
					getl(buf);
					while(!eof && getl(buf) && strcmp("%}",buf) != 0)
						fprintf(fout,"%s\n",buf);
					continue;
					}
				if(peek == '%'){
					(void)gch();
					(void)gch();
					x = DELIM;
					break;
					}
				goto character;
			case '|':
				if(peek == ' ' || peek == '\t' || peek == '\n'){
					casecount++;
					fprintf(fout,"case %d:\n",casecount-1);
					while((c=gch()) && c != '\n');
					continue;
					}
				x = '|';
				break;
			case '$':
				if(peek == '\n' || peek == ' ' || peek == '\t' || peek == '|' || peek == '/'){
					x = c;
					break;
					}
				goto character;
			case '^':
				if(prev != '\n' && scon != TRUE) goto character;
			case '?':
			case '+':
			case '.':
			case '*':
			case '(':
			case ')':
			case ',':
			case '/':
				x = c;
				break;
			case '}':
				iter = FALSE;
				x = c;
				break;
			case '{':	/* either iteration or definition */
				if(digit(c=gch())){	/* iteration */
					iter = TRUE;
				ieval:
					i = 0;
					while(digit(c)){
						if(i >= TOKENSIZE-1)
							error("Iteration count too long");
						token[i++] = c;
						c = gch();
						}
					token[i] = 0;
					yylval = siconv(token);
					unputc(c);
					x = ITER;
					break;
					}
				else {		/* definition */
					i = 0;
					while(c && c!='}'){
						if(i >= TOKENSIZE-1)
							error("Definition name too long");
						token[i++] = c;
						c = gch();
						}
					token[i] = 0;
					i = lookup(token,def);
					if(i < 0)
						warning("Definition %s not found",token);
					else
						unputs(subs[i]);
					continue;
					}
			case '<':		/* start condition ? */
				if(prev != '\n')		/* not at line begin, not start */
					goto character;
				{
				unsigned char *tp, *xq;
				tp = slptr;
				do {
					i = 0;
					c = gch();
					while(c != ',' && c && c != '>'){
						if(i >= TOKENSIZE-1)
							error("Start condition name too long");
						token[i++] = c;
						c = gch();
						}
					token[i] = 0;
					if(i == 0)
						goto character;
					i = lookup(token,sname);
					if(i < 0) {
						warning("Undefined start condition %s",token);
						continue;
						}
					if(slptr + 2 > slist + STARTSIZE)
						error("Too many start conditions used");
					*slptr++ = i+1;
					} while(c && c != '>');
				*slptr++ = 0;
				/* check if previous value re-usable */
				for (xq=slist; xq<tp; )
					{
					if (strcmp((char *)xq, (char *)tp)==0)
						break;
					while (*xq++);
					}
				if (xq<tp)
					{
					/* re-use previous pointer to string */
					slptr=tp;
					tp=xq;
					}
				yylval = (int)(tp - slist);
				}
				x = SCON;
				break;
			case '"':
				i = 0;
				while((c=gch()) && c != '"' && c != '\n'){
					if(c == '\\') c = usescape(c=gch());
					if(i >= TOKENSIZE-1){
						warning("String too long");
						break;
						}
					token[i++] = c;
					}
				if(c == '\n') {
					yyline--;
					warning("Non-terminated string");
					yyline++;
					}
				token[i] = 0;
				if(i == 0)x = NULLS;
				else if(i == 1){
					yylval = (unsigned char)token[0];
					x = CHAR;
					}
				else {
					yylval = STR_TOKEN;
					x = STR;
					}
				break;
			case '[':
				for(i=1;i<NCH;i++) symbol[i] = 0;
				x = CCL;
				if((c = gch()) == '^'){
					x = NCCL;
					c = gch();
					}
				while(c != ']' && c){
					if(c == '\\') c = usescape(c=gch());
					symbol[c] = 1;
					j = c;
					if((c=gch()) == '-' && peek != ']'){		/* range specified */
						c = gch();
						if(c == '\\') c = usescape(c=gch());
						k = c;
						if(j > k) {
							n = j;
							j = k;
							k = n;
							}
						/* a range wholly above 0177 is the documented
						   way to name the high half, so it is not the
						   ASCII-adjacency assumption this warns about */
						if(!(('A' <= j && k <= 'Z') ||
						     ('a' <= j && k <= 'z') ||
						     ('0' <= j && k <= '9') ||
						     j > 0177))
							warning("Non-portable Character Class");
						for(n=j+1;n<=k;n++)
							symbol[n] = 1;		/* implementation dependent */
						c = gch();
						}
					}
				/* try to pack ccl's */
				i = 0;
				for(j=0;j<NCH;j++)
					if(symbol[j])token[i++] = j;
				token[i] = 0;
				{
				const unsigned char *q;
				q = ccptr;
				if(optim){
					q = ccl;
					while(q < ccptr && strcmp(token,(char *)q) != 0)q++;
					}
				if(q < ccptr)	/* found it */
					yylval = (int)(q - ccl);
				else {
					if(ccptr + i + 1 > ccl + CCLSIZE)
						error("Too many large character classes");
					yylval = (int)(ccptr - ccl);
					strcpy((char *)ccptr, token);
					ccptr += i + 1;
					}
				}
				cclinter(x==CCL);
				break;
			case '\\':
				c = usescape(c=gch());
			default:
			character:
				if(iter){	/* second part of an iteration */
					iter = FALSE;
					if('0' <= c && c <= '9')
						goto ieval;
					}
				if(alpha(peek)){
					i = 0;
					token[i++] = c;
					while(alpha(peek)){
						if(i >= TOKENSIZE-1)
							error("Identifier too long");
						token[i++] = gch();
						}
					if(peek == '?' || peek == '*' || peek == '+')
						unputc((unsigned char)token[--i]);
					token[i] = 0;
					if(i == 1){
						yylval = (unsigned char)token[0];
						x = CHAR;
						}
					else {
						yylval = STR_TOKEN;
						x = STR;
						}
					}
				else {
					yylval = c;
					x = CHAR;
					}
				}
			scon = FALSE;
			if(x == SCON)scon = TRUE;
			sectbegin = FALSE;
			return(x);
			}
		}
	/* section three */
	ptail();
	while(getl(buf) && !eof)
		fprintf(fout,"%s\n",buf);
	return(0);
	}
/* end of yylex */
