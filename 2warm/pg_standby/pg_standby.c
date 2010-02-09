/*
 * $PostgreSQL$
 *
 *
 * pg_standby.c
 *
 * Production-ready example of how to create a Warm Standby
 * database server using continuous archiving as a
 * replication mechanism
 *
 * We separate the parameters for archive and nextWALfile
 * so that we can check the archive exists, even if the
 * WAL file doesn't (yet).
 *
 * This program will be executed once in full for each file
 * requested by the warm standby server.
 *
 * It is designed to cater to a variety of needs, as well
 * providing a customizable section.
 *
 * Original author:		Simon Riggs  simon@2ndquadrant.com
 * Current maintainer:	Greg Smith   greg@2ndquadrant.com
 *
 * Enhancements in this version:
 * 
 * Logging:
 *   - Logging level controllable
 *   - Logging about current contents of archive
 *
 * Reliability/performance improvements:
 *   - WAL filename consistency checking
 *   - Link feature completely removed
 *   - History files are never waited upon
 *   - Ignores 00000001.history file absence without throwing an error
 *
 * Backwards compatibility:
 *   - %r compatibility, use of that string doesn't throw an error
 *   - Files to keep (-k) defaults to 256 if %r not used, as is the case
 *     on a 8.2 server
 *   - Compatibility with all server versions from 8.2 to 9.0
 *  
 */
#include "postgres_fe.h"

#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#ifdef WIN32
int			getopt(int argc, char *const argv[], const char *optstring);
#else
#include <sys/time.h>
#include <unistd.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#endif   /* ! WIN32 */

extern char *optarg;
extern int	optind;

const char *progname;

/* Options and defaults */
int			sleeptime = 5;		/* amount of time to sleep between file checks */
int			waittime = -1;		/* how long we have been waiting, -1 no wait
								 * yet */
int			maxwaittime = 0;	/* how long are we prepared to wait for? */
int			keepfiles = 0;		/* number of WAL files to keep, 0 keep all */
int			maxretries = 3;		/* number of retries on restore command */
bool		need_cleanup = false;		/* do we need to remove files from
										 * archive? */

#ifndef WIN32
static volatile sig_atomic_t signaled = false;
#endif

char	   *archiveLocation;	/* where to find the archive? */
char	   *triggerPath;		/* where to find the trigger file? */
char	   *xlogFilePath;		/* where we are going to restore to */
char	   *nextWALFileName;	/* the file we need to get from archive */
char	   *restartWALFileName; /* the file from which we can restart restore */
char	   *priorWALFileName;	/* the file we need to get from archive */
char		WALFilePath[MAXPGPATH];		/* the file path including archive */
char		restoreCommand[MAXPGPATH];	/* run this to restore */
char		exclusiveCleanupFileName[MAXPGPATH];		/* the file we need to
														 * get from archive */

/*
 * Two types of failover are supported (smart and fast failover).
 *
 * The content of the trigger file determines the type of failover. If the
 * trigger file contains the word "smart" (or the file is empty), smart
 * failover is chosen: pg_standby acts as cp or ln command itself, on
 * successful completion all the available WAL records will be applied
 * resulting in zero data loss. But, it might take a long time to finish
 * recovery if there's a lot of unapplied WAL.
 *
 * On the other hand, if the trigger file contains the word "fast", the
 * recovery is finished immediately even if unapplied WAL files remain. Any
 * transactions in the unapplied WAL files are lost.
 *
 * An empty trigger file performs smart failover. SIGUSR or SIGINT triggers
 * fast failover. A timeout causes fast failover (smart failover would have
 * the same effect, since if the timeout is reached there is no unapplied WAL).
 */
#define NoFailover		0
#define SmartFailover	1
#define FastFailover	2

static int	Failover = NoFailover;


#define XLOG_DATA			 0
#define XLOG_HISTORY		 1
#define XLOG_BACKUP_LABEL	 2
int			nextWALFileType;

#define SET_RESTORE_COMMAND(cmd, arg1, arg2) \
	snprintf(restoreCommand, MAXPGPATH, cmd " \"%s\" \"%s\"", arg1, arg2)

struct stat stat_buf;

