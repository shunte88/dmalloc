/*
 * test program for malloc code
 *
 * Copyright 1995 by Gray Watson
 *
 * This file is part of the dmalloc package.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * NON-COMMERCIAL purpose and without fee is hereby granted, provided
 * that the above copyright notice and this permission notice appear
 * in all copies, and that the name of Gray Watson not be used in
 * advertising or publicity pertaining to distribution of the document
 * or software without specific, written prior permission.
 *
 * Please see the PERMISSIONS file or contact the author for information
 * about commercial licenses.
 *
 * Gray Watson makes no representations about the suitability of the
 * software described herein for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * The author may be contacted at gray.watson@letters.com
 */

/*
 * Test program for the malloc library.  Current it is interactive although
 * should be script based.
 */

#include <stdio.h>				/* for stdin */

#if HAVE_STRING_H
# include <string.h>
#endif

#include "dmalloc_argv.h"
#include "dmalloc.h"

/*
 * NOTE: these are only needed to test certain features of the library.
 */
#include "conf.h"
#include "debug_val.h"
#include "error_val.h"

#if INCLUDE_RCS_IDS
static	char	*rcs_id =
  "$Id: dmalloc_t.c,v 1.63 1997/12/22 00:32:00 gray Exp $";
#endif

/* external routines */
#if HAVE_SBRK
extern	char		*sbrk(const int incr);	/* to extend the heap */
#endif

#define INTER_CHAR		'i'
#define DEFAULT_ITERATIONS	10000
#define MAX_POINTERS		1024
#define MAX_ALLOC		(1024 * 1024)
#define MIN_AVAIL		10

#define RANDOM_VALUE(x)		((rand() % ((x) * 10)) / 10)

/* pointer tracking structure */
struct pnt_info_st {
  long			pi_crc;			/* crc of storage */
  int			pi_size;		/* size of storage */
  void			*pi_pnt;		/* pnt to storage */
  struct pnt_info_st	*pi_next;		/* pnt to next */
};

typedef struct pnt_info_st pnt_info_t;

static	pnt_info_t	*pointer_grid;

/* argument variables */
static	int		default_iter_n = DEFAULT_ITERATIONS; /* # of iters */
static	char		interactive_b = ARGV_FALSE;	/* interactive flag */
static	char		no_special_b = ARGV_FALSE;	/* no-special flag */
static	int		max_alloc = MAX_ALLOC;		/* amt of mem to use */
static	int		max_pointers = MAX_POINTERS;	/* # of pnts to use */
static	char		random_debug_b = ARGV_FALSE;	/* random flag */
static	char		silent_b = ARGV_FALSE;		/* silent flag */
static	unsigned int	seed_random = 0;		/* random seed */
static	char		verbose_b = ARGV_FALSE;		/* verbose flag */

static	argv_t		arg_list[] = {
  { INTER_CHAR,	"interactive",		ARGV_BOOL,		&interactive_b,
      NULL,			"turn on interactive mode" },
  { 'm',	"max-alloc",		ARGV_INT,		&max_alloc,
      "bytes",			"maximum allocation to test" },
  { 'n',	"no-special",		ARGV_BOOL,		&no_special_b,
      NULL,			"do not run special tests" },
  { 'p',	"max-pointers",		ARGV_INT,		&max_pointers,
      "pointers",		"number of pointers to test" },
  { 'r',	"random-debug",		ARGV_BOOL,	       &random_debug_b,
      NULL,			"randomly change debug flag" },
  { 's',	"silent",		ARGV_BOOL,		&silent_b,
      NULL,			"do not display messages" },
  { 'S',	"seed-random",		ARGV_U_INT,		&seed_random,
      "number",			"seed for random function" },
  { 't',	"times",		ARGV_INT,		&default_iter_n,
      "number",			"number of iterations to run" },
  { 'v',	"verbose",		ARGV_BOOL,		&verbose_b,
      NULL,			"enables verbose messages" },
  { ARGV_LAST }
};

/*
 * Hexadecimal STR to integer translation
 */
static	long	hex_to_long(char *str)
{
  long		ret;
  
  /* strip off spaces */
  for (; *str == ' ' || *str == '\t'; str++) {
  }
  
  /* skip a leading 0[xX] */
  if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X')) {
    str += 2;
  }
  
  for (ret = 0;; str++) {
    if (*str >= '0' && *str <= '9') {
      ret = ret * 16 + (*str - '0');
    }
    else if (*str >= 'a' && *str <= 'f') {
      ret = ret * 16 + (*str - 'a' + 10);
    }
    else if (*str >= 'A' && *str <= 'F') {
      ret = ret * 16 + (*str - 'A' + 10);
    }
    else {
      break;
    }
  }
  
  return ret;
}

