#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
/*
 * 执行流程：
 * 1. 在nginx载入nginx.conf文件时，会解析配置文件，当碰到mytest配置项时，会执行ngx_http_upstream_mytest_create_loc_conf函数和函数ngx_http_upstream_mytest_merge_loc_conf
 * 2. 在解析配置时，当出现mytest配置项时，将调用配置的方法ngx_http_upstream_mytest函数处理配置项参数
 * 3. 在ngx_http_upstream_mytest函数中以按需挂载的方式设置处理函数ngx_http_upstream_mytest_handler
 * 4. 当客户端发来请求时，在ngx_http_upstream_mytest_handler中对请求初始化upstream成员并根据配置项中的参数设置upstream，然后设置upstream的回调方法(create_request, process_header, finalize_request)，最后调用ngx_http_upstream_init启动upstream
 * 5. upstream模块会去检查文件缓存，如果缓存中已有合适的响应包，则会直接返回缓存，否则将调用create_request创建向上游发送的请求
 * 6. 当收到上游的响应时，调用注册的process_header函数解析相应头部,下面的例子中将响应的解析拆分为两个函数，分别解析响应行和响应头部
 *
 */

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
        ngx_http_upstream_mytest, // 当碰到mytest配置项时，执行函数ngx_http_upstream_mytest
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

    // buffering为0时，使用固定的缓冲区缓存上游返回的结果
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

// 合并配置项函数
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
    static ngx_str_t backendQueryLine = ngx_string("GET /s?wd=%V HTTP/1.1\r\nHost:www.baidu.com\r\nConnection: close\r\n\r\n");

    ngx_int_t queryLineLen = backendQueryLine.len + r->args.len - 2;
    ngx_buf_t *b = ngx_create_temp_buf(r->pool, queryLineLen);
    if (b == NULL)
        return NGX_ERROR;
    b->last = b->pos + queryLineLen;
    ngx_snprintf(b->pos, queryLineLen, (char*)backendQueryLine.data, &r->args);
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "param is %V", &r->args);
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
    // 在解析完http响应行之后解析http响应头部
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
        // rc == NGX_OK表明解析出一行HTTP头部
        if (rc == NGX_OK)
        {
            // 将刚刚解析出来的头部保存到headers_in中
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

            // 在umcf->headers_in_hash中查找key为h->hash的值，如果查找到，则更新值
            hh = ngx_hash_find(&umcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
            if (hh && hh->handler(r, h, hh->offset) != NGX_OK)
                return NGX_ERROR;
            continue;
        }

        // 所有HTTP头部都解析完毕
        if (rc == NGX_HTTP_PARSE_HEADER_DONE)
        {
            // 如果之前没有从http头部中解析出server和date，则根据HTTP规范添加这两个域
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

        // 还没解析完整的HTTP头部，需要继续接收上游数据，然后继续解析
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

// 在配置项处理函数中设置handler
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

    // 对请求初始化r->upstream成员
    if (ngx_http_upstream_create(r) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_http_upstream_create() failed");
        return NGX_ERROR;
    }

    ngx_http_upstream_mytest_conf_t *mycf = (ngx_http_upstream_mytest_conf_t *) ngx_http_get_module_loc_conf(r, ngx_http_upstream_mytest_module);
    ngx_http_upstream_t *u = r->upstream;

    // 将配置结构体中的参数赋给upstream
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

    //设置上游的地址，端口
    backendSockAddr.sin_family = AF_INET;
    backendSockAddr.sin_port = htons((in_port_t)80);
    char *pDmsIp = inet_ntoa(*(struct in_addr*)(pHost->h_addr_list[0]));
    backendSockAddr.sin_addr.s_addr = inet_addr(pDmsIp);
    myctx->backendServer.data = (u_char*)pDmsIp;
    myctx->backendServer.len = strlen(pDmsIp);

    u->resolved->sockaddr = (struct sockaddr *)&backendSockAddr;
    u->resolved->socklen = sizeof(struct sockaddr_in);
    u->resolved->naddrs = 1;

    // 设置回调函数
    u->create_request = mytest_upstream_create_request;
    u->process_header = ngx_http_upstream_mytest_process_status_line;
    u->finalize_request = mytest_upstream_finalize_request;

    // 增加引用计数，然后执行初始化
    r->main->count++;
    ngx_http_upstream_init(r);
    return NGX_DONE;
}