/*
 * Provide a simple implementation of an elog interface, to make logging in
 * this program read similarly to the rest of the backend source.  This
 * borrows and matches as closely as possible the interfaces in elog.h and
 * the simple logging implementation of ipc_test.c in hopes that a future
 * change to use standard backend logging would require minimal code changes,
 * even though that pulls a bit of extra baggage into here.
 */

#define DEBUG1      0
#define LOG         1
#define INFO        2
#define NOTICE      3
#define WARNING     4
#define ERROR       5

/*
 * Figure out how to extract the name of the current function from this
 * compiler, given that there are a couple of "standards" for that
 */
#ifdef HAVE_FUNCNAME__FUNC
#define PG_FUNCNAME_MACRO   __func__
#else
#ifdef HAVE_FUNCNAME__FUNCTION
#define PG_FUNCNAME_MACRO   __FUNCTION__
#else
#define PG_FUNCNAME_MACRO   NULL
#endif
#endif


#define elog    elog_start(__FILE__, __LINE__, PG_FUNCNAME_MACRO), elog_finish
extern void elog_start(const char *filename, int lineno, const char *funcname);
extern void elog_finish(int elevel, const char *fmt,...)
/* This extension allows gcc to check the format string for consistency with
   the supplied arguments. */
__attribute__((format(printf, 2, 3)));
extern void elog_flush(void);

static int verbosity = INFO;
static bool timestamplogs = false;

void
elog_start(const char *filename, int lineno, const char *funcname)
{
}

void
elog_finish(int elevel, const char *fmt,...)
{
	if (elevel >= verbosity)
	{
		if (timestamplogs)
		{
			/* XXX TODO Add timestamps to the front of the output line */
		}
		fprintf(stderr, "%s", fmt);
	}
}

/*
 * For situations where it's deemed important to do so, allow explicitly
 * flushing the log file.  Since many of those involve exiting the program,
 * a more general log cleanup routine might be an improvement over this
 * interface.
 */
void
elog_flush()
{
	fflush(stderr);
}

/* =====================================================================
 *
 *		  Customizable section
 *
 * =====================================================================
 *
 *	Currently, this section assumes that the Archive is a locally
 *	accessible directory. If you want to make other assumptions,
 *	such as using a vendor-specific archive and access API, these
 *	routines are the ones you'll need to change. You're
 *	enouraged to submit any changes to pgsql-hackers@postgresql.org
 *	or personally to the current maintainer. Those changes may be
 *	folded in to later versions of this program.
 */

#define XLOG_DATA_FNAME_LEN		24
#define XLOG_BACKUP_FNAME_LEN	31

#define XLOG_FNAME_CHARS "0123456789ABCDEF"
/* Reworked from access/xlog_internal.h */
#define XLogFileName(fname, tli, log, seg)	\
	snprintf(fname, XLOG_DATA_FNAME_LEN + 1, "%08X%08X%08X", tli, log, seg)

/*
 * Do a basic sanity check that a given file name looks like it could be
 * a valid WAL segment
 */
static bool
ValidWALFileName(char *fname)
{
	return (
		strlen(fname) == XLOG_DATA_FNAME_LEN &&
		strspn(fname, XLOG_FNAME_CHARS) == XLOG_DATA_FNAME_LEN
		);
}

/*
 *	Initialize allows customized commands into the warm standby program.
 *
 *	As an example, and probably the common case, we use either
 *	the cp command on *nix or the copy command on Windows.
 */
static void
CustomizableInitialize(void)
{
#ifdef WIN32
	snprintf(WALFilePath, MAXPGPATH, "%s\\%s", archiveLocation, nextWALFileName);
	SET_RESTORE_COMMAND("copy", WALFilePath, xlogFilePath);
#else
	snprintf(WALFilePath, MAXPGPATH, "%s/%s", archiveLocation, nextWALFileName);
	SET_RESTORE_COMMAND("cp", WALFilePath, xlogFilePath);
#endif

	/*
	 * This code assumes that archiveLocation is a directory.  You may wish to
	 * add code to check for tape libraries, etc.. So, since it is a
	 * directory, we use stat to test if its accessible
	 */
	if (stat(archiveLocation, &stat_buf) != 0)
	{
		fprintf(stderr, "%s: archiveLocation \"%s\" does not exist\n", progname, archiveLocation);
		fflush(stderr);
		exit(2);
	}
}

/*
 * CustomizableNextWALFileReady()
 *
 *	  Is the requested file ready yet?
 */
