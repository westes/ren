/* ren - rename multiple files according to search and replacement patterns.

   Copyright (C) 1988 Vladimir Lanin
   Copyright (C) 1997 Rob Schipper (Linux port)
   Copyright (C) 2026 Will Estes (autotools, ANSI C, gettext)

   Originally posted to comp.sources.unix (Volume 18, Issue 22) without
   a license notice.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <locale.h>
#include <libintl.h>

#define _(s) gettext (s)

#ifndef MAXNAMLEN
# define MAXNAMLEN 255
#endif

#define MAXWILD 20
#define MAXREP 40

typedef struct rep{
	int ftorep;
	char *repname;
	struct rep *dofirst;
	int mustdel;
	int status;
	struct rep *nextrep;
} REP;

#define DO 0
#define FORGET 1
#define DONE 2

#define ASKDEL 0
#define QUIETDEL 1
#define NODEL 2
#define ABORTDEL 3

static void procargs(int argc, char *argv[], int *pverbose, int *pdelstyle,
	char **pfrom_pat, char **pto_pat, char **ppath);
static int procdir(char *path, struct dirent ***pdot, REP ***pfilrep);
static void checkpats(char *from_pat, char *to_pat);
static int getreps(struct dirent *dot[], int nfils, char *from_pat,
	char *to_pat, REP *filrep[], REP **preps);
static int scan(char *pat, char *s, char **start1, int *len1);
static char *makerep(char *pat, char **start, int *len);
static void checkcollisons(REP *reps, int nreps);
static int mycmp(const void *pps1, const void *pps2);
static void checkdeletes(REP *reps, struct dirent *dot[], REP *filrep[],
	int nfils, int delstyle);
static int findname(char *s, struct dirent *dlist[], int n);
static char **breakcycles(REP *reps, int nreps, struct dirent *dot[]);
static void dorenames(REP *reps, struct dirent *dot[], char *tempnames[],
	int verbose);

static char tempprefix[] = "renTMP";

int
main(int argc, char *argv[])
{
	char *from_pat, *to_pat, *path;
	int verbose, delstyle;
	int nfils, nreps;
	struct dirent **dot;
	REP *reps, **filrep;
	char **tempnames;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	procargs(argc, argv, &verbose, &delstyle, &from_pat, &to_pat, &path);
	checkpats(from_pat, to_pat);
	nfils = procdir(path, &dot, &filrep);
	nreps = getreps(dot, nfils, from_pat, to_pat, filrep, &reps);
	checkcollisons(reps, nreps);
	checkdeletes(reps, dot, filrep, nfils, delstyle);
	tempnames = breakcycles(reps, nreps, dot);
	dorenames(reps, dot, tempnames, verbose);
	return(0);
}


static void
procargs(int argc, char *argv[], int *pverbose, int *pdelstyle,
	char **pfrom_pat, char **pto_pat, char **ppath)
{
	char *p;
	int arg1;

	*pverbose = 0;
	*pdelstyle = ASKDEL;
	for (arg1 = 1; arg1 < argc && *(argv[arg1]) == '-'; arg1++)
		for (p = argv[arg1]+1; *p != '\0'; p++)
			if (*p == 'v')
				*pverbose = 1;
			else if (*p == 'd')
				*pdelstyle = QUIETDEL;
			else if (*p == 'k')
				*pdelstyle = NODEL;
			else if (*p == 'a')
				*pdelstyle = ABORTDEL;
			else {
				fprintf(stderr, _("Illegal option -%c\n"), *p);
				exit(1);
			}

	if (argc - arg1 != 2) {
		fputs(_("Usage: ren [-d|-k|-a] [-v] [path/]search_pattern replacement_pattern\n"
			"\n"
			"Search patterns containing wildcard(s) should be quoted.\n"
			"Put #n into the replacement pattern to insert the string\n"
			"matched by the n'th wildcard in the search pattern.\n"),
			stderr);
		exit(1);
	}

	*ppath = argv[arg1];
	*pto_pat = argv[arg1 + 1];
	for (
		*pfrom_pat = *ppath + strlen(*ppath);
		**pfrom_pat != '/' && *pfrom_pat != *ppath;
		--(*pfrom_pat)
	) {}
	if (**pfrom_pat == '/') {
		**pfrom_pat = '\0';
		if (*pfrom_pat == *ppath)
			*ppath = "/";
		(*pfrom_pat)++;
	}
	else
		*ppath = ".";
}


static void
checkpats(char *from_pat, char *to_pat)
{
	char *p;
	int nwilds;

	for (nwilds = 0, p = from_pat; *p != '\0'; p++) {
		if (*p == '\\') {
			p++;
			if (*p == '\0')
				break;
		}
		else if (*p == '*' || *p == '?')
			nwilds++;
	}
	if (nwilds > MAXWILD) {
		fprintf(stderr,
			_("Too many wildcards in the search pattern (at most %d).\n"),
			MAXWILD);
		exit(1);
	}

	for (p = to_pat; *p != '\0'; p++) {
		if (*p == '/') {
			fputs(_("The replacement pattern must not contain a path.\n"),
				stderr);
			exit(1);
		}
		else if (*p == '*' || *p == '?') {
			fputs(_("No wildcards allowed in replacement pattern.\n"
				"Use #n to insert the substring matched\n"
				"by the n'th wildcard in the search pattern.\n"), stderr);
			exit(1);
		}
		else if (*p == '#') {
			if (*(p+1) < '1' || *(p+1) > '9' || *(p+1) - '0' > nwilds) {
				fprintf(stderr, _("#%c: bad substring number.\n"
					"(Use '\\#' to get '#' in replacement string.)\n"),
					*(p+1));
				exit(1);
			}
			p++;
		}
		else if (*p == '\\') {
			p++;
			if (*p == '\0')
				break;
		}
	}
}


static int
procdir(char *path, struct dirent ***pdot, REP ***pfilrep)
{
	int nfils;

	if (access(path, R_OK | X_OK | W_OK) < 0) {
		fprintf(stderr, _("Read/write access denied to %s\n"), path);
		exit(1);
	}
	if (chdir(path) < 0) {
		fprintf(stderr, _("Strange, can not change directory to %s\n"), path);
		exit(1);
	}
	if ((nfils = scandir(".", pdot, NULL, alphasort)) < 0) {
		fprintf(stderr, _("Strange, can not scan %s\n"), path);
		exit(1);
	}
	*pfilrep = (REP **)malloc(nfils * sizeof(REP *));
	return(nfils);
}



static int
getreps(struct dirent *dot[], int nfils, char *from_pat, char *to_pat,
	REP *filrep[], REP **preps)
{
	char *(start[MAXWILD]);
	int len[MAXWILD];
	int nreps, i;
	REP *cur;

	for (*preps = NULL, nreps = 0, i = 0; i < nfils; i++) {
		if (
			(*(dot[i]->d_name) != '.' || *from_pat == '.') &&
			strcmp(dot[i]->d_name, ".") != 0 &&
			strcmp(dot[i]->d_name, "..") != 0 &&
			scan(from_pat, dot[i]->d_name, start, len)
		) {
			cur = (REP *)malloc(sizeof(REP));
			filrep[i] = cur;
			cur->repname = makerep(to_pat, start, len);
			cur->ftorep = i;
			cur->mustdel = -1;
			cur->nextrep = *preps;
			cur->status = DO;
			*preps = cur;
			nreps++;
			if (*(cur->repname) == '\0') {
				fprintf(stderr, _("'%s' would have to be renamed to empty string.\n"
					"Aborting, no renames done.\n"), dot[i]->d_name);
				exit(1);
			}
		}
		else
			filrep[i] = NULL;
	}
	if (nreps == 0) {
		fputs(_("No match.\n"), stderr);
		exit(1);
	}
	return(nreps);
}


static int
scan(char *pat, char *s, char **start1, int *len1)
{
	*start1 = 0;
	while (1) {
		if (*pat == '*') {
			pat++;
			*start1 = s;
			if (*pat == '\0') {
				*len1 = strlen(s);
				return(1);
			}
			else {
				for (*len1=0; !scan(pat, s, start1+1, len1+1); (*len1)++, s++)
					if (*s == '\0')
						return(0);
				return(1);
			}
		}
		else if (*pat == '\0')
			return(*s == '\0');
		else if (*pat == '?') {
			if (*s == '\0')
				return(0);
			*(start1++) = s;
			*(len1++) = 1;
			pat++;
			s++;
		}
		else {
			if (*pat == '\\') {
				pat++;
				if (*pat == '\0')
					return(*s == '\0');
			}
			if (*pat == *s) {
				pat++;
				s++;
			}
			else
				return(0);
		}
	}
}


static char *
makerep(char *pat, char **start, int *len)
{
	int i, k;
	char *q, *p, *res;
	char b[MAXNAMLEN + 1];

	p = b;
	for ( ; *pat != '\0'; pat++) {
		if (*pat == '#') {
			pat++;
			k = *pat - '1';
			if (p - b + len[k] > MAXNAMLEN) {
				fputs(_("Replacement name too long.\n"), stderr);
				exit(1);
			}
			for (i=0, q = start[k]; i < len[k]; i++)
				*(p++)= *(q++);
		}
		else {
			if (*pat == '\\') {
				pat++;
				if (*pat == '\0')
					break;
			}
			if (p - b + 1 > MAXNAMLEN) {
				fputs(_("Replacement name too long.\n"), stderr);
				exit(1);
			}
			*(p++)= *pat;
		}
	}
	*(p++) = '\0';
	res = (char *)malloc((p - b) * sizeof(char));
	strcpy(res, b);
	return(res);
}


static void
checkcollisons(REP *reps, int nreps)
{
	char **repdict;
	REP *cur;
	int i;

	repdict = (char **)malloc(nreps * sizeof(char *));
	for (i = 0, cur = reps; cur != NULL; cur = cur->nextrep)
		repdict[i++] = cur->repname;
	qsort(repdict, nreps, sizeof(char *), mycmp);
	for (i = 0; i < nreps-1; i++)
		if (strcmp(repdict[i], repdict[i+1]) == 0) {
			fprintf(stderr, _("Two or more files would have to be renamed to '%s'.\n"
				"Aborting, no renames done.\n"), repdict[i]);
			exit(1);
		}
}


static int
mycmp(const void *pps1, const void *pps2)
{
	return(strcmp(*(char * const *)pps1, *(char * const *)pps2));
}


static void
checkdeletes(REP *reps, struct dirent *dot[], REP *filrep[], int nfils,
	int delstyle)
{
	int recheck, i;
	REP *cur;
	char doit, reply[MAXREP];

	do {
		recheck = 0;
		for (cur = reps; cur != NULL; cur = cur->nextrep) {
			if (cur->status == FORGET)
				continue;
			if ((i = findname(cur->repname, dot, nfils)) >= 0) {
				if (filrep[i] == NULL && cur->mustdel < 0) {
					cur->dofirst = NULL;
					if (delstyle == QUIETDEL)
						cur->mustdel = i;
					else if (delstyle == NODEL) {
						cur->status = FORGET;
						filrep[cur->ftorep] = NULL;
						recheck = 1;
					}
					else if (delstyle == ABORTDEL) {
						fprintf(stderr, _("%s would have to be removed.\n"
							"Aborting, no renames done.\n"), dot[i]->d_name);
						exit(1);
					}
					else {
						fprintf(stderr, _("%s -> %s ; remove old %s? "),
							dot[cur->ftorep]->d_name, cur->repname,
							dot[i]->d_name);
						for (;;) {
							if (fgets(reply, MAXREP, stdin) == NULL) {
								fputs(_("\nEnd of input, no renames done.\n"),
									stderr);
								exit(1);
							}
							doit = reply[0];
							if (doit ==  'Y' || doit == 'y') {
								cur->mustdel = i;
								break;
							}
							else if (doit == 'N' || doit == 'n') {
								cur->status = FORGET;
								filrep[cur->ftorep] = NULL;
								recheck = 1;
								break;
							}
							else
								fputs(_("Yes or No? "), stderr);
						}
					}
				}
				else {
					cur->dofirst = filrep[i];
					cur->mustdel = -1;
				}
			}
			else {
				cur->dofirst = NULL;
				cur->mustdel = -1;
			}
		}
	} while (recheck);
}


static int
findname(char *s, struct dirent *dlist[], int n)
{
	int first, k, last, res;

	for(first = 0, last = n - 1;;) {
		if (last < first)
			return(-1);
		k = (first + last) >> 1;
		if ((res = strcmp(s, dlist[k]->d_name)) == 0)
			return(k);
		if (res < 0)
			last = k - 1;
		else
			first = k + 1;
	}
}


static char **
breakcycles(REP *reps, int nreps, struct dirent *dot[])
{
	char **tempnames;
	int tempno;
	REP *cur, *pred;

	tempnames = (char **)malloc((nreps + 1) * sizeof(char *));
	tempno = 0;
	for (cur = reps; cur != NULL; cur = cur->nextrep) {
		if (cur->status == FORGET)
			continue;
		for (pred = cur->dofirst;
			 pred != NULL && pred != cur;
			 pred = pred->dofirst)
		{
			if (pred->status != DO) {
				fputs(_("Strange error in cycle checking.\n"), stderr);
				exit(1);
			}
		}
		if (pred == cur) {
			if (cur->dofirst == cur)
				cur->status = FORGET;
			else {
				pred = (REP *)malloc(sizeof(REP));
				tempnames[++tempno] =
					(char *)malloc(sizeof(tempprefix) + strlen(cur->repname));
				strcpy(tempnames[tempno], tempprefix);
				strcat(tempnames[tempno], cur->repname);
				pred->repname = cur->repname;
				pred->ftorep = -tempno;
				pred->dofirst = cur->dofirst;
				pred->mustdel = -1;
				pred->status = DO;
				pred->nextrep = cur->nextrep;
				cur->repname = tempnames[tempno];
				cur->dofirst = NULL;
				cur->nextrep = pred;
			}
		}
	}
	return(tempnames);
}


static void
dorenames(REP *reps, struct dirent *dot[], char *tempnames[], int verbose)
{
	REP *cur;
	int skipped;
	char *fromname;

	do {
		skipped = 0;
		for (cur = reps; cur != NULL; cur = cur->nextrep) {
			if (cur->status == DO) {
				if (cur->dofirst != NULL && cur->dofirst->status != DONE)
					skipped = 1;
				else {
					fromname = (cur->ftorep < 0 ?
						tempnames[-(cur->ftorep)] :
						dot[cur->ftorep]->d_name);
					cur->status = DONE;
					if (rename(fromname, cur->repname)) {
						fprintf(stderr, _("Strange. Can not rename '%s' to '%s'\n"),
							fromname, cur->repname);
					}
					else if (verbose) {
						printf("%s -> %s", fromname, cur->repname);
						if (cur->mustdel >= 0)
							fputs(" (*)", stderr);
						putchar('\n');
					}
				}
			}
		}
	} while (skipped);
}
