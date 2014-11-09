#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct
{
	ngx_str_t hello_string;
	ngx_int_t hello_counter;
}ngx_http_hello_loc_conf_t;

static ngx_int_t ngx_http_hello_init(ngx_conf_t *cf);

static void *ngx_http_hello_create_loc_conf(ngx_conf_t *cf);

static char *ngx_http_hello_string(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); // 处理hello_string参数的函数

static char *ngx_http_hello_counter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);// 处理hello_counter参数的函数


/* 模块的命令数组 */
static ngx_command_t ngx_http_hello_commands[] =
{
	{
		ngx_string("hello_string"),
		NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS | NGX_CONF_TAKE1,
		ngx_http_hello_string,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_hello_loc_conf_t, hello_string),
		NULL
	},
	{
		ngx_string("hello_counter"),
		NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
		ngx_http_hello_counter,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_hello_loc_conf_t, hello_counter),
		NULL
	},
	ngx_null_command
};

/*
 * static u_char ngx_hello_default_string[] = "Default String: Hello, world!";
 */

static int ngx_hello_visited_times = 0;

static ngx_http_module_t ngx_http_hello_module_ctx =
{
	NULL,                          /* preconfiguration */
	ngx_http_hello_init,           /* postconfiguration: 完成配置文件解析后调用 */

	NULL,                          /* create main configuration */
	NULL,                          /* init main configuration */

	NULL,                          /* create server configuration */
	NULL,                          /* merge server configuration */

	ngx_http_hello_create_loc_conf, /* create location configuration */
	NULL                            /* merge location configuration */
};

ngx_module_t ngx_http_hello_module =
{
	NGX_MODULE_V1,                 /* define the first seven fields of struct ngx_module_t */
	&ngx_http_hello_module_ctx,    /* module context */
	ngx_http_hello_commands,       /* module directives */
	NGX_HTTP_MODULE,               /* module type */
	NULL,                          /* init master */
	NULL,                          /* init module */
	NULL,                          /* init process */
	NULL,                          /* init thread */
	NULL,                          /* exit thread */
	NULL,                          /* exit process */
	NULL,                          /* exit master */
	NGX_MODULE_V1_PADDING
};


static ngx_int_t ngx_http_hello_handler(ngx_http_request_t *r)
{
	ngx_int_t                 rc;
	ngx_buf_t                 *b;
	ngx_chain_t               out;
	ngx_http_hello_loc_conf_t *my_conf;
	u_char                    ngx_hello_string[1024] = {0};
        ngx_uint_t                content_length;

	ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "ngx_http_hello_handler is called !");

	my_conf = ngx_http_get_module_loc_conf(r, ngx_http_hello_module);
	if(my_conf->hello_string.len == 0)
	{
		ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "hello_string is empty!");
		return NGX_DECLINED;
	}

	/* 根据hello_counter参数决定是否启用计数，并在客户端显示计数统计 */
	if (my_conf->hello_counter == NGX_CONF_UNSET || my_conf->hello_counter == 0)
	{
		ngx_sprintf(ngx_hello_string, "%s", my_conf->hello_string.data);
	}
	else
	{
		ngx_sprintf(ngx_hello_string, "%s Visited Times: %d", my_conf->hello_string.data, ++ngx_hello_visited_times);
	}

	ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "hello_string: %s", ngx_hello_string);
	content_length = ngx_strlen(ngx_hello_string);

	/* we response to 'GET' and 'HEAD' requests only */
	if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD)))
	{
		return NGX_HTTP_NOT_ALLOWED;
	}

	/* discard request body, since we don't need it here */
	rc = ngx_http_discard_request_body(r);
	if (rc != NGX_OK)
	{
		return rc;
	}

	/* set the 'Content-type' header */
	/*
	 * r->headers_out.content_type.len = sizeof("text/html") - 1;
	 * r->headers_out.content_type.data = (u_char *)"text/html";
	 */
	ngx_str_set(&r->headers_out.content_type, "text/html");
	/* send the header only, if the request type is http 'HEAD' */
	if (r->method == NGX_HTTP_HEAD)
	{
		r->headers_out.status = NGX_HTTP_OK;
		r->headers_out.content_length_n = content_length;
		return ngx_http_send_header(r);
	}
	/* allocate a buffer for your response body */
	b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if (b == NULL)
	{
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	/* attach this buffer to the buffer chain */
	out.buf = b;
	out.next = NULL;

	/* adjust the pointers of the buffer */
	b->pos = ngx_hello_string;
	b->last = ngx_hello_string + content_length;
	b->memory = 1;    /* this buffer is in memory */
	b->last_buf = 1;  /* this is the last buffer in the buffer chain */

	/* set the status line */
	r->headers_out.status = NGX_HTTP_OK;
	r->headers_out.content_length_n = content_length;

	/* send the headers of your response */
	rc = ngx_http_send_header(r);

	if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
		return rc;
	}
	/* send the buffer chain of your response */
	return ngx_http_output_filter(r, &out);
}

static void *ngx_http_hello_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_hello_loc_conf_t *local_conf = NULL;
	local_conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_hello_loc_conf_t));
	if(local_conf == NULL)
	{
		return NULL;
	}

	ngx_str_null(&local_conf->hello_string);
	local_conf->hello_counter = NGX_CONF_UNSET;
	return local_conf;
}

/*
   static char *ngx_http_hello_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
   {
   ngx_http_hello_loc_conf_t* prev = parent;
   ngx_http_hello_loc_conf_t* conf = child;

   ngx_conf_merge_str_value(conf->hello_string, prev->hello_string, ngx_hello_default_string);
   ngx_conf_merge_value(conf->hello_counter, prev->hello_counter, 0);

   return NGX_CONF_OK;
   }
   */

static char *ngx_http_hello_string(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{

	ngx_http_hello_loc_conf_t* local_conf;


	local_conf = conf;
    /* ngx_conf_set_str_slot从配置中获取指令参数，并将参数存储到配置文件结构体中 */
	char* rv = ngx_conf_set_str_slot(cf, cmd, conf);

	ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "hello_string:%s", local_conf->hello_string.data);

	return rv;
}


/*
 * 在解析hello_counter命令时执行的解析函数: 将hello_counter(含义：一个bool值)转换为
 * 0或者1，0代表不启用访问计数，1代表启用访问计数
 * */
static char *ngx_http_hello_counter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	ngx_http_hello_loc_conf_t* local_conf;

	local_conf = conf;

	char* rv = NULL;

	/* ngx_conf_set_flag_slot：把“on”和“off”转成1和0，并保存到配置文件结构体中 */
	rv = ngx_conf_set_flag_slot(cf, cmd, conf);

	ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "hello_counter:%d", local_conf->hello_counter);
	return rv;
}

/* 本函数在完成配置文件解析后调用 */
static ngx_int_t ngx_http_hello_init(ngx_conf_t *cf)
{
	ngx_http_handler_pt        *h;
	ngx_http_core_main_conf_t  *cmcf;

	cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

	/*
	 * 将ngx_http_hello_handler挂载到NGX_HTTP_CONTENT_PHASE阶段，
	 * CONTENT_PHASE阶段的所有的handler(处理函数)组成一个链表，
	 * 新挂载的处理函数位于链表头
	 */
	h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
	if (h == NULL) {
		return NGX_ERROR;
	}
	*h = ngx_http_hello_handler;
	return NGX_OK;
}