static bool
CustomizableNextWALFileReady()
{
	if (stat(WALFilePath, &stat_buf) == 0)
	{
		/*
		 * If it's a backup file, return immediately
		 */
		if (strlen(nextWALFileName) == XLOG_BACKUP_FNAME_LEN &&
			strspn(nextWALFileName, XLOG_FNAME_CHARS) == XLOG_DATA_FNAME_LEN &&
		strcmp(nextWALFileName + strlen(nextWALFileName) - strlen(".backup"),
			   ".backup") == 0)
		{
			nextWALFileType = XLOG_BACKUP_LABEL;
			return true;
		}
		
		/*
		 * If it's a regular file, return only if its the right size already
		 */		
		else if (stat_buf.st_size == XLOG_SEG_SIZE)
		{
#ifdef WIN32

			/*
			 * Windows 'cp' sets the final file size before the copy is
			 * complete, and not yet ready to be opened by pg_standby. So we
			 * wait for sleeptime secs before attempting to restore. If that
			 * is not enough, we will rely on the retry/holdoff mechanism.
			 * GNUWin32's cp does not have this problem.
			 */
			pg_usleep(sleeptime * 1000000L);
#endif
			nextWALFileType = XLOG_DATA;
			return true;
		}
		
		/*
		 * Files that are too large can never be processed here
		 */
		if (stat_buf.st_size > XLOG_SEG_SIZE)
		{
		 	/* XXX Not sure if this "long int" conversion is legit */
			elog(ERROR, "file size %lu greater than expected %u\n", 
				(long int) stat_buf.st_size, XLOG_SEG_SIZE);
			elog_flush();
			exit(3);
		}

		/*
		 * If still too small, exit to wait until it is the correct size
		 */		
	}

	return false;
}

#define MaxSegmentsPerLogFile ( 0xFFFFFFFF / XLOG_SEG_SIZE )

static bool
CustomizableCleanupPriorWALFiles(void)
{
	int nremoved = 0;

	if (!need_cleanup)
		return false;

	/*
	 * Work out name of prior file from current filename
	 */
	if (nextWALFileType == XLOG_DATA)
	{
		int			rc;
		DIR		   *xldir;
		struct dirent *xlde;

		/*
		 * Assume its OK to keep failing. The failure situation may change
		 * over time, so we'd rather keep going on the main processing than
		 * fail because we couldn't clean up yet.
		 */
		if ((xldir = opendir(archiveLocation)) != NULL)
		{
			while ((xlde = readdir(xldir)) != NULL)
			{
				/*
				 * We ignore the timeline part of the XLOG segment identifiers
				 * in deciding whether a segment is still needed.  This
				 * ensures that we won't prematurely remove a segment from a
				 * parent timeline. We could probably be a little more
				 * proactive about removing segments of non-parent timelines,
				 * but that would be a whole lot more complicated.
				 *
				 * We use the alphanumeric sorting property of the filenames
				 * to decide which ones are earlier than the
				 * exclusiveCleanupFileName file. Note that this means files
				 * are not removed in the order they were originally written,
				 * in case this worries you.
				 */
				if (strlen(xlde->d_name) == XLOG_DATA_FNAME_LEN &&
					strspn(xlde->d_name, XLOG_FNAME_CHARS) == XLOG_DATA_FNAME_LEN &&
				  strcmp(xlde->d_name + 8, exclusiveCleanupFileName + 8) < 0)
				{
#ifdef WIN32
					snprintf(WALFilePath, MAXPGPATH, "%s\\%s", archiveLocation, xlde->d_name);
#else
					snprintf(WALFilePath, MAXPGPATH, "%s/%s", archiveLocation, xlde->d_name);
#endif

					elog(NOTICE, "\nremoving \"%s\"", WALFilePath);

					rc = unlink(WALFilePath);
					if (rc != 0)
					{
						elog(ERROR, "\n%s: ERROR failed to remove \"%s\": %s",
							progname, WALFilePath, strerror(errno));
						break;
					}
					nremoved++;
				}
			}
			elog(NOTICE, "\n");
		}
		else
			elog(ERROR, "%s: archiveLocation \"%s\" open error\n", progname, archiveLocation);

		closedir(xldir);
		elog_flush();
	}

	if (nremoved > 0)
		return true;
	else
		return false;
}

