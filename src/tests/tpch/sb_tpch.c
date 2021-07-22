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
#include "db_driver.h"

#include "sysbench.h"

#define TPCH_QUERIES 22
#define TPCH_QUERY_OFFSET(iteration) (iteration % TPCH_QUERIES == 0 ? TPCH_QUERIES : iteration % TPCH_QUERIES)

/* TPC-H test arguments */
static sb_arg_t tpch_args[] =
{
  SB_OPT("data-size", "Size of the data to generate in GB", "", INT),
  SB_OPT("root-path", "Absolute path to sysbench's root", "", STRING),
  SB_OPT("report-json", "Print the results in JSON format", "off", BOOL),
  SB_OPT_END
};

/* TPC-H test operations */
static int tpch_prepare(void);
static int tpch_init(void);
static void tpch_print_mode(void);
static sb_event_t tpch_next_event(int thread_id);
static int tpch_execute_event(sb_event_t *, int);
static void tpch_report_intermediate(sb_stat_t *);
static void tpch_report_cumulative(sb_stat_t *);
static int tpch_done(void);

/* TPC-H test struct */
typedef struct tpch_s {
    unsigned int size;
    char *root_path;
    char *query_path;
    db_driver_t *db_driver;
    char **sql_queries;
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
    .report_intermediate = tpch_report_intermediate,
    .report_cumulative = tpch_report_cumulative,
    .done = tpch_done
  },
  .builtin_cmds = {
    .prepare = tpch_prepare
  },
  .args = tpch_args
};

