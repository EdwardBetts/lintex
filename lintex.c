/*------------------------------------------------------*
 | Author: Maurizio Loreti, aka MLO or (HAM) I3NOO      |
 | Work:   University of Padova - Department of Physics |
 |         Via F. Marzolo, 8 - 35131 PADOVA - Italy     |
 | Phone:  ++39(49) 827-7216     FAX: ++39(49) 827-7102 |
 | EMail:  loreti@padova.infn.it                        |
 | WWW:    http://wwwcdf.pd.infn.it/~loreti/mlo.html    |
 *------------------------------------------------------*

 Copyright (C) 2010 Ryan Kavanagh <ryanakca@kubuntu.org>

   $Id: lintex.c,v 1.6 2006/09/11 12:47:38 loreti Exp $


  Description: the command "lintex [-i] [-r] [dir1 [dir2 ...]]" scans
    the directories given as parameters (the default is the current
    directory), looking for TeX-related files no more needed and to be
    removed.  With the option -i the user is asked before actually
    removing any file; with the option -r, the given directories are
    scanned recursively.

  Environment: the program has been developed under Solaris 2; but
    should run on every system supporting opendir/readdir/closedir and
    stat.  The file names in the struct dirent (defined in <dirent.h>)
    are assumed to be null terminated (this is guaranteed under Solaris
    2).

  Compiler flags: -DDEBUG to list remove commands without their actual
    execution; -DFULLDEBUG for the above and a lot of printouts.

  History:
    1.00 - 1996-07-05 , first release
    1.01 - 1996-07-25 , improved (modify time in nodes)
    1.02 - 1996-10-16 , list garbage files w/out .tex
    1.03 - 1997-05-21 , call to basename; uploaded to CTAN, where lives
                        in /tex-archive/support/lintex .  Added a man
                        page and a Makefile.
    1.04 - 1998-06-22 , multiple directories in the command line; -r
                        command option; -I and -R accepted, in addition
                        to -i and -r; more extensions in protoTree; code
                        cleanup.
    1.05 - 2001-12-02 , linked list structure optimized.
    1.06 - 2002-09-25 , added .pdf extension.
           2010-07-11 , don't delete read only files
           2010-07-14 , delete .bbl BibTeX files

  ---------------------------------------------------------------------*/

/**
 | Included files
**/

#include <stdio.h>              /* Standard library */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#include <sys/types.h>          /* Unix proper */
#include <sys/stat.h>
#include <dirent.h>

/**
 | Definitions:
 | - LONG_ENOUGH: length of the buffer used to read the answer from the
 |   user, when the -i command option is specified;
 | - MAX_B_EXT: maximum length of the extension for backup files (including
 |   the leading dot and the trailing '\0').
 | - TRUE, FALSE: guess what?
**/

#define LONG_ENOUGH 48
#define MAX_B_EXT    8
#define TRUE         1
#define FALSE        0

/**
 | Type definitions:
 | - Froot: the root of a linked list structure, where file names having a
 |     given extension (pointed to by Froot.extension) will be stored;
 |     these linked lists are also used to store directory names (with fake
 |     extension strings).
 | - Fnode: an entry in the linked list of the file names; contains the
 |     file modification time, the file name and a pointer to the next node.
 |     As a side note, the so called 'struct hack', here used to store the
 |     file name, is not guaranteed to work by the current C ANSI standard;
 |     but no environment/compiler where it does not work is currently
 |     known.
**/

typedef struct sFroot {
  char *extension;
  struct sFnode *firstNode;
  struct sFnode *lastNode;
} Froot;

typedef struct sFnode {
  time_t mTime;
  struct sFnode *next;
  int write;
  char name[1];
} Fnode;

/**
 | Global variables:
 | - confirm: will be 0 or 1 according to the -i command option;
 | - recurse: will be 0 or 1 according to the -r command option;
 | - keep: will be 0 or 1 according to the -k command option;
 | - bExt: the extension for backup files: defaults to "~" (the emacs
 |   convention);
 | - n_bExt: the length of the previous string;
 | - programName: the name of the executable;
 | - protoTree: Froot's of the file names having extensions relevant to
 |   TeX.  ".tex" extensions are assumed to be pointed to by protoTree[0].
 | - keepTree: Froot's of the file names having extensions relevant to final
 |   generated documents.
**/

static int     confirm         = FALSE;
static int     recurse         = FALSE;
static int     keep            = FALSE;
static char    bExt[MAX_B_EXT] = "~";
static size_t  n_bExt;
static char   *programName;