/*
 * Read an address from the user
 */
static	void	*get_address(void)
{
  char	line[80];
  void	*pnt;
  
  do {
    (void)printf("Enter a hex address: ");
    if (fgets(line, sizeof(line), stdin) == NULL) {
      return NULL;
    }
  } while (line[0] == '\0');
  
  pnt = (void *)hex_to_long(line);
  
  return pnt;
}

/*
 * Try ITER_N random program iterations, returns 1 on success else 0
 */
static	int	do_random(const int iter_n)
{
  int		iter_c, last, amount, max = max_alloc, flags, free_c;
  pnt_info_t	*free_p, *used_p = NULL;
  pnt_info_t	*pnt_p, *last_p, *this_p;
  
  dmalloc_errno = ERROR_NONE;
  last = 0;
  flags = dmalloc_debug_current();
  
  pointer_grid = (pnt_info_t *)malloc(sizeof(pnt_info_t) * max_pointers);
  if (pointer_grid == NULL) {
    (void)printf("%s: problems allocating space for %d pointer slots.\n",
		 argv_program, max_pointers);
    return 0;
  }
  
  /* initialize free list */
  free_p = pointer_grid;
  for (pnt_p = pointer_grid; pnt_p < pointer_grid + max_pointers; pnt_p++) {
    pnt_p->pi_size = 0;
    pnt_p->pi_pnt = NULL;
    pnt_p->pi_next = pnt_p + 1;
  }
  /* redo the last next pointer */
  (pnt_p - 1)->pi_next = NULL;
  free_c = max_pointers;
  
  for (iter_c = 0; iter_c < iter_n;) {
    int		which;
    
    if (dmalloc_errno != last && ! silent_b) {
      (void)printf("ERROR: iter %d, %s (err %d)\n",
		   iter_c, dmalloc_strerror(dmalloc_errno), dmalloc_errno);
      last = dmalloc_errno;
    }
    
    if (random_debug_b) {
      which = RANDOM_VALUE(sizeof(int) * 8);
      flags ^= 1 << which;
      if (verbose_b) {
	(void)printf("%d: debug flags = %#x\n", iter_c + 1, flags);
      }
      dmalloc_debug(flags);
    }
    
    /* special case when doing non-linear stuff, sbrk took all memory */
    if (max < MIN_AVAIL && free_c == max_pointers) {
      break;
    }
    
    if (free_c < max_pointers && used_p == NULL) {
      (void)fprintf(stderr, "%s: problem with test program free list\n",
		    argv_program);
      exit(1);
    }
    
    /* decide whether to malloc a new pointer or free/realloc an existing */
    which = RANDOM_VALUE(4);
    
    /*
     * < MIN_AVAIL means alloc as long as we have enough memory and
     * there are free slots we do an allocation, else we free
     */
    if (free_c == max_pointers
	|| (free_c > 0 && which < 3 && max >= MIN_AVAIL)) {
      amount = RANDOM_VALUE(max / 2);
#if ALLOW_ALLOC_ZERO_SIZE == 0
      if (amount == 0) {
	amount = 1;
      }
#endif
      
      if (flags & DEBUG_ALLOW_NONLINEAR) {
	which = RANDOM_VALUE(10);
      }
      else {
	which = RANDOM_VALUE(9);
      }
      
      switch (which) {

      case 0: case 1: case 2:
	pnt_p = free_p;
	pnt_p->pi_pnt = calloc(amount, sizeof(char));
	
	if (verbose_b) {
	  (void)printf("%d: calloc %d of max %d into slot %d.  got %#lx\n",
		       iter_c + 1, amount, max, pnt_p - pointer_grid,
		       (long)pnt_p->pi_pnt);
	}
	break;

      case 3: case 4: case 5:
	pnt_p = free_p;
	pnt_p->pi_pnt = malloc(amount);
	
	if (verbose_b) {
	  (void)printf("%d: malloc %d of max %d into slot %d.  got %#lx\n",
		       iter_c + 1, amount, max, pnt_p - pointer_grid,
		       (long)pnt_p->pi_pnt);
	}
	break;
	
      case 6: case 7: case 8:
	if (free_c == max_pointers) {
	  continue;
	}
	
	which = RANDOM_VALUE(max_pointers - free_c);
	for (pnt_p = used_p; which > 0; which--) {
	  pnt_p = pnt_p->pi_next;
	}
	
	pnt_p->pi_pnt = realloc(pnt_p->pi_pnt, amount);
	max += pnt_p->pi_size;
	
	if (verbose_b) {
	  (void)printf("%d: realloc %d from %d of max %d slot %d.  got %#lx\n",
		       iter_c + 1, pnt_p->pi_size, amount, max,
		       pnt_p - pointer_grid, (long)pnt_p->pi_pnt);
	}
	break;
	
      default:
#if HAVE_SBRK
	{
	  void	*mem;
	  
	  mem = sbrk(amount);
	  if (verbose_b) {
	    (void)printf("%d: sbrk'd %d of max %d bytes.  got %#lx\n",
			 iter_c + 1, amount, max, (long)mem);
	  }
	  pnt_p = NULL;
	}
	break;
#else
	iter_c++;
	continue;
#endif
      }
      
      if (pnt_p != NULL) {
	if (pnt_p->pi_pnt == NULL) {
	  if (! silent_b) {
	    (void)printf("allocation of %d returned error on iteration #%d\n",
			 amount, iter_c + 1);
	  }
	  iter_c++;
	  continue;
	}
	
	/* set the size and take it off the free-list and put on used list */
	pnt_p->pi_size = amount;
	
	if (pnt_p == free_p) {
	  free_p = pnt_p->pi_next;
	  pnt_p->pi_next = used_p;
	  used_p = pnt_p;
	  free_c--;
	}
      }
      
      max -= amount;
      iter_c++;
      continue;
    }
    
    /*
     * choose a rand slot to free and make sure it is not a free-slot
     */
    which = RANDOM_VALUE(max_pointers - free_c);
    for (pnt_p = used_p; which > 0; which--) {
      pnt_p = pnt_p->pi_next;
    }
    
    free(pnt_p->pi_pnt);
    
    if (verbose_b) {
      (void)printf("%d: free'd %d bytes from slot %d (%#lx)\n",
		   iter_c + 1, pnt_p->pi_size, pnt_p - pointer_grid,
		   (long)pnt_p->pi_pnt);
    }
    
    pnt_p->pi_pnt = NULL;
    
    /* find pnt in the used list */
    for (this_p = used_p, last_p = NULL; this_p != NULL;
	 last_p = this_p, this_p = this_p->pi_next) {
      if (this_p == pnt_p) {
	break;
      }
    }
    if (last_p == NULL) {
      used_p = pnt_p->pi_next;
    }
    else {
      last_p->pi_next = pnt_p->pi_next;
    }
    
    pnt_p->pi_next = free_p;
    free_p = pnt_p;
    free_c++;
    
    max += pnt_p->pi_size;
    iter_c++;
  }
  
  /* free used pointers */
  for (pnt_p = pointer_grid; pnt_p < pointer_grid + max_pointers; pnt_p++) {
    if (pnt_p->pi_pnt != NULL) {
      free(pnt_p->pi_pnt);
    }
  }
  
  free(pointer_grid);
  
  if (dmalloc_errno == ERROR_NONE) {
    return 1;
  }
  else {
    return 0;
  }
}

