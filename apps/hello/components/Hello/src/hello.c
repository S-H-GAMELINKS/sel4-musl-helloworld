#include <ruby.h>
#include <stdio.h>

static void trace(const char *message)
{
    printf("%s\n", message);
    fflush(stdout);
}

int run(void)
{
    trace("before ruby_init");
    ruby_init();
    trace("after ruby_init");
    rb_eval_string("puts 'Hello from CRuby on seL4'");
    trace("after rb_eval_string");
    ruby_finalize();
    trace("after ruby_finalize");
    return 0;
}