/* =====================================================================
 *		  End of Customizable section
 * =====================================================================
 */

/*
 * SetWALFileNameForCleanup()
 *
 *	  Set the earliest WAL filename that we want to keep on the archive
 *	  and decide whether we need_cleanup
 */
static bool
SetWALFileNameForCleanup(void)
{
	uint32		tli = 1,
				log = 0,
				seg = 0;
	uint32		log_diff = 0,
				seg_diff = 0;
	bool		cleanup = false;

	if (restartWALFileName)
	{
		/*
		 * Don't do cleanup if the restartWALFileName provided is later than
		 * the xlog file requested. This is an error and we must not remove
		 * these files from archive. This shouldn't happen, but better safe
		 * than sorry.
		 */
		if (strcmp(restartWALFileName, nextWALFileName) > 0)
			return false;

		strcpy(exclusiveCleanupFileName, restartWALFileName);
		return true;
	}

	if (keepfiles > 0)
	{
		sscanf(nextWALFileName, "%08X%08X%08X", &tli, &log, &seg);
		if (tli > 0 && log >= 0 && seg > 0)
		{
			log_diff = keepfiles / MaxSegmentsPerLogFile;
			seg_diff = keepfiles % MaxSegmentsPerLogFile;
			if (seg_diff > seg)
			{
				log_diff++;
				seg = MaxSegmentsPerLogFile - (seg_diff - seg);
			}
			else
				seg -= seg_diff;

			if (log >= log_diff)
			{
				log -= log_diff;
				cleanup = true;
			}
			else
			{
				log = 0;
				seg = 0;
			}
		}
	}

	XLogFileName(exclusiveCleanupFileName, tli, log, seg);

	return cleanup;
}

/*
 * CheckForExternalTrigger()
 *
 *	  Is there a trigger file? Sets global 'Failover' variable to indicate
 *	  what kind of a trigger file it was. A "fast" trigger file is turned
 *	  into a "smart" file as a side-effect.
 */
static void
CheckForExternalTrigger(void)
{
	char		buf[32];
	int			fd;
	int			len;

	/*
	 * Look for a trigger file, if that option has been selected
	 *
	 * We use stat() here because triggerPath is always a file rather than
	 * potentially being in an archive
	 */
	if (!triggerPath || stat(triggerPath, &stat_buf) != 0)
		return;

	/*
	 * An empty trigger file performs smart failover. There's a little race
	 * condition here: if the writer of the trigger file has just created the
	 * file, but not yet written anything to it, we'll treat that as smart
	 * shutdown even if the other process was just about to write "fast" to
	 * it. But that's fine: we'll restore one more WAL file, and when we're
	 * invoked next time, we'll see the word "fast" and fail over immediately.
	 */
	if (stat_buf.st_size == 0)
	{
		Failover = SmartFailover;
		elog(WARNING,"trigger file found: smart failover\n");
		elog_flush();
		return;
	}

	if ((fd = open(triggerPath, O_RDWR, 0)) < 0)
	{
		elog(WARNING,"WARNING: could not open \"%s\": %s\n",
				triggerPath, strerror(errno));
		elog_flush();
		return;
	}

	if ((len = read(fd, buf, sizeof(buf))) < 0)
	{
		elog(WARNING,"WARNING: could not read \"%s\": %s\n",
				triggerPath, strerror(errno));
		elog_flush();
		close(fd);
		return;
	}
	buf[len] = '\0';

	if (strncmp(buf, "smart", 5) == 0)
	{
		Failover = SmartFailover;
		elog(WARNING, "trigger file found: smart failover\n");
		elog_flush();
		close(fd);
		return;
	}

	if (strncmp(buf, "fast", 4) == 0)
	{
		Failover = FastFailover;

		elog(WARNING, "trigger file found: fast failover\n");
		elog_flush();

		/*
		 * Turn it into a "smart" trigger by truncating the file. Otherwise if
		 * the server asks us again to restore a segment that was restored
		 * already, we would return "not found" and upset the server.
		 */
		if (ftruncate(fd, 0) < 0)
		{
			elog(WARNING, "WARNING: could not read \"%s\": %s\n",
					triggerPath, strerror(errno));
			elog_flush();
		}
		close(fd);

		return;
	}
	close(fd);

	elog(WARNING, "WARNING: invalid content in \"%s\"\n", triggerPath);
	elog_flush();
	return;
}