/*
 * Do some special tests, returns 1 on success else 0
 */
static	int	check_special(void)
{
  void	*pnt;
  int	hold = dmalloc_errno, ret;
  
  if (! silent_b) {
    (void)printf("The following tests will generate errors:\n");
  }
  
  if (! silent_b) {
    (void)printf("  Trying to realloc a 0L pointer.\n");
  }
  pnt = realloc(NULL, 10);
#if ALLOW_REALLOC_NULL
  if (pnt == NULL) {
    if (! silent_b) {
      (void)printf("   ERROR: re-allocation of 0L returned error.\n");
    }
  }
  else {
    free(pnt);
  }
#else
  if (pnt == NULL) {
    dmalloc_errno = ERROR_NONE;
  }
  else {
    if (! silent_b) {
      (void)printf("   ERROR: re-allocation of 0L did not return error.\n");
    }
    free(pnt);
  }
#endif
  
  if (! silent_b) {
    (void)printf("  Trying to free 0L pointer.\n");
  }
  free(NULL);
#if ALLOW_FREE_NULL
  if (dmalloc_errno != ERROR_NONE && ! silent_b) {
    (void)printf("   ERROR: free of 0L returned error.\n");
  }
#else
  if (dmalloc_errno == ERROR_NONE && (! silent_b)) {
    (void)printf("   ERROR: free of 0L did not return error.\n");
  }
  else {
    dmalloc_errno = ERROR_NONE;
  }
#endif
  
  if (! silent_b) {
    (void)printf("  Allocating a block of too-many bytes.\n");
  }
  pnt = malloc((1 << LARGEST_BLOCK) + 1);
  if (pnt == NULL) {
    dmalloc_errno = ERROR_NONE;
  }
  else {
    if (! silent_b) {
      (void)printf("   ERROR: allocation of > largest allowed size did not return error.\n");
    }
    free(pnt);
  }
  
  if (dmalloc_errno == ERROR_NONE) {
    ret = 1;
  }
  else {
    ret = 0;
  }
  
  if (hold != 0) {
    dmalloc_errno = hold;
  }
  
  return ret;
}

