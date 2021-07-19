/* Copyright (C) 2004 MySQL AB
   Copyright (C) 2004-2018 Alexey Kopytov <akopytov@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_MATH_H
# include <math.h>
#endif

#include <inttypes.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sysbench.h"

/* TPC-H test arguments */
static sb_arg_t tpch_args[] =
{
  SB_OPT("data-size", "Size of the data to generate in GB", "", INT),
  SB_OPT("root-path", "Absolute path to sysbench's root", "", STRING),

  SB_OPT_END
};

/* TPC-H test operations */
static int tpch_prepare(void);
static int tpch_init(void);
static void tpch_print_mode(void);
static sb_event_t tpch_next_event(int thread_id);
static int tpch_execute_event(sb_event_t *, int);
static void tpch_report_cumulative(sb_stat_t *);
static int tpch_done(void);

/* TPC-H test struct */
typedef struct tpch_s {
    unsigned int size;
    char *root_path;
} tpch_t;

static tpch_t tpch = {};

static sb_test_t tpch_test =
{
  .sname = "tpch",
  .lname = "TPC-H performance test",
  .ops = {
    .init = tpch_init,
    .print_mode = tpch_print_mode,
    .next_event = tpch_next_event,
    .execute_event = tpch_execute_event,
    .report_cumulative = tpch_report_cumulative,
    .done = tpch_done
  },
  .builtin_cmds = {
    .prepare = tpch_prepare
  },
  .args = tpch_args
};

/* Upper limit for primes */
static unsigned int    max_prime;

int register_test_tpch(sb_list_t * tests)
{
  SB_LIST_ADD_TAIL(&tpch_test.listitem, tests);

  return 0;
}

static int get_tpch_args(void)
{
    int size = sb_get_value_int("data-size");
    char *root_path = sb_get_value_string("root-path");

    if (size <= 0) {
        log_text(LOG_FATAL, "Invalid value of data-size: %d.", size);
        return 1;
    }
    if (root_path == NULL) {
        log_text(LOG_FATAL, "Invalid value of root-path, got NULL.");
        return 1;
    }

    tpch.size = (unsigned int)size;
    tpch.root_path = root_path;
    return 0;
}

static char *get_script_dir_path(void)
{
    int size = strlen(tpch.root_path) + strlen("/src/tests/tpch/scripts") + 1;
    char *path = malloc(sizeof(char) * size);

    if (path == NULL) {
        return NULL;
    }
    strcat(path, tpch.root_path);
    if (strlen(tpch.root_path) > 0 && tpch.root_path[strlen(tpch.root_path) - 1] != '/') {
        strcat(path, "/");
    }
    strcat(path, "src/tests/tpch/scripts");
    return path;
}

static char** get_script_arguments(const char *path)
{
    const int nb_args = 4;
    char args_str[nb_args][32] = {
            "--size",
            "",
            "--mysql-params",
            "tpch"
    };
    char **args = malloc(sizeof(char *) * (nb_args + 2));
    if (args == NULL)
        return NULL;

    sprintf(args_str[1], "%d", tpch.size);

    args[0] = malloc(sizeof(char) * strlen(path));
    if (args[0] == NULL) {
        free(args);
        return NULL;
    }

    strcpy(args[0], path);
    for (int i = 0; i < nb_args; i++) {
        args[i+1] = malloc(sizeof(char) * strlen(args_str[i]));
        if (args[i+1] == NULL) {
            for (int j = i; j >= 0; j--)
                free(args[j]);
            free(args);
            return NULL;
        }
        strcpy(args[i+1], args_str[i]);
    }
    args[nb_args+1] = NULL;
    return args;
}

static int execute_init_script(void)
{
    char *path = NULL;
    char *exec_path = NULL;
    char **args = NULL;
    int status = 0;
    struct sigaction saves[2];
    pid_t pid = fork();

    if (pid == -1) {
        return 1;
    }
    if (pid == 0) {
        path = get_script_dir_path();
        if (path == NULL)
            return 1;
        chdir(path);

        exec_path = malloc(sizeof(char) * (strlen(path) + strlen("/tpch_init.sh")));
        if (exec_path == NULL)
            return 1;
        strcat(exec_path, path);
        strcat(exec_path, "/tpch_init.sh");

        args = get_script_arguments(exec_path);
        if (args == NULL)
            return 1;
        int exit_code = execve(exec_path, args, sb_globals.env);
        if (exit_code == -1)
            printf("Something went wrong: %s\n", strerror(errno));
        exit(exit_code);
    }
    waitpid(pid, &status, 0);

    free(path);
    free(exec_path);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 0;
}

int tpch_prepare(void)
{
    if (get_tpch_args() > 0) {
        return 1;
    }
    execute_init_script();

    return 0;
}

int tpch_init(void)
{
    if (get_tpch_args() > 0) {
        return 1;
    }

    return 0;
}


sb_event_t tpch_next_event(int thread_id)
{
  sb_event_t req;

  (void) thread_id; /* unused */

  req.type = SB_REQ_TYPE_CPU;

  return req;
}

int tpch_execute_event(sb_event_t *r, int thread_id)
{
  unsigned long long c;
  unsigned long long l;
  double t;
  unsigned long long n=0;

  (void)thread_id; /* unused */
  (void)r; /* unused */

  /* So far we're using very simple test prime number tests in 64bit */

  for(c=3; c < max_prime; c++)
  {
    t = sqrt((double)c);
    for(l = 2; l <= t; l++)
      if (c % l == 0)
        break;
    if (l > t )
      n++; 
  }

  return 0;
}

void tpch_print_mode(void)
{
  log_text(LOG_INFO, "Doing CPU performance benchmark\n");  
  log_text(LOG_NOTICE, "Prime numbers limit: %d\n", max_prime);
}

/* Print cumulative stats. */

void tpch_report_cumulative(sb_stat_t *stat)
{
  log_text(LOG_NOTICE, "CPU speed:");
  log_text(LOG_NOTICE, "    events per second: %8.2f",
           stat->events / stat->time_interval);

  sb_report_cumulative(stat);
}


int tpch_done(void)
{
  return 0;
}