static Froot protoTree[] = {
  {".tex", 0, 0},                        /* Must be first */
  {".aux", 0, 0},
  {".log", 0, 0},
  {".dvi", 0, 0},
  {".ps",  0, 0},
  {".pdf", 0, 0},
  {".toc", 0, 0},
  {".lof", 0, 0},
  {".lot", 0, 0},
  {".idx", 0, 0},
  {".ind", 0, 0},
  {".ilg", 0, 0},
  {".bbl", 0, 0},
  {0, 0, 0}                              /* Must be last (sentinel) */
};

static Froot keepTree[] = {
  {".pdf", 0, 0},
  {".ps",  0, 0},
  {".dvi", 0, 0},
  {0, 0, 0}
};

/**
 | Procedure prototypes (in alphabetical order)
**/

static char  *baseName(char *);
static Froot *buildTree(char *, Froot *);
static void   clean(char *);
static void   examineTree(Froot *, char *);
static void   insertNode(char *, size_t, time_t, int, Froot *);
static void   noMemory(void);
static void   nuke(char *);
static void   releaseTree(Froot *);
static void   syntax(void);

#ifdef FULLDEBUG
static void   printTree(Froot *);
#endif   /* FULLDEBUG */

/*---------------------------*
 | And now, our main program |
 *---------------------------*/

int main(
  int argc,
  char *argv[]
){
  Froot *dirNames;              /* To hold the directories to be scanned */
  Fnode *pFN;                   /* Running pointer over directory names  */
  int    to_bExt  = FALSE;      /* Flag "next parameter to bExt"         */

  /**
   | Scans the arguments appropriately; the required directories are stored
   | in the linked list starting at "dirNames".
  **/

  programName = baseName(argv[0]);

  if ((dirNames = calloc(2, sizeof(Froot))) == 0) {
    noMemory();
  }
  dirNames->extension = "argv";

  while (--argc) {
    if ((*++argv)[0] == '-') {
      switch ( (*argv)[1] ) {
        case 'i':   case 'I':
          confirm = TRUE;
          break;

        case 'r':   case 'R':
          recurse = TRUE;
          break;

        case 'k':   case 'K':
          keep = TRUE;
          break;

        case 'b':   case 'B':
          to_bExt = TRUE;
          break;

        default:
          syntax();
      }

    } else {
      if (to_bExt) {
        strcpy(bExt, *argv);
        to_bExt = FALSE;
      } else {
        insertNode(*argv, 0, 0, 0, dirNames);
      }
    }
  }

  if (to_bExt) {
    syntax();
  }
  n_bExt = strlen(bExt);

  /**
   | If no parameter has been given, clean the current directory
  **/

  if ((pFN = dirNames->firstNode) == 0) {
    clean(".");
  } else {
    while (pFN != 0) {
      clean(pFN->name);
      pFN = pFN->next;
    }
  }
  releaseTree(dirNames);

  return EXIT_SUCCESS;
}

/*------------------------------------------*
 | The called procedures (in logical order) |
 *------------------------------------------*/

static void insertNode(
  char   *name,
  size_t  lName,
  time_t  mTime,
  int write,
  Froot  *root
){

  /**
   | Creates a new Fnode, to be inserted at the _end_ of the linked
   | list pointed to by root->firstNode (i.e., the list is organized
   | as a "queue", a.k.a. "FIFO" list): if a new node cannot be created,
   | an error message is printed and the program aborted.
   | If "lName" is bigger than zero, the file name is represented by the
   | first lName characters of "name"; otherwise by the whole string in
   | "name".
  **/

  Fnode  *pFN;                  /* The new node created by insertNode */
  size_t  sSize;                /* Structure size                     */

  sSize = sizeof(Fnode) + (lName == 0 ? strlen(name) : lName);

  if ((pFN = malloc(sSize)) == 0) {
    noMemory();
  }
  pFN->mTime = mTime;
  pFN->write = write;
  pFN->next  = 0;

  if (lName == 0) {
    strcpy(pFN->name, name);
  } else {
    strncpy(pFN->name, name, lName);
    pFN->name[lName] = '\0';
  }

  if (root->lastNode == 0) {
    root->firstNode = pFN;
  } else {
    root->lastNode->next = pFN;
  }
  root->lastNode = pFN;
}

static void noMemory(void)
{
  fprintf(stderr, "%s: couldn't obtain heap memory\n", programName);
  exit(EXIT_FAILURE);
}

