#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include "ngx_http_echo_echo.h"
#include "ngx_http_echo_util.h"
#include "ngx_http_echo_filter.h"

#include <nginx.h>

// 创建space和'\n'两个static字符串
static ngx_buf_t ngx_http_echo_space_buf;

static ngx_buf_t ngx_http_echo_newline_buf;


// 初始化 space_buf和newline_buf
ngx_int_t
ngx_http_echo_echo_init(ngx_conf_t *cf)
{
    static u_char space_str[]   = " ";
    static u_char newline_str[] = "\n";

    dd("global init...");

    ngx_memzero(&ngx_http_echo_space_buf, sizeof(ngx_buf_t));

    ngx_http_echo_space_buf.memory = 1;

    ngx_http_echo_space_buf.start =
        ngx_http_echo_space_buf.pos =
            space_str;

    ngx_http_echo_space_buf.end =
        ngx_http_echo_space_buf.last =
            space_str + sizeof(space_str) - 1;

    ngx_memzero(&ngx_http_echo_newline_buf, sizeof(ngx_buf_t));

    ngx_http_echo_newline_buf.memory = 1;

    ngx_http_echo_newline_buf.start =
        ngx_http_echo_newline_buf.pos =
            newline_str;

    ngx_http_echo_newline_buf.end =
        ngx_http_echo_newline_buf.last =
            newline_str + sizeof(newline_str) - 1;

    return NGX_OK;
}

