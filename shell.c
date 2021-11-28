#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static sigjmp_buf loop_env;

static void sigint_handler(int sig) {
  siglongjmp(loop_env, sig);
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT
    (void)mode;
    (void)MaybeClose;
    mode = token[i];
    if (mode == T_OUTPUT) {
      token[i] = NULL;
      i++;
      MaybeClose(outputp);
      *outputp = Open(token[i], O_CREAT | O_WRONLY | O_TRUNC, 0644);
      token[i] = NULL;
    } else if (mode == T_APPEND) {
      token[i] = NULL;
      i++;
      MaybeClose(outputp);
      *outputp = Open(token[i], O_CREAT | O_WRONLY | O_APPEND, 0644);
      token[i] = NULL;
    } else if (mode == T_INPUT) {
      token[i] = NULL;
      i++;
      MaybeClose(inputp);
      *inputp = Open(token[i], O_CREAT | O_RDONLY, 0644);
      token[i] = NULL;
    } else {
      token[n] = token[i];
      n++;
    }
#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  pid_t pid = Fork();
  int job = -1;
  if (pid == 0) {
    Setpgid(0, 0);
    Sigprocmask(SIG_SETMASK, &mask, NULL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);
    Signal(SIGCHLD, SIG_DFL);
    Signal(SIGTSTP, SIG_DFL);

    if (input != -1) {
      dup2(input, STDIN_FILENO);
      Close(input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      Close(output);
    }
    if ((exitcode = builtin_command(token)) >= 0)
      exit(exitcode);
    external_command(token);
  }

  job = addjob(pid, bg);
  addproc(job, pid, token);

  MaybeClose(&input);
  MaybeClose(&output);

  if (!bg)
    exitcode = monitorjob(&mask);
  else
    msg("[%d] running '%s'\n", job, jobcmd(job));
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT
  if (pid == 0) {
    Setpgid(0, 0);
    Sigprocmask(SIG_SETMASK, mask, NULL);
    Signal(SIGCHLD, SIG_DFL); // set signal handlers to default in child
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

    if (input != -1) {
      dup2(input, STDIN_FILENO);
      Close(input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      Close(output);
    }
    int exitcode = 0;
    if ((exitcode = builtin_command(token)) >= 0)
      exit(exitcode);
    external_command(token);
  }
#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT
  (void)input;
  (void)job;
  (void)pid;
  (void)pgid;
  (void)do_stage;
  for (int i = 0; i < ntokens; i++) {
    int x = i;
    while (token[x] != NULL && token[x] != T_PIPE)
      x += 1;

    token[x] = NULL;

    if (x < ntokens && i != 0) // for i == 0 already created
      mkpipe(&next_input, &output);

    pid = do_stage(pgid, &mask, input, output, token + i, x - i, 0);

    if (i == 0) {
      pgid = pid;
      job = addjob(pgid, bg);
    }

    MaybeClose(&input); // zamykam deskryptory potoku
    MaybeClose(&output);
    addproc(job, pid, token + i);
    input = next_input;
    i = x;
  }
  if (!bg)
    exitcode = monitorjob(&mask);
  else
    msg("[%d] running '%s'\n", job, jobcmd(job));
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  char *line = Malloc(MAXLINE);
  int len, res;

  write(STDOUT_FILENO, prompt, strlen(prompt));

  for (len = 0; len < MAXLINE; len++) {
    if (!(res = Read(STDIN_FILENO, line + len, 1)))
      break;

    if (line[len] == '\n') {
      line[len] = '\0';
      return line;
    }
  }

  if (len == 0) {
    free(line);
    return NULL;
  }

  return line;
}
#endif

int main(int argc, char *argv[]) {
#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  Signal(SIGINT, sigint_handler);
  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  char *line;
  while (true) {
    if (!sigsetjmp(loop_env, 1)) {
      line = readline("# ");
    } else {
      msg("\n");
      continue;
    }

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