static void clean(
  char *dirName
){

  /**
   | Does the job for the directory "dirName".
   |
   | Builds a structure holding the TeX-related files, and does the
   | required cleanup; finally, removes the file structure.
   | If the list appended to "dirs" has been filled, recurse over the
   | tree of subdirectories.
  **/

  Froot *teXTree;               /* Root node of the TeX-related files  */
  Froot *dirs;                  /* Subdirectories in this directory    */
  Fnode *pFN;                   /* Running pointer over subdirectories */

  if ((dirs = calloc(2, sizeof(Froot))) == 0) {
    noMemory();
  }
  dirs->extension = "subs";

  if ((teXTree = buildTree(dirName, dirs)) != 0) {

#ifdef FULLDEBUG
    printTree(teXTree);
#endif   /* FULLDEBUG */

    examineTree(teXTree, dirName);
    releaseTree(teXTree);
  }

  for (pFN = dirs->firstNode;   pFN != 0;   pFN = pFN->next) {
    clean(pFN->name);
  }
  releaseTree(dirs);
}

static Froot *buildTree(
  char  *dirName,
  Froot *subDirs
){

  /**
   | - Opens the required directory;
   | - allocates a structure to hold the names of the TeX-related files,
   |   initialized from the global structure "protoTree";
   | - starts a loop over all the files of the given directory.
  **/

  DIR           *pDir;         /* Pointer returned from opendir()    */
  struct dirent *pDe;          /* Pointer returned from readdir()    */
  Froot         *teXTree;      /* Root node of the TeX-related files */

#ifdef FULLDEBUG
  printf("* Scanning directory \"%s\" - confirm = %c, recurse = %c\n",
         dirName, (confirm ? 'Y' : 'N'), (recurse ? 'Y' : 'N'));
  printf("* Keep generated document %c\n", (keep ? 'Y' : 'N'));
  printf("* Editor trailer: \"%s\"\n", bExt);
  puts("------------------------------Phase 1: directory scan");
#endif   /* FULLDEBUG */

  if ((pDir = opendir(dirName)) == 0) {
    fprintf(stderr,
            "%s: \"%s\" cannot be opened (or is not a directory)\n",
            programName, dirName);
    return 0;
  }

  if ((teXTree = malloc(sizeof(protoTree))) == 0) {
    noMemory();
  }
  memcpy(teXTree, protoTree, sizeof(protoTree));

  while ((pDe = readdir(pDir)) != 0) {
    char    tName[FILENAME_MAX];         /* Fully qualified file name       */
    struct  stat sStat;                  /* To be filled by stat(2)         */
    size_t  len;                         /* Lenght of the current file name */
    size_t  last;                        /* Index of its last character     */
    char   *pFe;                         /* Pointer to file extension       */

    /**
     | - Tests for empty inodes (already removed files);
     | - skips the . and .. (current and previous directory);
     | - tests the trailing part of the file name against the extension of
     |   the backup files, to be always deleted.
    **/

    if (pDe->d_ino == 0)                continue;
    if (strcmp(pDe->d_name, ".")  == 0) continue;
    if (strcmp(pDe->d_name, "..") == 0) continue;

    sprintf(tName, "%s/%s", dirName, pDe->d_name);

    len  = strlen(pDe->d_name);
    last = len - 1;

    if (n_bExt != 0) {                  /* If 0, no backup files to delete */
      int crit;                         /* What exceeds backup extensions  */

      crit = len - n_bExt;
      if (crit > 0   &&   strcmp(pDe->d_name + crit, bExt) == 0) {
        nuke(tName);
        continue;
      }
    }

    /**
     | If the file is a directory and the -r option has been given, stores
     | the directory name in the linked list pointed to by "subDirs", for
     | recursive calls.
     |
     | N.B.: if stat(2) fails, the file is skipped.
    **/

    if (stat(tName, &sStat) != 0) {
      fprintf(stderr, "File \"%s", tName);
      perror("\"");
      continue;
    }

    if (S_ISDIR(sStat.st_mode) != 0) {

#ifdef FULLDEBUG
      printf("File %s - is a directory\n", pDe->d_name);
#endif   /* FULLDEBUG */

      if (recurse) {
        insertNode(tName, 0, 0, 0, subDirs);
      }
      continue;
    }

    /**
     | If the file has an extension (the rightmost dot followed by at
     | least one character), and if that extension matches one of the
     | entries in teXTree[i].extension: stores the file name (with the
     | extension stripped) in the appropriate linked list, together with
     | its modification time.
     |
     | Exception: A file is not added to teXTree if the keep option is
     | enabled and if the extension is in the list of extensions to keep.
    **/

    if ((pFe = strrchr(pDe->d_name, '.')) != 0) {
      size_t nameLen;

      nameLen = pFe - pDe->d_name;
      if (nameLen < last) {
        Froot *pTT;

#ifdef FULLDEBUG
        printf("File %s - extension %s", pDe->d_name, pFe);
#endif   /* FULLDEBUG */

        /**
         | Loop on recognized TeX-related file extensions
        **/

        for (pTT = teXTree;   pTT->extension != 0;   pTT++) {
          if ((strcmp(pFe, pTT->extension) == 0)) {
            /* Do we want to keep final product? */
            if (keep) {
              /* kExt will contain the extensions we want to keep */
              Froot *kExt;
              /**
               | Loop on recognized TeX-related document extensions
               | to make sure we aren't deleting a document we want
               | to keep.
               |
               | Surely there's a more elegant way?
              **/
              int guard = 1;
              for (kExt = keepTree; kExt->extension != 0; kExt++) {
                if ((strcmp(kExt->extension, pTT->extension) == 0)) {
                  guard = 0;
                  break;
                }
              }
              if (guard) {
                /**
                 | This is not a final TeX document. Let's add it to the list
                 | of files to remove.
                **/
                insertNode(pDe->d_name, nameLen, sStat.st_mtime,
                           access(tName, W_OK), pTT);
#ifdef FULLDEBUG
              } else {
                printf("File %s - keep final document with extension %s\n",
                        pDe->d_name, pTT->extension);
#endif
              }
            } else {
              insertNode(pDe->d_name, nameLen, sStat.st_mtime,
                         access(tName, W_OK), pTT);
            }

#ifdef FULLDEBUG
            printf(" - inserted in tree");
#endif   /* FULLDEBUG */
            break;
          }
        } /* loop on known extensions */

#ifdef FULLDEBUG
        puts("");

      } else {
        printf("File %s - empty extension\n", pDe->d_name);
#endif   /* FULLDEBUG */
      }

#ifdef FULLDEBUG
    } else {
      printf("File %s - without extension\n", pDe->d_name);
#endif   /* FULLDEBUG */
    }
  }             /* while (readdir) ... */

  if (closedir(pDir) != 0) {
    fprintf(stderr, "Directory \"%s", dirName);
    perror("\"");
  }

  return teXTree;
}

