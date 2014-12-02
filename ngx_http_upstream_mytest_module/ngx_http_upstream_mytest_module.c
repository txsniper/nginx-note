#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

// 定义模块相关的上下文结构体
typedef struct
{
    // 上游的返回状态行
    ngx_http_status_t   status;
    ngx_str_t           backendServer;
} ngx_http_upstream_mytest_ctx_t;

typedef struct
{
    ngx_http_upstream_conf_t upstream;
} ngx_http_upstream_mytest_conf_t;


static char *
ngx_http_upstream_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t
ngx_http_upstream_mytest_handler(ngx_http_request_t *r);

static void*
ngx_http_upstream_mytest_create_loc_conf(ngx_conf_t *cf);

static char*
ngx_http_upstream_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_int_t
ngx_http_upstream_mytest_process_header(ngx_http_request_t *r);

static ngx_int_t
ngx_http_upstream_mytest_process_status_line(ngx_http_request_t *r);


static ngx_str_t ngx_http_proxy_hide_headers[] =
{
    ngx_string("Date"),
    ngx_string("Server"),
    ngx_string("X-Pad"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_null_string
};

static ngx_command_t ngx_http_upstream_mytest_commands[] =
{
    {
        ngx_string("mytest"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LMT_CONF | NGX_CONF_NOARGS,
        ngx_http_upstream_mytest,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_upstream_mytest_module_ctx =
{
    NULL,
    NULL,

    NULL,
    NULL,

    NULL,
    NULL,

    ngx_http_upstream_mytest_create_loc_conf,
    ngx_http_upstream_mytest_merge_loc_conf
};

ngx_module_t ngx_http_upstream_mytest_module =
{
    NGX_MODULE_V1,
    &ngx_http_upstream_mytest_module_ctx,
    ngx_http_upstream_mytest_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};

// 创建配置项结构体
static void* ngx_http_upstream_mytest_create_loc_conf(ngx_conf_t* cf)
{
    ngx_http_upstream_mytest_conf_t *mycf;
    mycf = (ngx_http_upstream_mytest_conf_t *)ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_mytest_conf_t));
    if (mycf == NULL)
        return NULL;
    mycf->upstream.connect_timeout= 60000;
    mycf->upstream.send_timeout= 60000;
    mycf->upstream.read_timeout = 60000;
    mycf->upstream.store_access= 0600;

    mycf->upstream.buffering = 0;
    mycf->upstream.bufs.num = 8;
    mycf->upstream.bufs.size = ngx_pagesize;
    mycf->upstream.buffer_size = ngx_pagesize;
    mycf->upstream.busy_buffers_size = 2 * ngx_pagesize;
    mycf->upstream.temp_file_write_size = 2 * ngx_pagesize;
    mycf->upstream.max_temp_file_size = 1024 * 1024 * 1024;

    mycf->upstream.hide_headers = NGX_CONF_UNSET_PTR;
    mycf->upstream.pass_headers = NGX_CONF_UNSET_PTR;
    return mycf;
}

static char *ngx_http_upstream_mytest_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{

    ngx_http_upstream_mytest_conf_t *prev = (ngx_http_upstream_mytest_conf_t*)parent;
    ngx_http_upstream_mytest_conf_t* conf = (ngx_http_upstream_mytest_conf_t*)child;
    ngx_hash_init_t hash;
    hash.max_size = 100;
    hash.bucket_size = 1024;
    hash.name = "proxy_headers_hash";
    if (ngx_http_upstream_hide_headers_hash(cf, &conf->upstream, &prev->upstream, ngx_http_proxy_hide_headers, &hash) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
}

// 创建upstream的请求
static ngx_int_t mytest_upstream_create_request(ngx_http_request_t *r)
{
    static ngx_str_t backendQueryLine = ngx_string("GET /search?wd=%V HTTP/1.1\r\nHost:www.baidu.com\r\nConnection: close\r\n\r\n");

    ngx_int_t queryLineLen = backendQueryLine.len + r->args.len - 2;
    ngx_buf_t *b = ngx_create_temp_buf(r->pool, queryLineLen);
    if (b == NULL)
        return NGX_ERROR;
    b->last = b->pos + queryLineLen;
    ngx_snprintf(b->pos, queryLineLen, (char*)backendQueryLine.data, &r->args);
    r->upstream->request_bufs = ngx_alloc_chain_link(r->pool);
    if (r->upstream->request_bufs == NULL)
        return NGX_ERROR;

    // request_bufs只包含了一个ngx_buf_t缓冲区
    r->upstream->request_bufs->buf = b;
    r->upstream->request_bufs->next = NULL;

    r->upstream->request_sent = 0;
    r->upstream->header_sent = 0;

    // header_hash必须不为0
    r->header_hash = 1;
    return NGX_OK;

}

static ngx_int_t ngx_http_upstream_mytest_process_status_line(ngx_http_request_t *r)
{
    size_t len;
    ngx_int_t rc;
    ngx_http_upstream_t* u;

    ngx_http_upstream_mytest_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_upstream_mytest_module);
    if (ctx == NULL)
        return NGX_ERROR;
    u = r->upstream;
    // 解析返回的状态行，并保存到ctx->status
    rc = ngx_http_parse_status_line(r, &u->buffer, &ctx->status);
    // 如果没有解析到完整的http响应行，则返回NGX_AGAIN
    if (rc == NGX_AGAIN)
        return rc;
    // 如果解析失败，则表示没有接收到合法的http响应行
    if (rc == NGX_ERROR)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent no vaild HTTP/1.0 header");
        r->http_version = NGX_HTTP_VERSION_9;
        u->state->status = NGX_HTTP_OK;
        return NGX_OK;
    }

    if (u->state)
    {
        u->state->status = ctx->status.code;
    }

    u->headers_in.status_n = ctx->status.code;
    len = ctx->status.end - ctx->status.start;
    u->headers_in.status_line.len = len;
    // 将http状态行保存到upstream->headers_in.status_line
    u->headers_in.status_line.data = ngx_pnalloc(r->pool, len);
    if (u->headers_in.status_line.data == NULL)
    {
        return NGX_ERROR;
    }
    ngx_memcpy(u->headers_in.status_line.data, ctx->status.start, len);

    u->process_header = ngx_http_upstream_mytest_process_header;
    return ngx_http_upstream_mytest_process_header(r);
}

static ngx_int_t ngx_http_upstream_mytest_process_header(ngx_http_request_t *r)
{
    ngx_int_t rc;
    ngx_table_elt_t *h;
    ngx_http_upstream_header_t *hh;
    ngx_http_upstream_main_conf_t *umcf;

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    for(;;)
    {
        rc = ngx_http_parse_header_line(r, &r->upstream->buffer, 1);
        if (rc == NGX_OK)
        {
            h = ngx_list_push(&r->upstream->headers_in.headers);
            if (h == NULL)
                return NGX_ERROR;

            h->hash = r->header_hash;
            h->key.len = r->header_name_end - r->header_name_start;
            h->value.len = r->header_end - r->header_start;
            h->key.data = ngx_pnalloc(r->pool, h->key.len + 1 + h->value.len + 1 + h->key.len);
            if (h->key.data == NULL)
                return NGX_ERROR;
            h->value.data = h->key.data + h->key.len + 1;
            h->lowcase_key = h->key.data + h->key.len + 1 + h->value.len + 1;
            ngx_memcpy(h->key.data, r->header_name_start, h->key.len);
            h->key.data[h->key.len] = '\0';
            ngx_memcpy(h->value.data, r->header_start, h->value.len);
            h->value.data[h->value.len] = '\0';

            if (h->key.len == r->lowcase_index)
            {
                ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);
            }
            else
            {
                ngx_strlow(h->lowcase_key, h->key.data, h->key.len);
            }

            hh = ngx_hash_find(&umcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
            if (hh && hh->handler(r, h, hh->offset) != NGX_OK)
                return NGX_ERROR;
            continue;
        }

        if (rc == NGX_HTTP_PARSE_HEADER_DONE)
        {
            if (r->upstream->headers_in.server == NULL)
            {
                h = ngx_list_push(&r->upstream->headers_in.headers);
                if (h == NULL)
                {
                    return NGX_ERROR;
                }
                h->hash = ngx_hash(ngx_hash(ngx_hash(ngx_hash(ngx_hash('s','e'),'r'),'v'),'e'),'r');
                ngx_str_set(&h->key, "Server");
                ngx_str_null(&h->value);
                h->lowcase_key = (u_char*)"Server";
            }

            if (r->upstream->headers_in.date == NULL)
            {
                h = ngx_list_push(&r->upstream->headers_in.headers);
                if (h == NULL)
                    return NGX_ERROR;
                h->hash = ngx_hash(ngx_hash(ngx_hash('d', 'a'), 't'), 'e');
                ngx_str_set(&h->key, "Date");
                ngx_str_null(&h->value);
                h->lowcase_key = (u_char *)"date";
            }
            return NGX_OK;
        }

        if (rc == NGX_AGAIN)
            return NGX_AGAIN;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent invalid header");
        return NGX_HTTP_UPSTREAM_INVALID_HEADER;
    }
}

static void mytest_upstream_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "mytest_upstream_finalize_request");
}

