#include <stdio.h>
#include "cors.h"
#include "options.h"

#define TEST_CORS_WAIT_MS  2000
#define TEST_CORS_TIMEOUT_MS 30000

static int try_load_conf(cors_opt_t *opt, const char *conf)
{
    return cors_loadopts(opt, conf) ? 1 : 0;
}

static int load_default_conf(cors_opt_t *opt)
{
    static const char *candidates[]={
        "../../conf/validation/cors-lab.conf",  /* build/test/ */
        "../conf/validation/cors-lab.conf",     /* build/ */
        "conf/validation/cors-lab.conf",        /* repo root */
        NULL
    };
    int i;

    for (i=0;candidates[i];i++) {
        if (try_load_conf(opt,candidates[i])) return 1;
    }
    return 0;
}

int main(int argc, const char *argv[])
{
    const char *conf=NULL;
    cors_opt_t opt={0};
    cors_t cors={0};
    int waited=0;

    log_trace_open("");
    log_set_level(0);

    if (argc>=2&&argv[1][0]) {
        conf=argv[1];
        if (!try_load_conf(&opt,conf)) {
            fprintf(stderr,"test_cors: cannot load config %s\n",conf);
            return 1;
        }
    }
    else if (!load_default_conf(&opt)) {
        fprintf(stderr,"test_cors: cannot load default cors-lab.conf\n");
        return 1;
    }
    if (!cors_start(&cors,&opt)) {
        fprintf(stderr,"test_cors: cors_start failed\n");
        return 1;
    }

    uv_sleep(TEST_CORS_WAIT_MS);
    cors_close(&cors);

    while (cors.state&&waited<TEST_CORS_TIMEOUT_MS) {
        uv_sleep(100);
        waited+=100;
    }
    if (cors.state) {
        fprintf(stderr,"test_cors: timeout waiting for shutdown\n");
        return 1;
    }

    printf("test_cors: OK\n");
    return 0;
}
