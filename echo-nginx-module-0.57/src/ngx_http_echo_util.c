
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_http_echo_util.h"
#include "ngx_http_echo_sleep.h"


ngx_uint_t  ngx_http_echo_content_length_hash = 0;


ngx_http_echo_ctx_t *
ngx_http_echo_create_ctx(ngx_http_request_t *r)
{
    ngx_http_echo_ctx_t         *ctx;

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_echo_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->sleep.handler   = ngx_http_echo_sleep_event_handler;
    ctx->sleep.data      = r;
    ctx->sleep.log       = r->connection->log;

    return ctx;
}

// 解析参数选项和脚本变量
ngx_int_t
ngx_http_echo_eval_cmd_args(ngx_http_request_t *r,
    ngx_http_echo_cmd_t *cmd, ngx_array_t *computed_args,
    ngx_array_t *opts)
{
    ngx_uint_t                       i;
    ngx_array_t                     *args = cmd->args;
    ngx_str_t                       *arg, *raw, *opt;
    ngx_http_echo_arg_template_t    *value;
    ngx_flag_t                       expecting_opts = 1;

    value = args->elts;
    /* 解析参数选项和参数值 */
    for (i = 0; i < args->nelts; i++) {
        raw = &value[i].raw_value;

        if (value[i].lengths == NULL && raw->len > 0) {
            if (expecting_opts) {
                if (raw->len == 1 || raw->data[0] != '-') {
                    expecting_opts = 0;

                } else if (raw->data[1] == '-') {
                    expecting_opts = 0;
                    continue;

                } else {
                    opt = ngx_array_push(opts);
                    if (opt == NULL) {
                        return NGX_HTTP_INTERNAL_SERVER_ERROR;
                    }

                    opt->len = raw->len - 1;
                    opt->data = raw->data + 1;

                    dd("pushing opt: %.*s", (int) opt->len, opt->data);

                    continue;
                }
            }

        } else {
            expecting_opts = 0;
        }

        /* 计算脚本变量值 */
        arg = ngx_array_push(computed_args);
        if (arg == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        if (value[i].lengths == NULL) { /* does not contain vars */
            dd("Using raw value \"%.*s\"", (int) raw->len, raw->data);
            *arg = *raw;

        } else {
            if (ngx_http_script_run(r, arg, value[i].lengths->elts,
                                    0, value[i].values->elts)
                == NULL)
            {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
        }

        dd("pushed arg: %.*s", (int) arg->len, arg->data);
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_echo_send_chain_link(ngx_http_request_t* r,
    ngx_http_echo_ctx_t *ctx, ngx_chain_t *in)
{
    ngx_int_t        rc;

    rc = ngx_http_echo_send_header_if_needed(r, ctx);

    // r->header_only为1表示只用发送响应头，因此返回
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    if (in == NULL) {

#if defined(nginx_version) && nginx_version <= 8004

        /* earlier versions of nginx does not allow subrequests
            to send last_buf themselves */
        if (r != r->main) {
            return NGX_OK;
        }

#endif
        // 如果in为NULL，则我们没有别的数据需要发送了，
        // 发送NGX_HTTP_LAST信息，并主动地关闭掉http请求
        rc = ngx_http_send_special(r, NGX_HTTP_LAST);
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }

        return NGX_OK;
    }

    // 发送包体
    return ngx_http_output_filter(r, in);
}


ngx_int_t
ngx_http_echo_send_header_if_needed(ngx_http_request_t* r,
    ngx_http_echo_ctx_t *ctx)
{
    ngx_http_echo_loc_conf_t    *elcf;
    // header_sent为0表示响应头还没发送，为1表示响应头已经发送
    if (!r->header_sent) {
        elcf = ngx_http_get_module_loc_conf(r, ngx_http_echo_module);

        // 设置相应状态码
        r->headers_out.status = (ngx_uint_t) elcf->status;

        // 设置响应头部的 content_type字段
        if (ngx_http_set_content_type(r) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        /*
            本函数在需要时发送响应头部，由于不知道响应包体的长度，因此调用
            ngx_http_clear_content_length函数清除响应头部中的content_length字段，
            ngx_http_clear_accept_ranges函数清除响应头部中的accept_range字段

            Q. ngx_http_clear_content_length 和 ngx_http_clear_accept_ranges 起到了什 么作用，都有哪些场合需要使用？

            A. 这两个函数用于确定无法获知响应的实际长度时，告诉其它 filter 对响应做相应的 处理。具体分析如下：
                ngx_http_clear_content_length 清空响应包头中 Content-Length 相关的字段：

                    r->headers_out.content_length_n = -1; if (r->headers_out.content_length) 
                    { r->headers_out.content_length->hash = 0; r->headers_out.content_length = NULL; }

                ngx_http_clear_accept_ranges 清空响应包头中和 Accept-Ranges 相关的字段：

                    r->allow_ranges = 0; if (r->headers_out.accept_ranges) 
                    { r->headers_out.accept_ranges->hash = 0; r->headers_out.accept_ranges = NULL; }

                ngx_http_range_filter_module 模块由 accept_ranges 向响应包头填充 Accept-Ranges 字段 
                (告知客户端是否可以请求资源的某个范围内的数据)。动态生 成的资源，是不支持通过范围方式请求的。

                content_length_n 标识 Nginx 明确知道请求资源的长度。动态生成的资源，无法 获知准确长度的情况下，
                需要使用 传输编码 对整个报文进行编码，即 Transfer-Encoding。目前最新的 HTTP 规范只定义了一种传输编码，
                chunked。 ngx_http_chunked_filter_module 负责根据 content_length_n 的值判断是否 添加 Transfer-Encoding 
                包头 (设置 r->chunked，由 header_filter 添加)。

            content_length的影响：
                对于http1.0协议来说，如果响应头中有content-length头，则以content-length的长度就可以知道body的长度了，
            客户端在接收body时，就可以依照这个长度来接收数据，接收完后，就表示这个请求完成了。而如果没有content-length头，
            则客户端会一直接收数据，直到服务端主动断开连接，才表示body接收完了。

                而对于http1.1协议来说，如果响应头中的Transfer-encoding为chunked传输，则表示body是流式输出，body会被分
            成多个块，每块的开始会标识出当前块的长度，此时，body不需要通过长度来指定。如果是非chunked传输，而且有
            content-length，则按照content-length来接收数据。否则，如果是非chunked，并且没有content-length，则客户端
            接收数据，直到服务端主动断开连接。
        */
        ngx_http_clear_content_length(r);
        ngx_http_clear_accept_ranges(r);

        // 发送响应头部
        return ngx_http_send_header(r);
    }

    return NGX_OK;
}


ssize_t
ngx_http_echo_atosz(u_char *line, size_t n)
{
    ssize_t  value;

    if (n == 0) {
        return NGX_ERROR;
    }

    for (value = 0; n--; line++) {
        if (*line == '_') { /* we ignore undercores */
            continue;
        }

        if (*line < '0' || *line > '9') {
            return NGX_ERROR;
        }

        value = value * 10 + (*line - '0');
    }

    if (value < 0) {
        return NGX_ERROR;
    }

    return value;
}


/* Modified from the ngx_strlcasestrn function in ngx_string.h
 * Copyright (C) by Igor Sysoev */
u_char *
ngx_http_echo_strlstrn(u_char *s1, u_char *last, u_char *s2, size_t n)
{
    ngx_uint_t  c1, c2;

    c2 = (ngx_uint_t) *s2++;
    last -= n;

    do {
        do {
            if (s1 >= last) {
                return NULL;
            }

            c1 = (ngx_uint_t) *s1++;

        } while (c1 != c2);

    } while (ngx_strncmp(s1, s2, n) != 0);

    return --s1;
}


ngx_int_t
ngx_http_echo_post_request_at_head(ngx_http_request_t *r,
    ngx_http_posted_request_t *pr)
{
    dd_enter();

    if (pr == NULL) {
        pr = ngx_palloc(r->pool, sizeof(ngx_http_posted_request_t));
        if (pr == NULL) {
            return NGX_ERROR;
        }
    }

    pr->request = r;
    pr->next = r->main->posted_requests;
    r->main->posted_requests = pr;

    return NGX_OK;
}


u_char *
ngx_http_echo_rebase_path(ngx_pool_t *pool, u_char *src, size_t osize,
    size_t *nsize)
{
    u_char            *p, *dst;

    if (osize == 0) {
        return NULL;
    }

    if (src[0] == '/') {
        /* being an absolute path already, just add a trailing '\0' */
        *nsize = osize;

        dst = ngx_palloc(pool, *nsize + 1);
        if (dst == NULL) {
            *nsize = 0;
            return NULL;
        }

        p = ngx_copy(dst, src, osize);
        *p = '\0';

        return dst;
    }

    *nsize = ngx_cycle->prefix.len + osize;

    dst = ngx_palloc(pool, *nsize + 1);
    if (dst == NULL) {
        *nsize = 0;
        return NULL;
    }

    p = ngx_copy(dst, ngx_cycle->prefix.data, ngx_cycle->prefix.len);
    p = ngx_copy(p, src, osize);

    *p = '\0';

    return dst;
}


ngx_int_t
ngx_http_echo_flush_postponed_outputs(ngx_http_request_t *r)
{
    if (r == r->connection->data && r->postponed) {
        /* notify the downstream postpone filter to flush the postponed
         * outputs of the current request */
        return ngx_http_output_filter(r, NULL);
    }

    /* do nothing */
    return NGX_OK;
}
