/*
 * 一. http_hello_module模块：
 *      (1).nginx.conf:
 *          server {
 *              listen       80;
 *              server_name  localhost;
 *
 *              location / {
 *                  root   html;
 *                  index  index.html index.htm;
 *              }
 *
 *              location /test {
 *                  hello_string Hello_World;
 *                  hello_counter on;
 *              }
 *              ......
 *          }
 *
 *          location为test块内定义了hello_string和hello_counter配置项
 *
 *      (2)代码结构：
 *          ngx_module_t ngx_http_hello_module; 定义了模块的结构
 *          ngx_http_module_t ngx_http_hello_module_ctx; 定义了模块对配置的处理函数
 *          ngx_command_t ngx_http_hello_commands; 定义了模块相关的命令
 *
 *
 * ngx_http_hello_module
 *      .type ---> NGX_HTTP_MODULE  (我们定义的是一个HTTP模块)
 *      .ctx ----> ngx_http_hello_module_ctx
 *                  .postconfiguration ----->ngx_http_hello_init(完成配置文件解析后调用)
 *                  .handle = ngx_http_hello_handler; (设置客户请求匹配本模块配置项时的处理函数)
 *                      .create_loc_conf ----->ngx_http_hello_create_loc_conf(创建数据结构存储loc级别的配置项)
 *
 *      .commands ---> ngx_http_hello_commands
 *                          command1("hello_string")
 *                              .set = ngx_http_hello_string (当http{}, server{}, location{}配置块内定义了hello_string配置项，nginx会调用本方法)
 *                          command2("hello_counter")
 *                              .set = ngx_http_hello_counter( 同上 )
 *
 *      (3)nginx处理流程：
 *           3.1 nginx首先解析nginx.conf中的配置，当碰到http{}配置块时，初始化每一个HTTP模块
 *           3.2 调用各模块的 create_(main,srv,loc)_conf函数, 这时会第一次调用ngx_http_hello_create_loc_conf函数创建
 *               存储配置项的结构
 *           3.3 调用各模块的 postconfiguration函数，这时会调用我们模块的 ngx_http_hello_init函数，该函数会设置处理客户端请求的
 *               handler函数: ngx_http_hello_handler。
 *           3.4 接下来解析http{}块内的每一个配置项，当发现一个配置项时，就遍历所有的HTTP模块，检查每个模块的commands数组中是否有name相同
 *               的command，如果找到了，就调用ngx_command_t结构的set函数
 *           3.5 当在http{}内碰到server{}块时，与之前碰到http{}一样，调用每个HTTP模块的create_(srv,loc)_conf函数，然后保存返回的地址。
 *           3.6 解析server{}里面的每个配置项，在解析到/test时，发现我们定义的配置项( hello_string, hello_counter)，然后调用它们的
 *               set函数(ngx_http_hello_string, ngx_http_hello_counter)。
 *           3.7 解析完server{}和http{}之后，合并配置项。
 *           3.8 然后去解析http{}之外的配置项。
 *           3.9 当用户请求为 /test时，就调用我们的handler函数： ngx_http_hello_handler。
 *
 *      (4) handler的两种挂载方式：
 *          4.1 按处理阶段挂载：
 *          static ngx_int_t
 *          ngx_http_hello_init(ngx_conf_t *cf)
 *          {
 *              ngx_http_handler_pt        *h;
 *              ngx_http_core_main_conf_t  *cmcf;
 *
 *              cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
 *
 *              h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
 *              if (h == NULL) {
 *                  return NGX_ERROR;
 *              }
 *
 *              *h = ngx_http_hello_handler;
 *              return NGX_OK;
 *          }
 *
 *          4.2 按需挂载：
 *              当一个请求进来以后，nginx从NGX_HTTP_POST_READ_PHASE阶段开始依次执行每个阶段中
 *          所有handler。执行到 NGX_HTTP_CONTENT_PHASE阶段的时候，如果这个location有一个对应的
 *          content handler模块，那么就去执行这个content handler模块真正的处理函数。否则继续依
 *          次执行NGX_HTTP_CONTENT_PHASE阶段中所有content phase handlers，直到某个函数处理返回
 *          NGX_OK或者NGX_ERROR。
 *
 *              换句话说，当某个location处理到NGX_HTTP_CONTENT_PHASE阶段时，如果有content handler
 *          模块，那么NGX_HTTP_CONTENT_PHASE挂载的所有content phase handlers都不会被执行了。
 *              但是使用这个方法挂载上去的handler有一个特点是必须在NGX_HTTP_CONTENT_PHASE阶段才能
 *          执行到。如果你想自己的handler在更早的阶段执行，那就不要使用这种挂载方式。
 *
 *              那么在什么情况会使用这种方式来挂载呢？一般情况下，某个模块对某个location进行了
 *          处理以后，发现符合自己处理的逻辑，而且也没有必要再调用NGX_HTTP_CONTENT_PHASE阶段的
 *          其它handler进行处理的时候，就动态挂载上这个handler。
 *
 *          static char *
 *          ngx_http_circle_gif(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
 *          {
 ×              ngx_http_core_loc_conf_t  *clcf;
 ×              clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
 *              clcf->handler = ngx_http_circle_gif_handler;
 ×              return NGX_CONF_OK;
 ×          }
 *
 *
 */