static char* ngx_http_upstream_mytest(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_upstream_mytest_handler;
    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_upstream_mytest_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_mytest_ctx_t *myctx = ngx_http_get_module_ctx(r, ngx_http_upstream_mytest_module);
    if (myctx == NULL)
    {
        myctx = ngx_palloc(r->pool, sizeof(ngx_http_upstream_mytest_ctx_t));
        if (myctx == NULL)
            return NGX_ERROR;
        ngx_http_set_ctx(r, myctx, ngx_http_upstream_mytest_module);
    }

    if (ngx_http_upstream_create(r) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_http_upstream_create() failed");
        return NGX_ERROR;
    }

    ngx_http_upstream_mytest_conf_t *mycf = (ngx_http_upstream_mytest_conf_t *) ngx_http_get_module_loc_conf(r, ngx_http_upstream_mytest_module);
    ngx_http_upstream_t *u = r->upstream;
    u->conf = &mycf->upstream;
    u->buffering = mycf->upstream.buffering;

    u->resolved = (ngx_http_upstream_resolved_t*)ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_resolved_t));
    if (u->resolved == NULL)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_pcalloc resolved error: %s.", strerror(errno));
        return NGX_ERROR;
    }

    static struct sockaddr_in backendSockAddr;
    struct hostent *pHost = gethostbyname((char*)"www.baidu.com");
    if (pHost == NULL)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "gethostbyname fail. %s", strerror(errno));
        return NGX_ERROR;
    }

    backendSockAddr.sin_family = AF_INET;
    backendSockAddr.sin_port = htons((in_port_t)80);
    char *pDmsIp = inet_ntoa(*(struct in_addr*)(pHost->h_addr_list[0]));
    backendSockAddr.sin_addr.s_addr = inet_addr(pDmsIp);
    myctx->backendServer.data = (u_char*)pDmsIp;
    myctx->backendServer.len = strlen(pDmsIp);

    u->resolved->sockaddr = (struct sockaddr *)&backendSockAddr;
    u->resolved->socklen = sizeof(struct sockaddr_in);
    u->resolved->naddrs = 1;

    u->create_request = mytest_upstream_create_request;
    u->process_header = ngx_http_upstream_mytest_process_status_line;
    u->finalize_request = mytest_upstream_finalize_request;

    r->main->count++;
    ngx_http_upstream_init(r);
    return NGX_DONE;
}