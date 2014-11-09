/*
 * 一. http_hello_module模块：
 *
 *  1.组织结构：
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
 *           3.1 nginx首先解析nginx.conf中的配置，当碰到http配置项时，初始化每一个HTTP模块
 *           3.2 调用各模块的 create_(main,srv,loc)_conf函数, 这时会第一次调用ngx_http_hello_create_loc_conf函数创建
 *               存储配置项的结构
 *           3.3 调用各模块的 postconfiguration函数，这时会调用我们模块的 ngx_http_hello_init函数
 *
 *
 */