/*
 正常的发送包体:
 ngx_http_echo_send_chain_link:
    ----> ngx_http_echo_send_header_if_needed 发送响应头部
    ----> ngx_http_output_filter 发送响应包体
*/
ngx_int_t
ngx_http_echo_exec_echo_sync(ngx_http_request_t *r,
    ngx_http_echo_ctx_t *ctx)
{
    ngx_buf_t                   *buf;
    ngx_chain_t                 *cl = NULL; /* the head of the chain link */

    buf = ngx_calloc_buf(r->pool);
    if (buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* 
        如果整块buffer经过处理完以后，没有数据了，你可以把buffer的sync标志置上，
        表示只是同步的用处
                        FROM : http://tengine.taobao.org/book/chapter_4.html
    */
    buf->sync = 1;

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cl->buf  = buf;
    cl->next = NULL;

    return ngx_http_echo_send_chain_link(r, ctx, cl);
}


/* 完成两个功能：
 * 1. 将参数字符串串在一起，相互之间用space_buf隔开，结尾加上换行字符串 
 * 2. 调用ngx_http_echo_send_chain_link发送响应 */

ngx_int_t
ngx_http_echo_exec_echo(ngx_http_request_t *r,
    ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args,
    ngx_flag_t in_filter, ngx_array_t *opts)
{
    ngx_uint_t                  i;

    ngx_buf_t                   *space_buf;
    ngx_buf_t                   *newline_buf;
    ngx_buf_t                   *buf;

    ngx_str_t                   *computed_arg;
    ngx_str_t                   *computed_arg_elts;
    ngx_str_t                   *opt;

    ngx_chain_t *cl  = NULL; /* the head of the chain link */
    ngx_chain_t **ll = &cl;  /* always point to the address of the last link */


    dd_enter();

    if (computed_args == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    // 遍历每个参数字符串
    computed_arg_elts = computed_args->elts;
    for (i = 0; i < computed_args->nelts; i++) {
        computed_arg = &computed_arg_elts[i];

        // 将参数字符串保存在分配的buf中
        if (computed_arg->len == 0) {
            buf = NULL;

        } else {
            buf = ngx_calloc_buf(r->pool);
            if (buf == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            buf->start = buf->pos = computed_arg->data;
            buf->last = buf->end = computed_arg->data +
                computed_arg->len;

            buf->memory = 1;
        }

        // 将所有保存参数字符串的buf串起来保存到ngx_chain_t *cl中，参数字符串之间间隔一个space
        if (cl == NULL) {
            cl = ngx_alloc_chain_link(r->pool);
            if (cl == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            cl->buf  = buf;
            cl->next = NULL;
            ll = &cl->next;

        } else {
            /* append a space first */
            *ll = ngx_alloc_chain_link(r->pool);

            if (*ll == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            space_buf = ngx_calloc_buf(r->pool);

            if (space_buf == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            /* 如下所说：nginx在处理每个request的结尾时会清除掉buf的flags，
             * 因此我们分配一个space_buf来保存ngx_http_echo_space_buf，而不是
             * 把ngx_http_echo_space_buf直接串到cl中 */
            /* nginx clears buf flags at the end of each request handling,
             * so we have to make a clone here. */
            *space_buf = ngx_http_echo_space_buf;

            (*ll)->buf = space_buf;
            (*ll)->next = NULL;

            ll = &(*ll)->next;

            /* then append the buf only if it's non-empty */
            if (buf) {
                *ll = ngx_alloc_chain_link(r->pool);
                if (*ll == NULL) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
                (*ll)->buf  = buf;
                (*ll)->next = NULL;

                ll = &(*ll)->next;
            }
        }
    } /* end for */

    if (cl && cl->buf == NULL) {
        cl = cl->next;
    }

    if (opts && opts->nelts > 0) {
        opt = opts->elts;
        if (opt[0].len == 1 && opt[0].data[0] == 'n') {
            goto done;
        }
    }

    /* append the newline character */
    /* 上面附加了参数字符串之后，这里结尾处附加一个换行符字符串 */
    newline_buf = ngx_calloc_buf(r->pool);

    if (newline_buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    *newline_buf = ngx_http_echo_newline_buf;

    if (cl == NULL) {
        cl = ngx_alloc_chain_link(r->pool);

        if (cl == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        cl->buf = newline_buf;
        cl->next = NULL;
        /* ll = &cl->next; */

    } else {
        *ll = ngx_alloc_chain_link(r->pool);

        if (*ll == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        (*ll)->buf  = newline_buf;
        (*ll)->next = NULL;
        /* ll = &(*ll)->next; */
    }

    // 生成包体后，发送响应
done:

    if (cl == NULL || cl->buf == NULL) {
        return NGX_OK;
    }

    // 目前in_filter参数为0，不会进入if分支
    if (in_filter) {
        return ngx_http_echo_next_body_filter(r, cl);
    }
    // 调用ngx_http_echo_send_chain_link发送响应：响应头和包体
    return ngx_http_echo_send_chain_link(r, ctx, cl);
}


// 赶紧将数据发送出去
/*
 ngx_buf_t : flush字段
 flush: 遇到有flush字段被设置为1的的buf的chain，则该chain的数据即便不是最后结束的数据
 （last_buf被设置，标志所有要输出的内容都完了），也会进行输出，不会受postpone_output配置的限制，
 但是会受到发送速率等其他条件的限制。
*/
ngx_int_t
ngx_http_echo_exec_echo_flush(ngx_http_request_t *r, ngx_http_echo_ctx_t *ctx)
{
    // ngx_http_send_special(r, NGX_HTTP_FLUSH); 分配一个ngx_buf_t，然后设置flush为1,最后发送出去
    return ngx_http_send_special(r, NGX_HTTP_FLUSH);
}


ngx_int_t
ngx_http_echo_exec_echo_request_body(ngx_http_request_t *r,
    ngx_http_echo_ctx_t *ctx)
{
    ngx_buf_t       *b;
    ngx_chain_t     *out, *cl, **ll;

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        return NGX_OK;
    }

    out = NULL;
    ll = &out;

    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        /* 
            检测buf: pass掉特殊的buf
             #define  ngx_buf_special(b)                                                   \
                ((b->flush || b->last_buf || b->sync)                                    \
                && !ngx_buf_in_memory(b) && !b->in_file)
        */
        if (ngx_buf_special(cl->buf)) {
            /* we do not want to create zero-size bufs */
            continue;
        }

        *ll = ngx_alloc_chain_link(r->pool);
        if (*ll == NULL) {
            return NGX_ERROR;
        }

        b = ngx_alloc_buf(r->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }

        (*ll)->buf = b;
        (*ll)->next = NULL;

        ngx_memcpy(b, cl->buf, sizeof(ngx_buf_t));
        // 这里保存tag用处是什么
        b->tag = (ngx_buf_tag_t) &ngx_http_echo_exec_echo_request_body;
        b->last_buf = 0;
        b->last_in_chain = 0;

        ll = &(*ll)->next;
    }

    if (out == NULL) {
        return NGX_OK;
    }

    return ngx_http_echo_send_chain_link(r, ctx, out);
}


ngx_int_t
ngx_http_echo_exec_echo_duplicate(ngx_http_request_t *r,
    ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args)
{
    ngx_str_t                   *computed_arg;
    ngx_str_t                   *computed_arg_elts;
    ssize_t                     i, count;
    ngx_str_t                   *str;
    u_char                      *p;
    ngx_int_t                   rc;

    ngx_buf_t                   *buf;
    ngx_chain_t                 *cl;


    dd_enter();

    computed_arg_elts = computed_args->elts;

    // 获取重复的次数数字
    computed_arg = &computed_arg_elts[0];

    count = ngx_http_echo_atosz(computed_arg->data, computed_arg->len);

    if (count == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "invalid size specified: \"%V\"", computed_arg);

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    str = &computed_arg_elts[1];

    if (count == 0 || str->len == 0) {
        rc = ngx_http_echo_send_header_if_needed(r, ctx);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            return rc;
        }

        return NGX_OK;
    }

    // 分配内存并循环copy
    buf = ngx_create_temp_buf(r->pool, count * str->len);
    if (buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    p = buf->pos;
    for (i = 0; i < count; i++) {
        p = ngx_copy(p, str->data, str->len);
    }
    buf->last = p;

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cl->next = NULL;
    cl->buf = buf;

    return ngx_http_echo_send_chain_link(r, ctx, cl);
}
