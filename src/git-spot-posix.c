/**
 * Copyright (c) 2006-2010 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "git-spot.h"
#include "cmd.h"

/// Set when libspotify want to process events
static int notify_events;

/// Synchronization mutex to protect various shared data
static pthread_mutex_t notify_mutex;

/// Synchronization condition variable for the main thread
static pthread_cond_t notify_cond;

/// Synchronization condition variable to disable prompt temporarily
static pthread_cond_t prompt_cond;

/// Command line to execute
static char *cmdline;

extern int is_logged_out;




/**
 *
 */
static void trim(char *buf)
{
  size_t l = strlen(buf);
  while(l > 0 && buf[l - 1] < 32)
    buf[--l] = 0;
}


/**
 *
 */
int main(int argc, char **argv)
{
  const char *username = NULL;
  const char *password = NULL;
  int cmdargc;
  char **cmdargv = NULL;
  char username_buf[256];
  int r;
  int next_timeout = 0;
  int opt;

  while ((opt = getopt(argc, argv, "u:p:")) != EOF) {
    switch (opt) {
    case 'u':
      username = optarg;
      break;

    case 'p':
      password = optarg;
      break;

    default:
      exit(1);
    }
  }

  if (optind >= argc){
    fprintf(stderr, "Usage: git-spot [options] command [args]");
    exit(1);
  }

  cmdargv = argv + optind;
  cmdargc = argc - optind;

  if (username == NULL) {
    printf("Username: ");
    fflush(stdout);
    fgets(username_buf, sizeof(username_buf), stdin);
    trim(username_buf);
    username = username_buf;
  }

  if (password == NULL)
    password = getpass("Password: ");

  pthread_mutex_init(&notify_mutex, NULL);
  pthread_cond_init(&notify_cond, NULL);
  pthread_cond_init(&prompt_cond, NULL);

  if ((r = git_spot_init(username, password)) != 0)
    exit(r);

  pthread_mutex_lock(&notify_mutex);

  while(!is_logged_out) {
    // Release prompt

    if (next_timeout == 0) {
      while(!notify_events && !cmdline)
        pthread_cond_wait(&notify_cond, &notify_mutex);
    } else {
      struct timespec ts;

#if _POSIX_TIMERS > 0
      clock_gettime(CLOCK_REALTIME, &ts);
#else
      struct timeval tv;
      gettimeofday(&tv, NULL);
      TIMEVAL_TO_TIMESPEC(&tv, &ts);
#endif

      ts.tv_sec += next_timeout / 1000;
      ts.tv_nsec += (next_timeout % 1000) * 1000000;

      while(!notify_events && !cmdline) {
        if(pthread_cond_timedwait(&notify_cond, &notify_mutex, &ts))
          break;
      }
    }

    // Process initial command
    if(cmdargc > 0) {
      pthread_mutex_unlock(&notify_mutex);
      cmd_dispatch(cmdargc, cmdargv);
      pthread_mutex_lock(&notify_mutex);
      cmdargc = 0;
    }

    // Process libspotify events
    notify_events = 0;
    pthread_mutex_unlock(&notify_mutex);

    do {
      sp_session_process_events(g_session, &next_timeout);
    } while (next_timeout == 0);

    pthread_mutex_lock(&notify_mutex);
  }
  printf("Logged out\n");
  sp_session_release(g_session);
  printf("Exiting...\n");
  return 0;
}

/**
 *
 */
void cmd_done(void)
{
  pthread_mutex_lock(&notify_mutex);
  pthread_cond_signal(&prompt_cond);
  pthread_mutex_unlock(&notify_mutex);
}


/**
 *
 */
void notify_main_thread(sp_session *session)
{
  pthread_mutex_lock(&notify_mutex);
  notify_events = 1;
  pthread_cond_signal(&notify_cond);
  pthread_mutex_unlock(&notify_mutex);
}
