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

#include "sysbench.h"

/* CPU test arguments */
static sb_arg_t tpch_args[] =
{
  SB_OPT("path-dbgen", "upper limit for primes generator", "", STRING),
  SB_OPT("path-qgen", "upper limit for primes generator", "", STRING),

  SB_OPT_END
};

/* CPU test operations */
static int tpch_init(void);
static void tpch_print_mode(void);
static sb_event_t tpch_next_event(int thread_id);
static int tpch_execute_event(sb_event_t *, int);
static void tpch_report_cumulative(sb_stat_t *);
static int tpch_done(void);

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
  .args = tpch_args
};

/* Upper limit for primes */
static unsigned int    max_prime;

int register_test_tpch(sb_list_t * tests)
{
  SB_LIST_ADD_TAIL(&tpch_test.listitem, tests);

  return 0;
}

int tpch_init(void)
{
  int prime_option= sb_get_value_int("cpu-max-prime");
  if (prime_option <= 0)
  {
    log_text(LOG_FATAL, "Invalid value of cpu-max-prime: %d.", prime_option);
    return 1;
  }
  max_prime= (unsigned int)prime_option;

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
