#include "emacs-module.h"

#define S(s) (env->intern(env, s))

#define JOYMACS_OPEN                                                     \
    "(joymacs-open N)\n"                                                 \
    "\n"                                                                 \
    "Create a handle for the Nth joystick."

#define JOYMACS_CLOSE                                                    \
    "(joymacs-close JOYSTICK)\n"                                         \
    "\n"                                                                 \
    "Immediately destroy JOYSTICK handle.\n"                             \
    "\n"                                                                 \
    "Handles close automatically through garbage collection, but this\n" \
    "releases the resources immediately."

#define JOYMACS_READ                                                     \
    "(joymacs-read JOYSTICK EVENT)\n"                                    \
    "\n"                                                                 \
    "Fill 5-element vector EVENT with a single joystick event.\n"        \
    "\n"                                                                 \
    "Elements of EVENT are [time type value number init-p],\n"           \
    "where \"type\" is :button or :axis. Returns EVENT on success,\n"    \
    "or if no events are available."

int plugin_is_GPL_compatible;

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <linux/joystick.h>

static void
fin_close(void *fdptr)
{
    int fd = (intptr_t)fdptr;
    if (fd != -1)
        close(fd);
}

static emacs_value
joymacs_open(emacs_env *env, ptrdiff_t n, emacs_value *args, void *ptr)
{
    (void)ptr;
    (void)n;
    int id = env->extract_integer(env, args[0]);
    if (env->non_local_exit_check(env) != emacs_funcall_exit_return)
        return S("nil");
    char buf[64];
    int buflen = sprintf(buf, "/dev/input/js%d", id);
    int fd = open(buf, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        emacs_value signal = env->intern(env, "file-error");
        emacs_value message = env->make_string(env, buf, buflen);
        env->non_local_exit_signal(env, signal, message);
        return S("nil");
    }
    return env->make_user_ptr(env, fin_close, (void *)(intptr_t)fd);
}


static emacs_value
joymacs_close(emacs_env *env, ptrdiff_t n, emacs_value *args, void *ptr)
{
    (void)ptr;
    (void)n;
    int fd = (intptr_t)env->get_user_ptr(env, args[0]);
    if (env->non_local_exit_check(env) != emacs_funcall_exit_return)
        return S("nil");
    if (fd != -1) {
        close(fd);
        env->set_user_ptr(env, args[0], (void *)(intptr_t)-1);
    }
    return S("nil");
}


static emacs_value
joymacs_read (emacs_env *env, ptrdiff_t nargs,
              emacs_value *args, void *data)
{
  (void)nargs;
  (void)data;

  /* ------------------------------------------------------------------ */
  /* 1.  Get the file descriptor.                                       */
  /* ------------------------------------------------------------------ */
  int fd = (intptr_t) env->get_user_ptr (env, args[0]);

  /* Bail out if a non-local exit is already pending.  */
  if (env->non_local_exit_check (env) != emacs_funcall_exit_return)
    return S ("nil");

  /* ------------------------------------------------------------------ */
  /* 2.  Try to read one joystick event.                                */
  /* ------------------------------------------------------------------ */
  struct js_event e;
  int r = read (fd, &e, sizeof e);

  if (r == -1 && errno == EAGAIN)           /* No event ready.  */
    return S ("nil");

  if (r == -1) {                            /* Real I/O error.  */
    emacs_value Qfile_error = env->intern (env, "file-error");
    const char *msg         = strerror (errno);
    emacs_value message     = env->make_string (env, msg, strlen (msg));
    env->non_local_exit_signal (env, Qfile_error, message);
    return S ("nil");
  }

  /* ------------------------------------------------------------------ */
  /* 3.  Build and return a fresh vector describing the event.          */
  /* ------------------------------------------------------------------ */
  emacs_value Qnil    = S ("nil");

  emacs_value make_vector_sym = env->intern (env, "make-vector");
  emacs_value args_mv[2]      = { env->make_integer (env, 5), Qnil };
  emacs_value vec             = env->funcall (env, make_vector_sym, 2, args_mv);

  emacs_value Qbutton = S (":button");
  emacs_value Qtype   = (e.type & JS_EVENT_BUTTON) ? Qbutton : S (":axis");

  emacs_value Qvalue;
  if (Qtype == Qbutton)
    Qvalue = e.value ? S ("t") : S ("nil");
  else
    Qvalue = env->make_float (env, e.value / (double) INT16_MAX);

  env->vec_set (env, vec, 0, env->make_integer (env, e.time));
  env->vec_set (env, vec, 1, Qtype);
  env->vec_set (env, vec, 2, Qvalue);
  env->vec_set (env, vec, 3, env->make_integer (env, e.number));
  env->vec_set (env, vec, 4, (e.type & JS_EVENT_INIT) ? S ("t") : S ("nil"));

  return vec;
}

int
emacs_module_init(struct emacs_runtime *ert)
{
    emacs_env *env = ert->get_environment(ert);

    /* Bind functions. */
    emacs_value fset = env->intern(env, "fset");
    emacs_value args[2];
    args[0] = env->intern(env, "joymacs-open");
    args[1] = env->make_function(env, 1, 1, joymacs_open, JOYMACS_OPEN, 0);
    env->funcall(env, fset, 2, args);
    args[0] = env->intern(env, "joymacs-close");
    args[1] = env->make_function(env, 1, 1, joymacs_close, JOYMACS_CLOSE, 0);
    env->funcall(env, fset, 2, args);
    args[0] = env->intern(env, "joymacs-read");
    args[1] = env->make_function(env, 2, 2, joymacs_read, JOYMACS_READ, 0);
    env->funcall(env, fset, 2, args);

    /* (provide 'joymacs) */
    emacs_value provide = env->intern(env, "provide");
    emacs_value joymacs = env->intern(env, "joymacs");
    env->funcall(env, provide, 1, &joymacs);
    return 0;
}