#ifdef FULLDEBUG
static void printTree(
  Froot *teXTree
){

  /**
   | Prints all the file names archived in the linked lists (for
   | debugging purposes).
  **/

  Froot *pTT;           /* Running pointer over teXTree elements */

  puts("------------------------------Phase 2: tree printout");
  for (pTT = teXTree;   pTT->extension != 0;   pTT++) {
    Fnode *pTeX;              /* Running pointer over TeX-related files */
    int    nNodes = 0;        /* Counter */

    for (pTeX = pTT->firstNode;   pTeX != 0;   pTeX = pTeX->next) {
      ++nNodes;
      printf("%s%s\n", pTeX->name, pTT->extension);
    }
    printf("  --> %d file%s with extension %s\n", nNodes,
           (nNodes == 1 ? "" : "s"), pTT->extension);
  }
}
#endif   /* FULLDEBUG */

static void examineTree(
  Froot *teXTree,
  char  *dirName
){

  /**
   | Examines the linked lists for this directory doing the effective
   | cleanup.
  **/

  Froot *pTT;           /* Pointer over linked list trees      */
  Fnode *pTeX;          /* Running pointer over the .tex files */

  /**
   | Looks, for all the .tex files, if a corresponding entry with the same
   | name exists (with a different extension) in the other lists; if so,
   | and if its modification time is later than the one of the related
   | .tex file, removes it from the file system.
  **/
#ifdef FULLDEBUG
  puts("------------------------------Phase 3: effective cleanup");
#endif   /* FULLDEBUG */

  for (pTeX = teXTree->firstNode;   pTeX != 0;   pTeX = pTeX->next) {
    char tName[FILENAME_MAX];

    sprintf(tName, "%s/%s.tex", dirName, pTeX->name);
    pTT = teXTree;

#ifdef FULLDEBUG
    printf("    Finding files related to %s:\n", tName);
#endif   /* FULLDEBUG */

    for (pTT++;   pTT->extension != 0;   pTT++) {
      Fnode *pComp;

      for (pComp = pTT->firstNode;   pComp != 0;   pComp = pComp->next) {
        char cName[FILENAME_MAX];

        if (strcmp(pTeX->name, pComp->name) == 0) {
          sprintf(cName, "%s/%s%s", dirName, pTeX->name, pTT->extension);
          pComp->name[0] = '\0';

          if (difftime(pComp->mTime, pTeX->mTime) > 0.0) {
            if (pComp->write == 0) {
              nuke(cName);
            } else {
#ifdef FULLDEBUG
              printf("*** %s readonly; perms are %d***\n", cName, pComp->write);
#endif    /* FULLDEBUG */
              printf("*** %s not removed; it is read only ***\n", cName);
            }
          } else {
            printf("*** %s not removed; %s is newer ***\n", cName, tName);
          }
          break;
        }
      }
    }
  }

  /**
   | If some garbage file has not been deleted, list it
  **/

#ifdef FULLDEBUG
  puts("------------------------------Phase 4: left garbage files");
#endif   /* FULLDEBUG */

  pTT = teXTree;
  for (pTT++;  pTT->extension != 0;  pTT++) {
    Fnode *pComp;

    for (pComp = pTT->firstNode;   pComp != 0;   pComp = pComp->next) {
      if (pComp->name[0] != '\0') {
        char cName[FILENAME_MAX];

        sprintf(cName, "%s/%s%s", dirName, pComp->name, pTT->extension);
        printf("*** %s not removed; no .tex file found ***\n", cName);
      }
    }
  }
}