/*
 * RestoreWALFileForRecovery()
 *
 *	  Perform the action required to restore the file from archive
 */
static bool
RestoreWALFileForRecovery(void)
{
	int			rc = 0;
	int			numretries = 0;

	elog(NOTICE, "running restore		:");
	elog_flush();

	while (numretries <= maxretries)
	{
		rc = system(restoreCommand);
		if (rc == 0)
		{
			elog(NOTICE, " OK\n");
			elog_flush();
			return true;
		}
		pg_usleep(numretries++ * sleeptime * 1000000L);
	}

	/*
	 * Allow caller to add additional info
	 */
	elog(DEBUG1, "not restored\n");
	return false;
}

static void
usage(void)
{
	printf("%s allows PostgreSQL warm standby servers to be configured.\n", progname);
	printf("Compatible with PostgreSQL 8.2, 8.3, 8.4 and 9.0\n\n");
	printf("Usage:\n");
	printf("  %s [OPTION]... ARCHIVELOCATION NEXTWALFILE XLOGFILEPATH [RESTARTWALFILE]\n", progname);
	printf("\n"
		"with main intended use as a restore_command in the recovery.conf:\n"
		   "  restore_command = 'pg_standby [OPTION]... ARCHIVELOCATION %%f %%p %%r'\n"
		   "e.g.\n"
		   "  restore_command = 'pg_standby -l /mnt/server/archiverdir %%f %%p %%r'\n");
	printf("\nOptions:\n");
	printf("  -k NUMFILESTOKEEP  if RESTARTWALFILE not used, removes files prior to limit\n"
		   "                     (0 keeps all, default=256)\n");
	printf("  -r MAXRETRIES      max number of times to retry, with progressive wait\n"
		   "                     (default=3)\n");
	printf("  -s SLEEPTIME       seconds to wait between file checks (min=1, max=60,\n"
		   "                     default=5)\n");
	printf("  -t TRIGGERFILE     defines a trigger file to initiate failover (no default, can use a signal instead on some platforms)\n");
	printf("  -w MAXWAITTIME     max seconds to wait for a file (0=no limit) (default=0)\n");
	printf("  -T                 Turn on timestamps on the output log entries (default is off)\n");	
	printf("  -v VERBOSITY       verbosity of logging output (0-5, higher is more verbose, default = 2)\n");
	printf("  --help             show this help, then exit\n");
	printf("  --version          output version information, then exit\n");	
	printf("\nDeprecated options:\n");
	printf("  -c                 copies file from archive (default and only option)\n");
	printf("  -l                 does nothing; use of link is now deprecated\n");	
	printf("  -d                 generate lots of debugging output (same as verbosity=5)\n");
	printf("\nReport bugs to <simon@2ndQuadrant.com>.\n");
}

#ifndef WIN32
static void
sighandler(int sig)
{
	signaled = true;
}

/* We don't want SIGQUIT to core dump */
static void
sigquit_handler(int sig)
{
	signal(SIGINT, SIG_DFL);
	kill(getpid(), SIGINT);
}
#endif

/*------------ MAIN ----------------------------------------*/
int
main(int argc, char **argv)
{
	int			c;

	progname = get_progname(argv[0]);

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage();
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			puts("pg_standby (PostgreSQL) " PG_VERSION " enhanced by 2ndQuadrant r1.1");
			exit(0);
		}
	}

#ifndef WIN32
	/*
	 * You can send SIGUSR1 to trigger failover.
	 *
	 * Postmaster uses SIGQUIT to request immediate shutdown. The default
	 * action is to core dump, but we don't want that, so trap it and commit
	 * suicide without core dump.
	 *
	 * We used to use SIGINT and SIGQUIT to trigger failover, but that turned
	 * out to be a bad idea because postmaster uses SIGQUIT to request
	 * immediate shutdown. We still trap SIGINT, but that may change in a
	 * future release.
	 *
	 * There's no way to trigger failover via signal on Windows.
	 */
	(void) signal(SIGUSR1, sighandler);
	(void) signal(SIGINT, sighandler);	/* deprecated, use SIGUSR1 */
	(void) signal(SIGQUIT, sigquit_handler);