/*
 * Run the interactive section of the program
 */
static	void	do_interactive(void)
{
  int		len;
  char		line[128], *line_p;
  void		*pnt;
  
  (void)printf("Malloc test program.  Type 'help' for assistance.\n");
  
  for (;;) {
    (void)printf("> ");
    if (fgets(line, sizeof(line), stdin) == NULL) {
      break;
    }
    line_p = strchr(line, '\n');
    if (line_p != NULL) {
      *line_p = '\0';
    }
    
    len = strlen(line);
    if (len == 0) {
      continue;
    }
    
    if (strncmp(line, "?", len) == 0
	|| strncmp(line, "help", len) == 0) {
      (void)printf("\thelp      - print this message\n\n");
      
      (void)printf("\tmalloc    - allocate memory\n");
      (void)printf("\tcalloc    - allocate/clear memory\n");
      (void)printf("\trealloc   - reallocate memory\n");
#if HAVE_STRDUP
      (void)printf("\tstrdup    - allocate a string\n");
#endif
      (void)printf("\tfree      - deallocate memory\n\n");
      
      (void)printf("\tmap       - map the heap to the logfile\n");
      (void)printf("\tstats     - dump heap stats to the logfile\n");
      (void)printf("\tunfreed   - list the unfree memory to the logfile\n\n");
      
      (void)printf("\tverify    - check out a memory address (or all heap)\n");
      (void)printf("\toverwrite - overwrite some memory to test errors\n");
#if HAVE_SBRK
      (void)printf("\tsbrk       - call sbrk to test external areas\n\n");
#endif
      
      (void)printf("\trandom    - randomly execute a number of [de] allocs\n");
      (void)printf("\tspecial   - run some special tests\n\n");
      
      (void)printf("\tquit      - quit this test program\n");
      continue;
    }
    
    if (strncmp(line, "quit", len) == 0) {
      break;
    }
    
    if (strncmp(line, "malloc", len) == 0) {
      int	size;
      
      (void)printf("How much to malloc: ");
      if (fgets(line, sizeof(line), stdin) == NULL) {
	break;
      }
      size = atoi(line);
      (void)printf("malloc(%d) returned '%#lx'\n", size, (long)malloc(size));
      continue;
    }
    
    if (strncmp(line, "calloc", len) == 0) {
      int	size;
      
      (void)printf("How much to calloc: ");
      if (fgets(line, sizeof(line), stdin) == NULL) {
	break;
      }
      size = atoi(line);
      (void)printf("calloc(%d) returned '%#lx'\n",
		   size, (long)calloc(size, sizeof(char)));
      continue;
    }
    
    if (strncmp(line, "realloc", len) == 0) {
      int	size;
      
      pnt = get_address();
      
      (void)printf("How much to realloc: ");
      if (fgets(line, sizeof(line), stdin) == NULL) {
	break;
      }
      size = atoi(line);
      
      (void)printf("realloc(%#lx, %d) returned '%#lx'\n",
		   (long)pnt, size, (long)realloc(pnt, size));
      
      continue;
    }
    
#if HAVE_STRDUP
    if (strncmp(line, "strdup", len) == 0) {
      (void)printf("Enter a string to strdup: ");
      if (fgets(line, sizeof(line), stdin) == NULL) {
	break;
      }
      (void)printf("strdup returned '%#lx'\n", (long)strdup(line));
      continue;
    }
#endif
    
    if (strncmp(line, "free", len) == 0) {
      pnt = get_address();
      free(pnt);
      continue;
    }
    
    if (strncmp(line, "map", len) == 0) {
      dmalloc_log_heap_map();
      (void)printf("Done.\n");
      continue;
    }
    
    if (strncmp(line, "stats", len) == 0) {
      dmalloc_log_stats();
      (void)printf("Done.\n");
      continue;
    }
    
    if (strncmp(line, "unfreed", len) == 0) {
      dmalloc_log_unfreed();
      (void)printf("Done.\n");
      continue;
    }
    
    if (strncmp(line, "overwrite", len) == 0) {
      char	*overwrite = "OVERWRITTEN";
      
      pnt = get_address();
      memcpy((char *)pnt, overwrite, strlen(overwrite));
      (void)printf("Done.\n");
      continue;
    }
    
#if HAVE_SBRK
    /* call sbrk directly */
    if (strncmp(line, "sbrk", len) == 0) {
      int	size;
      
      (void)printf("How much to sbrk: ");
      if (fgets(line, sizeof(line), stdin) == NULL) {
	break;
      }
      size = atoi(line);
      (void)printf("sbrk(%d) returned '%#lx'\n", size, (long)sbrk(size));
      continue;
    }
#endif
    
    /* do random heap hits */
    if (strncmp(line, "random", len) == 0) {
      int	iter_n;
      
      (void)printf("How many iterations[%d]: ", default_iter_n);
      if (fgets(line, sizeof(line), stdin) == NULL) {
	break;
      }
      if (line[0] == '\0' || line[0] == '\n') {
	iter_n = default_iter_n;
      }
      else {
	iter_n = atoi(line);
      }
      
      if (do_random(iter_n)) {
	(void)printf("It succeeded.\n");
      }
      else {
	(void)printf("It failed.\n");
      }
      
      continue;
    }
    
    /* do special checks */
    if (strncmp(line, "special", len) == 0) {
      if (check_special()) {
	(void)printf("It succeeded.\n");
      }
      else {
	(void)printf("It failed.\n");
      }
      
      continue;
    }
    
    if (strncmp(line, "verify", len) == 0) {
      int	ret;
      
      (void)printf("If the address is 0, verify will check the whole heap.\n");
      pnt = get_address();
      ret = malloc_verify((char *)pnt);
      (void)printf("malloc_verify(%#lx) returned '%s'\n",
		   (long)pnt,
		   (ret == DMALLOC_VERIFY_NOERROR ? "success" : "failure"));
      continue;
    }
    
    (void)printf("Unknown command '%s'.  Type 'help' for assistance.\n", line);
  }
}

