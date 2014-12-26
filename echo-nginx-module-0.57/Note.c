                HttpEchoModule学习
一. 框架:
ngx_http_echo_module.h ngx_http_echo_module.c
这两个文件包含了模块的整体框架:

定义：
    Echo模块支撑很多命令(echo, echo_duplicate, echo_flush等等)，
ngx_http_echo_module.h中将每个命令定义为一个opcode(ngx_http_echo_opcode_t)，
ngx_http_echo_cmd_category_t将所有的命令分为两类: (1). echo_handler_cmd,
(2). echo_filter_cmd, 前者表示命令是一个自己独立的执行行为，后者则表示
命令用于对其执行行为的结果进行过滤。
    Echo模块的命令支持处理nginx配置文件定义的脚本变量:

    ngx_http_echo_cmd_t定义了命令和参数列表
    typedef struct {
        ngx_http_echo_opcode_t      opcode;

        /* each argument is of type echo_arg_template_t: */
        ngx_array_t                 *args;
    } ngx_http_echo_cmd_t;

    ngx_http_echo_arg_template_t定义了命令的参数类型，lengths和
values是为了处理nginx变量存在的。
    typedef struct {
        /* holds the raw string of the argument value */
        ngx_str_t       raw_value;
        /* fields "lengths" and "values" are set by
        * the function ngx_http_script_compile,
        * iff the argument value indeed contains
        * nginx variables like "$foo" */
        ngx_array_t     *lengths;
        ngx_array_t     *values;
    } ngx_http_echo_arg_template_t;


执行流程：
    1.  命令的set函数(例如：ngx_http_echo_echo, ngx_http_echo_echo_sleep等等)调用
        ngx_http_echo_helper函数
    2.  ngx_http_echo_helper函数根据命令的类型注册处理handler(ngx_http_echo_handler)，
        并且调用nginx的脚本函数(ngx_http_script_variables_count, ngx_http_script_compile)保存
        和处理命令参数
    3.  当服务端收到客户端请求，执行命令时调用ngx_http_echo_handler，handler主要调用
        ngx_http_echo_run_cmds来根据具体的命令执行响应的函数
        ngx_http_echo_run_cmds主要完成两个工作：
        3.1. 解析出要执行的命令参数值(包括求出脚本变量的值和分离出参数选项)
        3.2. 根据命令的opcode执行具体的函数 