static void releaseTree(
  Froot *teXTree
){

  /**
   | Cleanup of the file name storage structures: an _array_ of Froot's,
   | terminated by a NULL extension pointer as a sentinel, is assumed.
   | Every linked list nodes is freed; then releaseTree frees also the
   | root structure.
  **/

  Froot *pFR;

#ifdef FULLDEBUG
  puts("------------------------------Phase 5: tree cleanup");
#endif   /* FULLDEBUG */

  for (pFR = teXTree;   pFR->extension != 0;   pFR++) {
    Fnode *pFN, *p;

#ifdef FULLDEBUG
    int nNodes = 0;

    printf("Dealing with extensions %s ...", pFR->extension);
#endif   /* FULLDEBUG */

    for (pFN = pFR->firstNode;   pFN != 0;   pFN = p) {
      p = pFN->next;
      free(pFN);

#ifdef FULLDEBUG
      nNodes++;
#endif   /* FULLDEBUG */
    }

#ifdef FULLDEBUG
    printf("   %d nodes freed\n", nNodes);
#endif   /* FULLDEBUG */
  }

  free(teXTree);
}

static void nuke(
  char *name
){

  /**
   | Removes "name" (the fully qualified file name) from the file system
  **/

#if defined(FULLDEBUG) || defined(DEBUG)
  printf("*** File \"%s\" would have been removed ***\n", name);
#else

  if (confirm) {
    char yn[LONG_ENOUGH], c;

    do {
      printf("Remove %s (y|n) ? ", name);
      if (fgets(yn, LONG_ENOUGH, stdin) == 0) return;
      if (yn[0] == '\0' || (c = tolower((unsigned char) yn[0])) == 'n') {
        return;
      }
    } while (c != 'y');
  }

  if (remove(name) != 0) {
    fprintf(stderr, "File \"%s", name);
    perror("\"");
  } else {
    printf("%s has been removed\n", name);
  }

#endif   /* FULLDEBUG or DEBUG */
}

static char *baseName(
  char *pc
){
  char *p;

/**
 | Strips the (eventual) path information from the string pointed
 | to by 'pc'; if no file name is given, returns an empty string.
**/

  p = strrchr(pc, '/');
  if (p == 0) return pc;
  return ++p;
}

static void syntax()
{
  puts("Usage:");
  printf("  %s [-i] [-r] [-b ext] [dir [dir ... ]]\n", programName);
  puts("Purpose:");
  puts("  removes unneeded TeX auxiliary files and editor backup files from"
       " the");
  puts("  given directories (default: the current directory); the TeX files"
       " are");
  puts("  actually removed only if their modification time is more recent"
       " than");
  puts("  the one of the related TeX source.");
  puts("Options:");
  puts("  -i : asks the user before removing any file;");
  puts("  -r : scans recursively the subdirectories of the given"
       " directories;");
  puts("  -b : \"ext\" is the trailing string identifying editor backup"
       " files");
  puts("       (defaults to \"~\").  -b \"\" avoids any cleanup of special"
       " files.");

  exit(EXIT_SUCCESS);
}
