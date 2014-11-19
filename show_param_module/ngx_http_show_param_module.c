#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_string.h>

typedef struct
{
	ngx_int_t enable;
}ngx_http_show_param_loc_conf_t;

static ngx_int_t ngx_http_show_param_init(ngx_conf_t *cf);

// 创建配置信息结构
static void *ngx_http_show_param_create_loc_conf(ngx_conf_t *cf);

static char *ngx_http_show_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);// 处理hello_counter参数的函数

static int ngx_copy_header_in_to_response(ngx_http_request_t *r, u_char* buff, size_t size);

/* 模块的命令数组 */
static ngx_command_t ngx_http_show_param_commands[] =
{
	{
		ngx_string("show_param"),
		NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
        ngx_http_show_param,
        NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_show_param_loc_conf_t, enable),
		NULL
	},
	ngx_null_command
};


static ngx_http_module_t ngx_http_show_param_module_ctx =
{
	NULL,                          /* preconfiguration */
	ngx_http_show_param_init,      /* postconfiguration: 完成配置文件解析后调用 */

	NULL,                          /* create main configuration */
	NULL,                          /* init main configuration */

	NULL,                          /* create server configuration */
	NULL,                          /* merge server configuration */

	ngx_http_show_param_create_loc_conf,      /* create location configuration */
	NULL                           /* merge location configuration */
};

ngx_module_t ngx_http_show_param_module =
{
	NGX_MODULE_V1,                 /* define the first seven fields of struct ngx_module_t */
	&ngx_http_show_param_module_ctx,    /* module context */
	ngx_http_show_param_commands,       /* module directives */
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

static int ngx_copy_header_in_to_response(ngx_http_request_t *r, u_char* buff, size_t size)
{
    ngx_list_part_t *part = &r->headers_in.headers.part;
    ngx_table_elt_t *header = part->elts;
    //ngx_table_elt_t* content_length = headers_in->content_length;

    size_t i = 0;
    size_t len = 0;
    for(i=0; len < size; i++)
    {
        //char temp[1024]= {0};
        if(i >= part->nelts)
        {
            if (part->next == NULL)
            {
                break;
            }
            part = part->next;
            header = part->elts;
            i = 0;
        }

        if(header[i].hash == 0)
        {
            continue;
        }
        //ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "conf param enbale: on\n");
        ngx_snprintf(buff+len, size-len-1, "%V:%V\n", &header[i].key, &header[i].value);
        len += header[i].key.len + header[i].value.len + 2;
    }
    return size;
}
static ngx_int_t ngx_http_show_param_handler(ngx_http_request_t *r)
{
	ngx_int_t                 rc;
	ngx_chain_t               out;
	ngx_http_show_param_loc_conf_t *my_conf;
    ngx_uint_t                content_length;
    u_char                    temp[1024] ={0};
	ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "ngx_http_show_param_handler is called !");

	my_conf = ngx_http_get_module_loc_conf(r, ngx_http_show_param_module);

	/* 根据enable参数决定是否启用计数，并在客户端显示计数统计 */
	if (my_conf->enable == NGX_CONF_UNSET || my_conf->enable == 0)
	{
		ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "conf param enable: off\n");
	}
	else
	{
		ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "conf param enbale: on\n");
	}


    content_length = ngx_copy_header_in_to_response(r, temp, sizeof(temp));

    ngx_buf_t *content = ngx_create_temp_buf(r->pool, content_length);
    if (content == NULL)
    {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_memcpy(content->pos, temp, content_length);
    content->last = content->pos + content_length;
    content->last_buf = 1;
    content->memory = 1;

    ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "content_length : %d", content_length);
    ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "content: %s", temp);

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

	/* attach this buffer to the buffer chain */
	out.buf = content;
	out.next = NULL;

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

static void *ngx_http_show_param_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_show_param_loc_conf_t *local_conf = NULL;
	local_conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_show_param_loc_conf_t));
	if(local_conf == NULL)
	{
		return NULL;
	}

	local_conf->enable = NGX_CONF_UNSET;
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "create loc conf \n");
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


/*
 * 在解析show_param命令时执行的解析函数: 将show_param(含义：一个bool值)转换为
 * 0或者1，0代表不启用访问计数，1代表启用访问计数
 * */
static char *ngx_http_show_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	ngx_http_show_param_loc_conf_t* local_conf;

	local_conf = conf;

	char* rv = NULL;

	/* ngx_conf_set_flag_slot：把“on”和“off”转成1和0，并保存到配置文件结构体中 */
	rv = ngx_conf_set_flag_slot(cf, cmd, conf);

	ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ngx_http_show_param: show_param:%d", local_conf->enable);

    // 按需挂载处理函数handler
    ngx_http_core_loc_conf_t *clcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_show_param_handler;

    ngx_conf_log_error(NGX_LOG_EMERG, cf , 0, "show_param: handler hooked\n");

    return rv;
}

/* 本函数在完成配置文件解析后调用 */
static ngx_int_t ngx_http_show_param_init(ngx_conf_t *cf)
{
    /*
	ngx_http_handler_pt        *h;
	ngx_http_core_main_conf_t  *cmcf;

	cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);


	 // 将ngx_http_hello_handler挂载到NGX_HTTP_CONTENT_PHASE阶段，
	 // CONTENT_PHASE阶段的所有的handler(处理函数)组成一个链表，
	 // 新挂载的处理函数位于链表头

	h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
	if (h == NULL) {
		return NGX_ERROR;
	}
	*h = ngx_http_show_param_handler;
    */
    return NGX_OK;
}