static void tpch_report_json(const double seconds, sb_stat_t *stat)
{
    printf("[\n"
           "\t{\n"
           "\t\t\"time\": %d,\n"
           "\t\t\"threads\": %d,\n"
           "\t\t\"tps\": %4.2f,\n"
           "\t\t\"qps\": {\n"
           "\t\t\t\"total\": %4.2f,\n"
           "\t\t\t\"reads\": %4.2f,\n"
           "\t\t\t\"writes\": %4.2f,\n"
           "\t\t\t\"other\": %4.2f,\n"
           "\t\t\"latency\": %4.2f,\n"
           "\t\t\"errors\": %4.2f,\n"
           "\t\t\"reconnects\": %4.2f\n"
           "\t}\n"
           "]\n",
           (int)seconds,
           0,
           stat->events / seconds,
           (stat->reads + stat->writes + stat->other) / seconds,
           stat->reads / seconds,
           stat->writes / seconds,
           stat->other / seconds,
           SEC2MS(stat->latency_pct),
           stat->errors / seconds,
           stat->reconnects / seconds);
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

static char *add_path_to_root(char *add)
{
    char *path = malloc(strlen(tpch.root_path) + strlen(add) + 2);

    if (path == NULL) {
        return NULL;
    }
    strcat(path, tpch.root_path);
    if (strlen(tpch.root_path) > 0 && tpch.root_path[strlen(tpch.root_path) - 1] != '/') {
        strcat(path, "/");
    }
    strcat(path, add);
    return path;
}

static char** get_script_arguments(const char *path)
{
    const int nb_args = 4;
    char args_str[4][32] = {
            "--size",
            "",
            "--mysql-params",
            "tpch"
    };
    char **args = malloc(sizeof(char *) * (nb_args + 2));
    if (args == NULL)
        return NULL;

    sprintf(args_str[1], "%d", tpch.size);

    args[0] = strdup(path);
    if (args[0] == NULL) {
        free(args);
        return NULL;
    }

    for (int i = 0; i < nb_args; i++) {
        args[i+1] = malloc(strlen(args_str[i]));
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
    pid_t pid = fork();

    if (pid == -1) {
        return 1;
    }
    if (pid == 0) {
        path = add_path_to_root("src/tests/tpch/scripts");
        if (path == NULL)
            return 1;
        chdir(path);

        exec_path = malloc(strlen(path) + strlen("/tpch_init.sh"));
        if (exec_path == NULL)
            return 1;
        strcat(exec_path, path);
        strcat(exec_path, "/tpch_init.sh");

        args = get_script_arguments(exec_path);
        if (args == NULL)
            return 1;
        int exit_code = execve(exec_path, args, sb_globals.env);
        for (int i = 0; args[i] != NULL; i++)
            free(args[i]);
        free(args);
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

static char *get_sql_file_content(unsigned int id)
{
    FILE *f = NULL;
    long file_size = 0;
    char *content = NULL;
    char *file_path = malloc((strlen(tpch.query_path) + strlen("/00.sql")));

    if (file_path == NULL)
        return NULL;
    sprintf(file_path, "%s/%02d.sql", tpch.query_path, id);

    f = fopen(file_path, "rb");
    if (f == NULL) {
        free(file_path);
        return NULL;
    }
    free(file_path);
    fseek(f, 0L, SEEK_END);
    file_size = ftell(f);
    rewind(f);

    content = malloc(file_size);
    if (content == NULL) {
        fclose(f);
        return NULL;
    }

    fread(content, file_size, 1, f);
    fclose(f);
    f = NULL;

    return content;
}

static char **load_all_queries()
{
    char **queries = malloc(sizeof(char *) * (TPCH_QUERIES + 1));

    if (queries == NULL)
        return NULL;
    for (int i = 0; i < TPCH_QUERIES; i++) {
        queries[i] = get_sql_file_content(i+1);
        if (queries[i] == NULL) {
            for (int j = i-1; j >= 0; j--)
                free(queries[j]);
            free(queries);
            return NULL;
        }
    }
    queries[TPCH_QUERIES] = NULL;
    return queries;
}

int register_test_tpch(sb_list_t * tests)
{
    SB_LIST_ADD_TAIL(&tpch_test.listitem, tests);

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
    if (get_tpch_args() > 0)
        return 1;

    tpch.db_driver = db_create(NULL);
    if (tpch.db_driver == NULL)
        return 1;

    tpch.query_path = add_path_to_root("src/tests/tpch/scripts/queries");
    if (tpch.query_path == NULL)
        return 1;

    tpch.sql_queries = load_all_queries();
    if (tpch.sql_queries == NULL)
        return 1;
    return 0;
}


sb_event_t tpch_next_event(int thread_id)
{
    sb_event_t req;

    req.type = SB_REQ_TYPE_SQL;

    return req;
}

int tpch_execute_event(sb_event_t *r, int thread_id)
{
    /* unused */
    (void)r;
    (void)thread_id;

    static unsigned int event_count = 1;
    db_conn_t *conn = NULL;
    char *query = NULL;
    unsigned int script_id = TPCH_QUERY_OFFSET(event_count);

    event_count++;

    conn = db_connection_create(tpch.db_driver);
    if (conn == NULL)
        return 1;

    query = tpch.sql_queries[script_id-1];

    db_result_t *res = db_query(conn, query, strlen(query));
    if (res == NULL) {
        printf("Error, got NULL result with query id %d\n", script_id);
        return 0;
    }
    db_free_results(res);
    db_connection_free(conn);

    return 0;
}

void tpch_print_mode(void)
{

}

void tpch_report_intermediate(sb_stat_t *stat)
{
    int json_format = sb_get_value_flag("report-json");
    const double seconds = stat->time_total;

    if (json_format) {
        tpch_report_json(seconds, stat);
        return;
    }

    log_timestamp(LOG_NOTICE, stat->time_total,
                  "thds: %u tps: %4.2f "
                  "qps: %4.2f (r/w/o: %4.2f/%4.2f/%4.2f) "
                  "lat (ms,%u%%): %4.2f err/s: %4.2f "
                  "reconn/s: %4.2f",
                  stat->threads_running,
                  stat->events / seconds,
                  (stat->reads + stat->writes + stat->other) / seconds,
                  stat->reads / seconds,
                  stat->writes / seconds,
                  stat->other / seconds,
                  sb_globals.percentile,
                  SEC2MS(stat->latency_pct),
                  stat->errors / seconds,
                  stat->reconnects / seconds);
}

/* Print cumulative stats. */

void tpch_report_cumulative(sb_stat_t *stat)
{
    int json_format = sb_get_value_flag("report-json");
    const double seconds = stat->time_total;

    if (json_format) {
        tpch_report_json(seconds, stat);
        return;
    }
    log_timestamp(LOG_NOTICE, stat->time_total,
                  "thds: %u tps: %4.2f "
                  "qps: %4.2f (r/w/o: %4.2f/%4.2f/%4.2f) "
                  "lat (ms,%u%%): %4.2f err/s: %4.2f "
                  "reconn/s: %4.2f",
                  stat->threads_running,
                  stat->events / seconds,
                  (stat->reads + stat->writes + stat->other) / seconds,
                  stat->reads / seconds,
                  stat->writes / seconds,
                  stat->other / seconds,
                  sb_globals.percentile,
                  SEC2MS(stat->latency_pct),
                  stat->errors / seconds,
                  stat->reconnects / seconds);

    sb_report_cumulative(stat);
}

int tpch_done(void)
{
    for (int i = 0; tpch.sql_queries != NULL && tpch.sql_queries[i] != NULL; i++)
        free(tpch.sql_queries[i]);
    if (tpch.sql_queries != NULL)
        free(tpch.sql_queries);
    free(tpch.query_path);

    if (tpch.db_driver != NULL)
        db_destroy(tpch.db_driver);
    return 0;
}
