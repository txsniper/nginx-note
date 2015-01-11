#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include "ngx_http_echo_util.h"
#include "ngx_http_echo_location.h"
#include "ngx_http_echo_handler.h"

#include <nginx.h>


static ngx_int_t ngx_http_echo_adjust_subrequest(ngx_http_request_t *sr);


ngx_int_t
ngx_http_echo_exec_echo_location_async(ngx_http_request_t *r,
    ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args)
{
    ngx_int_t                    rc;
    ngx_http_request_t          *sr; /* subrequest object */
    ngx_str_t                   *computed_arg_elts;
    ngx_str_t                    location;
    ngx_str_t                   *url_args;
    ngx_str_t                    args;
    ngx_uint_t                   flags = 0;

    dd_enter();

    computed_arg_elts = computed_args->elts;

    location = computed_arg_elts[0];

    if (location.len == 0) {
        return NGX_ERROR;
    }

    if (computed_args->nelts > 1) {
        url_args = &computed_arg_elts[1];
    } else {
        url_args = NULL;
    }

    args.data = NULL;
    args.len = 0;

    // 判断location是否是一个安全的URI
    if (ngx_http_parse_unsafe_uri(r, &location, &args, &flags) != NGX_OK) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "echo_location_async sees unsafe uri: \"%V\"",
                       &location);
        return NGX_ERROR;
    }

    if (args.len > 0 && url_args == NULL) {
        url_args = &args;
    }

    // 发送响应头部
    rc = ngx_http_echo_send_header_if_needed(r, ctx);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    // 发送subrequest
    rc = ngx_http_subrequest(r, &location, url_args, &sr, NULL, 0);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_http_echo_adjust_subrequest(sr);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_echo_exec_echo_location(ngx_http_request_t *r,
    ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args)
{
    ngx_int_t                            rc;
    ngx_http_request_t                  *sr; /* subrequest object */
    ngx_str_t                           *computed_arg_elts;
    ngx_str_t                            location;
    ngx_str_t                           *url_args;
    ngx_http_post_subrequest_t          *psr;
    ngx_str_t                            args;
    ngx_uint_t                           flags = 0;
    ngx_http_echo_ctx_t                 *sr_ctx;

    computed_arg_elts = computed_args->elts;

    location = computed_arg_elts[0];

    if (location.len == 0) {
        return NGX_ERROR;
    }

    if (computed_args->nelts > 1) {
        url_args = &computed_arg_elts[1];

    } else {
        url_args = NULL;
    }

    args.data = NULL;
    args.len = 0;

    if (ngx_http_parse_unsafe_uri(r, &location, &args, &flags) != NGX_OK) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "echo_location sees unsafe uri: \"%V\"",
                       &location);
        return NGX_ERROR;
    }

    if (args.len > 0 && url_args == NULL) {
        url_args = &args;
    }

    rc = ngx_http_echo_send_header_if_needed(r, ctx);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    sr_ctx = ngx_http_echo_create_ctx(r);

    psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if (psr == NULL) {
        return NGX_ERROR;
    }

    /*
        subrequest执行完后，将会调用ngx_http_echo_post_subrequest方法，
        注意，此时传给你的ngx_http_request_t上下文是子请求的，不是原始的父请求
    */
    psr->handler = ngx_http_echo_post_subrequest;
    psr->data = sr_ctx;

    rc = ngx_http_subrequest(r, &location, url_args, &sr, psr, 0);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_http_echo_adjust_subrequest(sr);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


// 调整subrequest，这里没怎么懂
/*
    应该是这个bug:
    ngx_http_core_module.c, line 2080 (in 0.8.33):
sr->headers_in = r->headers_in;

This results in a bitwise copy of headers_in from the parent request
to the subrequest. Inside headers_in is a list:
typedef struct {
ngx_list_t headers;
// ...
} ngx_http_headers_in_t;

When you do a bitwise copy of an ngx_list_t, you end up with a broken
list. Specifically, sr->headers_in->list->last in the subrequest
points to the last node in the r->headers_in.list.part chain in the
parent request. It should instead point to the last node in the
subrequest's sr->headers_in.list.part chain.

Thus if any module attempts to add more headers to the subrequest's
sr->headers_in, they actually will be added to the parent request's
r->headers_in instead.

I ran into this problem in a module that needs to add extra request
headers to a proxied subrequest.
*/
static ngx_int_t
ngx_http_echo_adjust_subrequest(ngx_http_request_t *sr)
{
    ngx_http_core_main_conf_t   *cmcf;
    ngx_http_request_t          *r;

    /* we do not inherit the parent request's variables */
    cmcf = ngx_http_get_module_main_conf(sr, ngx_http_core_module);

    r = sr->parent;

    sr->header_in = r->header_in;

    /* XXX work-around a bug in ngx_http_subrequest */
    if (r->headers_in.headers.last == &r->headers_in.headers.part) {
        sr->headers_in.headers.last = &sr->headers_in.headers.part;
    }

    sr->variables = ngx_pcalloc(sr->pool, cmcf->variables.nelts
                                * sizeof(ngx_http_variable_value_t));

    if (sr->variables == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}