#endif

	while ((c = getopt(argc, argv, "cdk:lr:s:t:w:v:T")) != -1)
	{
		switch (c)
		{
			case 'l':			/* Use copy always; accept these parameter for backwards compatibility */
			case 'c':
				break;
			case 'd':			/* Debug mode */
				verbosity = DEBUG1;
				break;
			case 'k':			/* keepfiles */
				keepfiles = atoi(optarg);
				if (keepfiles < 0)
				{
					fprintf(stderr, "%s: -k keepfiles must be >= 0\n", progname);
					exit(2);
				}
				break;
			case 'r':			/* Retries */
				maxretries = atoi(optarg);
				if (maxretries < 0)
				{
					fprintf(stderr, "%s: -r maxretries must be >= 0\n", progname);
					exit(2);
				}
				break;
			case 's':			/* Sleep time */
				sleeptime = atoi(optarg);
				if (sleeptime <= 0 || sleeptime > 60)
				{
					fprintf(stderr, "%s: -s sleeptime incorrectly set\n", progname);
					exit(2);
				}
				break;
			case 't':			/* Trigger file */
				triggerPath = optarg;
				break;
			case 'w':			/* Max wait time */
				maxwaittime = atoi(optarg);
				if (maxwaittime < 0)
				{
					fprintf(stderr, "%s: -w maxwaittime incorrectly set\n", progname);
					exit(2);
				}
				break;
			case 'v':			/* Verbosity */				
				/*
				 * Verbosity inputs here have 5=most verbose, which is inverted
				 * from how they're represented internally (to look more like
				 * the standard elog values).  Inside the code, 0=most
				 * verbose.  Invert the input value to convert between the
				 * two scales, then do error checking against the internal
				 * representation.
				 */

				verbosity = atoi(optarg);
				verbosity = ERROR - verbosity;								
				if (verbosity < DEBUG1 || verbosity > ERROR)
				{
					fprintf(stderr, "%s: -v verbosity incorrectly set\n", progname);
					exit(2);
				}
				break;
			case 'T':			/* Enable timestamps in log files */
				timestamplogs = true;
				break;
			default:
				fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
				exit(2);
				break;
		}
	}

	/*
	 * Parameter checking - after checking to see if trigger file present
	 */
	if (argc == 1)
	{
		fprintf(stderr, "%s: not enough command-line arguments\n", progname);
		exit(2);
	}

	/*
	 * We will go to the archiveLocation to get nextWALFileName.
	 * nextWALFileName may not exist yet, which would not be an error, so we
	 * separate the archiveLocation and nextWALFileName so we can check
	 * separately whether archiveLocation exists, if not that is an error
	 */
	if (optind < argc)
	{
		archiveLocation = argv[optind];
		optind++;
	}
	else
	{
		fprintf(stderr, "%s: must specify archive location\n", progname);
		fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
		exit(2);
	}

	if (optind < argc)
	{
		nextWALFileName = argv[optind];
		if (!ValidWALFileName(nextWALFileName))
		{
			fprintf(stderr, "%s: invalid NEXTWALFILENAME\n", progname);
			fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
			exit(2);
		}
		optind++;
	}
	else
	{
		fprintf(stderr, "%s: use %%f to specify nextWALFileName\n", progname);
		fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
		exit(2);
	}

	if (optind < argc)
	{
		xlogFilePath = argv[optind];
		optind++;
	}
	else
	{
		fprintf(stderr, "%s: use %%p to specify xlogFilePath\n", progname);
		fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
		exit(2);
	}

	if (optind < argc)
	{
		/*
		 * If %r is specified, that suggests we are running PostgreSQL 8.2.
		 * In 8.3 or above, the %r would have been replaced with a
		 * WAL file name. Similarly check for %R and use same behavior.
		 */
		if (strcmp(argv[optind], "%r") == 0 || strcmp(argv[optind], "%R") == 0)
		{
			if (keepfiles == 0)
				keepfiles = 256;		/* 4 GB of WAL files */
		}
		else
		{
			restartWALFileName = argv[optind];
			if (!ValidWALFileName(restartWALFileName))
			{
				fprintf(stderr, "%s: invalid RESTARTWALFILENAME\n", progname);
				fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
				exit(2);
			}
		}
		optind++;
	}

	CustomizableInitialize();

	need_cleanup = SetWALFileNameForCleanup();

	elog(NOTICE, "Trigger file 		: %s\n", triggerPath ? triggerPath : "<not set>");
	elog(NOTICE, "Waiting for WAL file	: %s\n", nextWALFileName);
	elog(NOTICE, "WAL file path		: %s\n", WALFilePath);
	elog(NOTICE, "Restoring to		: %s\n", xlogFilePath);
	elog(NOTICE, "Sleep interval		: %d second%s\n",
			sleeptime, (sleeptime > 1 ? "s" : " "));
	elog(NOTICE, "Max wait interval	: %d %s\n",
			maxwaittime, (maxwaittime > 0 ? "seconds" : "forever"));
	elog(NOTICE, "Command for restore	: %s\n", restoreCommand);
	elog(NOTICE, "Keep archive history	: ");
	if (need_cleanup)
		elog(NOTICE, "%s and later\n", exclusiveCleanupFileName);
	else
		elog(NOTICE, "No cleanup required\n");
	elog_flush();

	/*
	 * Check for initial history file: always the first file to be requested
	 * It's OK if the file isn't there - all other files need to wait
	 */
	 
	/* XXX These comparisons with 8 seem weird.  Should use same full-length
	   comparison adopted by the rest of the code now. */
	if (strlen(nextWALFileName) > 8 &&
		strspn(nextWALFileName, XLOG_FNAME_CHARS) == 8 &&
		strcmp(nextWALFileName + strlen(nextWALFileName) - strlen(".history"),
			   ".history") == 0)
	{
		nextWALFileType = XLOG_HISTORY;
		
		/*
		 * Validate the history file exists before trying to restore it.
		 * If it's not there already, it's not expected to ever show up.
		 * Exit rather than wasting time waiting on it on the restore loop.
         */
		if (stat(nextWALFileName, &stat_buf) != 0) {
			elog(NOTICE,"Optional requested history file %s was not found\n",
				nextWALFileName);
			elog_flush();
			exit(1);
		}
		
		if (RestoreWALFileForRecovery())
			exit(0);
		else
		{
			/*
			 * Skip error message if the first history file is not available,
			 * since it's mistakenly asked for but never available in any
			 * server version < 9.0.
			 * XXX If the stat change above stays, this check is redundant
			 * because that will exit before reaching this point.
			 */
			if (strcmp(nextWALFileName,  "00000001.history") != 0)
			{
				elog(ERROR, "history file not found\n");
				elog_flush();
			}
			exit(1);
		}
	}

	/*
	 * Main wait loop
	 */
	for (;;)
	{
		/* Check for trigger file or signal first */
		CheckForExternalTrigger();
#ifndef WIN32
		if (signaled)
		{
			Failover = FastFailover;
			elog(ERROR, "signaled to exit: fast failover\n");
			elog_flush();
		}
#endif

		/*
		 * Check for fast failover immediately, before checking if the
		 * requested WAL file is available
		 */
		if (Failover == FastFailover)
			exit(1);

		if (CustomizableNextWALFileReady())
		{
			/*
			 * Once we have restored this file successfully, we can remove some
			 * prior WAL files. If this restore fails we musn't remove any
			 * file, because some of them will be requested again immediately
			 * after the failed restore, or when we restart recovery.
			 */
			if (RestoreWALFileForRecovery())
			{
				if (CustomizableCleanupPriorWALFiles())
				    elog(INFO, "Archive retains WAL file %s and later\n", exclusiveCleanupFileName);

				exit(0);
			}
			else
			{
				/* Something went wrong in copying the file */
				exit(1);
			}
		}

		/* Check for smart failover if the next WAL file was not available */
		if (Failover == SmartFailover)
			exit(1);

		if (sleeptime <= 60)
			pg_usleep(sleeptime * 1000000L);

		waittime += sleeptime;
		if (waittime >= maxwaittime && maxwaittime > 0)
		{
			Failover = FastFailover;
			elog(ERROR, "Timed out after %d seconds: fast failover\n",
					waittime);
			elog_flush();
		}

		elog(NOTICE, "WAL file not present yet.");
		if (triggerPath)
			elog(NOTICE, " Checking for trigger file...");
		elog(NOTICE,"\n");
		elog(INFO,".");
		elog_flush();
	}
}
