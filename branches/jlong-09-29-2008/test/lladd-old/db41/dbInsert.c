/*---
This software is copyrighted by the Regents of the University of
California, and other parties. The following terms apply to all files
associated with the software unless explicitly disclaimed in
individual files.
                                                                                                                                  
The authors hereby grant permission to use, copy, modify, distribute,
and license this software and its documentation for any purpose,
provided that existing copyright notices are retained in all copies
and that this notice is included verbatim in any distributions. No
written agreement, license, or royalty fee is required for any of the
authorized uses. Modifications to this software may be copyrighted by
their authors and need not follow the licensing terms described here,
provided that the new terms are clearly indicated on the first page of
each file where they apply.
                                                                                                                                  
IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY PARTY
FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
ARISING OUT OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY
DERIVATIVES THEREOF, EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
                                                                                                                                  
THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
NON-INFRINGEMENT. THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, AND
THE AUTHORS AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
                                                                                                                                  
GOVERNMENT USE: If you are acquiring this software on behalf of the
U.S. government, the Government shall have only "Restricted Rights" in
the software and related documentation as defined in the Federal
Acquisition Regulations (FARs) in Clause 52.227.19 (c) (2). If you are
acquiring the software on behalf of the Department of Defense, the
software shall be classified as "Commercial Computer Software" and the
Government shall have only "Restricted Rights" as defined in Clause
252.227-7013 (c) (1) of DFARs. Notwithstanding the foregoing, the
authors grant the U.S. Government and others acting in its behalf
permission to use and distribute the software in accordance with the
terms specified in this license.
---*/

#include "../../pbl/jbhash.h"
#include <db.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#define DATABASE "access.db"
#define ENV_DIRECTORY   "TXNAPP"

#define VERBOSE 0
#define SEED 5095
int db_open(DB_ENV *dbenv, DB **dbp, char *name, int dups);

void env_open(DB_ENV **dbenvp);

void env_dir_create();


int main(int argc, char **argv) {
	DB *dbp;
	DB_ENV *dbenv;
	DB_TXN *xid;
	DBT key, data;
	db_recno_t recno;

	int num_trials;
	int ret;
	char c;


	double r;
	int start, end;

	char first_printable_ascii = ' ';
	char last_printable_ascii = '~';
	int ascii_length = (int)(last_printable_ascii - first_printable_ascii);
	char *ASCII = (char *)malloc(sizeof(char) * ascii_length);
	char *string;

	for (c = 0; c < ascii_length; c++) {
		ASCII[(int)c] = first_printable_ascii + c;
	}

	if (argc != 2)  {
		printf("usage: %s <num trials>\n", argv[0]);
		exit(-1);
	}


	env_dir_create();
	env_open(&dbenv);
	if (db_open(dbenv, &dbp, DATABASE, 0))
		return (1); 	

        memset(&key, 0, sizeof(DBT));
        memset(&data, 0, sizeof(DBT));
	num_trials = atoi(argv[1]);
	for (recno = 1; (int)recno <= num_trials; recno++) {
		start = 0;
		end = 0;
		while (start == end) {	
			r = ((double)rand()/(double)((double)RAND_MAX+1)); /* returns [0, 1)*/
			r = r*ascii_length;
			start = (int)r; /* an int in the rand [0, ascii_length) */

			r = ((double)rand()/(double)((double)RAND_MAX+1)); /* re turns [0, 1)*/
			r = r*ascii_length;
			end = (int)r; /* an int in the rand [0, ascii_length) */
		}
		if (end < start) {
			int swap = start;
			start = end;
			end = swap;
		}

		string = (char *)malloc(sizeof(char) * (end - start) + 1);
		strncpy(string, ASCII + start, end-start);
		string[end-start] = '\0'; /* make the string null terminated */

		dbenv->txn_begin(dbenv, NULL, &xid, 0);
		key.size = sizeof(recno);
		key.data = &recno;
		data.size = strlen(string) + 1; // + 1 for the null terminator	
		data.data = string;
		switch (ret = dbp->put(dbp, xid, &key, &data, 0)) {
			case 0:
				xid->commit(xid, 0);
				break;
			default:
				dbp->err(dbp, ret, "DB->put");
				xid->abort(xid);
				break;
		}


	}


	return 0;

}


int db_open(DB_ENV *dbenv, DB **dbp, char *name, int dups) {
        DB *db;
        int ret;


        /* Create the database handle. */
        if ((ret = db_create(&db, dbenv, 0)) != 0) {
                dbenv->err(dbenv, ret, "db_create");
                return (1);
        }


        /* Optionally, turn on duplicate data items. */
        if (dups && (ret = db->set_flags(db, DB_DUP)) != 0) {
                (void)db->close(db, 0);
                dbenv->err(dbenv, ret, "db->set_flags: DB_DUP");
                return (1);
        }


        /*
         * Open a database in the environment:
         *      create if it doesn't exist
         *      free-threaded handle
         *      read/write owner only
         */
        if ((ret = db->open(db, NULL, name, NULL, DB_BTREE,
            DB_AUTO_COMMIT | DB_CREATE | DB_THREAD, S_IRUSR | S_IWUSR)) != 0) {
                (void)db->close(db, 0);
                dbenv->err(dbenv, ret, "db->open: %s", name);
                return (1);
        }


        *dbp = db;
        return (0);
}


void env_open(DB_ENV **dbenvp) {
        DB_ENV *dbenv;
        int ret;


        /* Create the environment handle. */
        if ((ret = db_env_create(&dbenv, 0)) != 0) {
                fprintf(stderr,
                    "txnapp: db_env_create: %s\n", db_strerror(ret));
                exit (1);
        }


        /* Set up error handling. */
        dbenv->set_errpfx(dbenv, "txnapp");
        dbenv->set_errfile(dbenv, stderr);


        /*
         * Open a transactional environment:
         *      create if it doesn't exist
         *      free-threaded handle
         *      run recovery
         *      read/write owner only
         */
        if ((ret = dbenv->open(dbenv, ENV_DIRECTORY,
            DB_CREATE | DB_INIT_LOCK | DB_INIT_LOG |
            DB_INIT_MPOOL | DB_INIT_TXN | DB_RECOVER | DB_THREAD,
            S_IRUSR | S_IWUSR)) != 0) {
                (void)dbenv->close(dbenv, 0);
                fprintf(stderr, "dbenv->open: %s: %s\n",
                    ENV_DIRECTORY, db_strerror(ret));
                exit (1);
        }


        *dbenvp = dbenv;
}


void env_dir_create() {
        struct stat sb;


        /*
         * If the directory exists, we're done.  We do not further check
         * the type of the file, DB will fail appropriately if it's the
         * wrong type.
         */
        if (stat(ENV_DIRECTORY, &sb) == 0)
                return;


        /* Create the directory, read/write/access owner only. */
        if (mkdir(ENV_DIRECTORY, S_IRWXU) != 0) {
                fprintf(stderr,
                    "txnapp: mkdir: %s: %s\n", ENV_DIRECTORY, strerror(errno));
                exit (1);
        }
}