int	main(int argc, char **argv)
{
  int	ret;
  
  argv_process(arg_list, argc, argv);
  
  if (silent_b && (verbose_b || interactive_b)) {
    silent_b = ARGV_FALSE;
  }
  
  /* repeat until we get a non 0 seed */
  while (seed_random == 0) {
    seed_random = time(0) ^ 0xDEABEEF;
  }
  (void)srand(seed_random);
  
  if (! silent_b) {
    (void)printf("Random seed = %u\n", seed_random);
  }
  
  if (interactive_b) {
    do_interactive();
  }
  else {
    if (! silent_b) {
      (void)printf("Running %d tests (use -%c for interactive)...\n",
		   default_iter_n, INTER_CHAR);
    }
    (void)fflush(stdout);
    
    ret = do_random(default_iter_n);
    if (! silent_b) {
      (void)printf("%s.\n", (ret == 1 ? "Succeeded" : "Failed"));
    }
  }
  
  /* don't allow silent dumps */
  if (! no_special_b
      && (! (silent_b && (dmalloc_debug_current() & DEBUG_ERROR_ABORT)))) {
    if (! silent_b) {
      (void)printf("Running special tests...\n");
    }
    ret = check_special();
    if (! silent_b) {
      (void)printf("%s.\n", (ret == 1 ? "Succeeded" : "Failed"));
    }
  }
  
  /* you will need this if you can't auto-shutdown */
#if HAVE_ATEXIT == 0 && HAVE_ON_EXIT == 0 && FINI_DMALLOC == 0
  /* shutdown the alloc routines */
  malloc_shutdown();
#endif
  
  argv_cleanup(arg_list);
  
  ret = malloc_verify(NULL);
  if (! silent_b) {
    (void)printf("Final malloc_verify returned '%s'\n",
		 (ret == DMALLOC_VERIFY_NOERROR ? "success" : "failure"));
  }
  
  if (dmalloc_errno == ERROR_NONE) {
    exit(0);
  }
  else {
    exit(1);
  }
}
